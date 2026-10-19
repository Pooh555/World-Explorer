#!/usr/bin/env python3
"""Associate newly observed ELF files with installed owners/notices, offline.

Package queries and selected file reads only. No imports of worker backends,
installation, model loading, reconstruction, archive search or rights clearance.
"""

import argparse
from email.parser import BytesParser
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import sys

from audit_runtime_dependencies import CommandRunner
from audit_worker_environment import file_digest, publish_receipt
import check_backend_bindings as mappings
from check_dependency_manifest import (
    load_json,
    read_file,
    receipt_packages,
    safe_relative,
)

__all__ = [
    "FIELDS",
    "GAPS",
    "MAX_EVIDENCE",
    "MAX_GROUPS",
    "MAX_NODES",
    "MAX_NOTICES",
    "MAX_NOTICE_BYTES",
    "SCOPE",
    "SOURCES",
    "capture",
    "check_summary",
    "main",
    "package_fields",
    "public_fields",
    "reference",
    "validate_file_binding",
    "verify_reference",
]

SCOPE = "installed ownership/notices for baseline_unseen mapped ELF files; not archive, bundled-term or rights closure"
MAX_NODES = 512
MAX_GROUPS = 256
MAX_NOTICES = 256
MAX_NOTICE_BYTES = 16 * 1024**2
MAX_EVIDENCE = 16 * 1024**2
GAPS = [
    "ORIGINAL_ARCHIVES_NOT_BOUND_IN_THIS_ASSOCIATION",
    "SOURCE_BINARY_BINDING_UNVERIFIED",
    "BUNDLED_AND_TRANSITIVE_TERMS_UNREVIEWED",
]
SOURCES = [
    "tools/audit_binding_owners.py",
    "tools/audit_runtime_dependencies.py",
    "tools/audit_worker_environment.py",
    "tools/check_backend_bindings.py",
    "tools/check_dependency_manifest.py",
]
FIELDS = [
    "schema_version",
    "status",
    "scope",
    "rights_qualified",
    "sources",
    "mapping",
    "selection",
    "installed_receipts",
    "package_tool",
    "nodes",
    "groups",
    "listed_bytes",
]


def reference(root, text):
    path = mappings.relative_path(root, text)
    sha256, size = file_digest(path, MAX_EVIDENCE)
    return {"path": text, "sha256": sha256, "bytes": size}


def verify_reference(root, value, local):
    safe_relative(value["path"])
    if (
        not re.fullmatch(r"[0-9a-f]{64}", value["sha256"])
        or type(value["bytes"]) is not int
        or not 0 < value["bytes"] <= MAX_EVIDENCE
    ):
        raise ValueError("Invalid ownership evidence binding")
    if local and reference(root, value["path"]) != value:
        raise ValueError("Ownership evidence binding changed")


def public_fields(report):
    return {field: report[field] for field in FIELDS}


def validate_file_binding(value):
    if (
        not re.fullmatch(r"[0-9a-f]{64}", value["sha256"])
        or type(value["bytes"]) is not int
        or not 0 <= value["bytes"] <= MAX_EVIDENCE
    ):
        raise ValueError("Invalid ownership file fingerprint")
    text = value["path"]
    if text.startswith("repo:"):
        safe_relative(text[5:])
    elif text.startswith("system:"):
        path = PurePosixPath(text[7:])
        if (
            path.as_posix() != text[7:]
            or ".." in path.parts
            or "\\" in text
            or "\0" in text
            or not path.is_relative_to("/usr")
        ):
            raise ValueError("Invalid system ownership path")
    else:
        raise ValueError("Invalid ownership file namespace")


def package_fields(name, output):
    fields = dict(re.findall(r"^(Name|Version|URL|Licenses)\s+: (.*)$", output, re.M))
    if fields.get("Name") != name or set(fields) != {
        "Name",
        "Version",
        "URL",
        "Licenses",
    }:
        raise ValueError("Invalid installed system package metadata")
    return {
        "kind": "system-package",
        "name": name,
        "version": fields["Version"],
        "source_url": fields["URL"],
        "declared_license": fields["Licenses"],
    }


def capture(root, mapping_text, selection_text, runner=None):
    root = root.resolve(strict=True)
    runner = runner or CommandRunner()
    watched = {}

    def snapshot(path):
        info = path.stat()
        return (
            info.st_dev,
            info.st_ino,
            info.st_size,
            info.st_mtime_ns,
            info.st_ctime_ns,
        )

    def observe(key, maximum):
        path = mappings.node_path(root, key)
        before = snapshot(path)
        sha256, size = file_digest(path, maximum)
        if snapshot(path) != before:
            raise ValueError("Ownership input changed while hashing")
        watched[key] = before
        return {"path": key, "sha256": sha256, "bytes": size}

    def input_binding(text):
        value = observe("repo:" + text, MAX_EVIDENCE)
        return {**value, "path": text}

    source_bindings = [input_binding(text) for text in SOURCES]
    mapping_binding, selection_binding = input_binding(mapping_text), input_binding(
        selection_text
    )
    mapped = load_json(mappings.relative_path(root, mapping_text))
    input_binding(mapped["baseline"]["path"])
    # Historical overlap can be stale. This slice rechecks exactly baseline_unseen;
    # it never upgrades or overwrites the preceding loader/mapping receipts.
    mappings.check_summary(root, mapped, False)
    baseline = load_json(mappings.relative_path(root, mapped["baseline"]["path"]))
    selection = load_json(mappings.relative_path(root, selection_text))
    if (
        selection_binding["sha256"] != baseline["selection"]["sha256"]
        or selection_text != baseline["selection"]["path"]
    ):
        raise ValueError("Ownership selection differs from mapped baseline")
    baseline_raw_path = mappings.relative_path(root, baseline["receipt"]["path"])
    baseline_raw_binding = input_binding(baseline["receipt"]["path"])
    if baseline_raw_binding["sha256"] != baseline["receipt"]["sha256"]:
        raise ValueError("Original installed receipt associations changed")
    original_receipts = {
        row["path"].removeprefix("repo:"): row["sha256"]
        for row in load_json(baseline_raw_path)["installed_receipts"]
    }
    selected = set(mapped["baseline_unseen"])
    if not selected or len(selected) > MAX_NODES:
        raise ValueError("Invalid newly mapped inventory")
    if len(selection["worker_receipts"]) > 8:
        raise ValueError("Installed receipt count exceeds budget")
    nodes, groups, installed = {}, {}, []
    total, notice_count, notice_bytes = 0, 0, 0

    def notice(key):
        nonlocal notice_count, notice_bytes
        value = observe(key, min(MAX_EVIDENCE, MAX_NOTICE_BYTES - notice_bytes))
        notice_count += 1
        notice_bytes += value["bytes"]
        if notice_count > MAX_NOTICES:
            raise ValueError("Ownership notice count exceeds budget")
        return value

    for key in sorted(selected):
        value = observe(
            key, min(mappings.MAX_FILE_BYTES, mappings.MAX_TOTAL_BYTES - total)
        )
        expected = mapped["nodes"][key]
        if any(value[field] != expected[field] for field in ("sha256", "bytes")):
            raise ValueError("Newly mapped ELF bytes changed")
        total += value["bytes"]
        nodes[key] = {field: value[field] for field in ("sha256", "bytes")}

    for entry in selection["worker_receipts"]:
        prefix = entry["prefix"]
        safe_relative(prefix)
        bound = input_binding(entry["receipt"])
        if original_receipts.get(entry["receipt"]) != bound["sha256"]:
            raise ValueError(
                "Selected installed inventory differs from original binding"
            )
        installed.append({"prefix": prefix, **bound})
        environment = receipt_packages(
            load_json(mappings.relative_path(root, entry["receipt"]))
        )
        if len(environment["packages"]) > MAX_GROUPS:
            raise ValueError("Installed group count exceeds budget")
        for package in environment["packages"]:
            files = package["files"]
            if len(files) > 100000:
                raise ValueError("Installed file count exceeds budget")
            matches = []
            for item in files:
                safe_relative(item["path"])
                key = "repo:" + prefix + "/" + item["path"]
                if key in selected:
                    if item["record_check"] != "MATCH" or any(
                        item[field] != nodes[key][field]
                        for field in ("sha256", "bytes")
                    ):
                        raise ValueError(
                            "Mapped ELF differs from installed RECORD inventory"
                        )
                    matches.append(key)
            if not matches:
                continue
            name, version = package["name"], package["version"]
            if not re.fullmatch(r"[a-z0-9][a-z0-9.-]{0,127}", name):
                raise ValueError("Invalid installed distribution name")
            group_key = "worker:" + prefix + ":" + name
            if group_key in groups:
                raise ValueError("Ambiguous installed distribution")
            group = {
                "kind": "installed-distribution",
                "prefix": prefix,
                "name": name,
                "version": version,
                "receipt": entry["receipt"],
                "gaps": list(GAPS),
                "notices": [],
            }
            for field, suffix in [("metadata", "/METADATA"), ("record", "/RECORD")]:
                candidates = [
                    item
                    for item in files
                    if item["path"].endswith(suffix)
                    and item["sha256"] == package[field + "_sha256"]
                ]
                if len(candidates) != 1:
                    raise ValueError(
                        "Ambiguous or missing distribution metadata/RECORD"
                    )
                item = candidates[0]
                value = observe("repo:" + prefix + "/" + item["path"], MAX_EVIDENCE)
                if any(value[key] != item[key] for key in ("sha256", "bytes")):
                    raise ValueError(
                        "Current metadata/RECORD differs from installed receipt"
                    )
                group[field] = value
            fields = BytesParser().parsebytes(
                read_file(mappings.node_path(root, group["metadata"]["path"]))
            )
            declared = re.sub(r"[-_.]+", "-", str(fields.get("Name", ""))).lower()
            if declared != name or fields.get("Version") != version:
                raise ValueError("Installed metadata name/version differs")
            for item in package["notices"]:
                safe_relative(item["path"])
                value = notice("repo:" + prefix + "/" + item["path"])
                if any(value[key] != item[key] for key in ("sha256", "bytes")):
                    raise ValueError("Current notice differs from installed receipt")
                group["notices"].append(value)
            if not group["notices"]:
                group["gaps"].append("SEPARATE_INSTALLED_NOTICE_ABSENT")
            groups[group_key] = group
            for key in matches:
                if "group" in nodes[key]:
                    raise ValueError("Ambiguous installed mapped-file ownership")
                nodes[key]["group"] = group_key

    package_tool = observe(
        "system:" + selection["package_tool"], mappings.MAX_FILE_BYTES
    )
    package_query = selection["package_tool"]
    initial = []

    def query(arguments, expected=(0,)):
        value = runner.run([package_query, *arguments], expected=expected)
        initial.append((list(arguments), expected, value["exit"], value["output"]))
        return value

    for key in sorted(selected):
        if key.startswith("repo:"):
            if "group" not in nodes[key]:
                raise ValueError("Newly mapped worker ELF is unattributed")
            continue
        owned = query(["-Qoq", "--", key.removeprefix("system:")], expected=(0, 1))
        if owned["exit"] != 0:
            raise ValueError("Newly mapped system ELF is unattributed")
        name = owned["output"].strip()
        if not re.fullmatch(r"[A-Za-z0-9@_.+-]{1,128}", name):
            raise ValueError("Ambiguous installed system owner")
        group_key = "system:" + name
        if group_key not in groups:
            group = package_fields(name, query(["-Qi", "--", name])["output"])
            paths = query(["-Qlq", "--", name])["output"].splitlines()
            group.update(notices=[], gaps=list(GAPS))
            for text in paths:
                path = PurePosixPath(text)
                if not path.is_relative_to("/usr/share/licenses"):
                    continue
                if ".." in path.parts or "\\" in text or "\0" in text:
                    raise ValueError("Unsafe system notice path")
                selected_path = mappings.node_path(root, "system:" + text.rstrip("/"))
                if selected_path.is_dir():
                    continue
                group["notices"].append(notice("system:" + text))
            if not group["notices"]:
                group["gaps"].append("SEPARATE_INSTALLED_NOTICE_ABSENT")
            groups[group_key] = group
        nodes[key]["group"] = group_key
        if len(groups) > MAX_GROUPS:
            raise ValueError("Ownership group count exceeds budget")
    # Recheck package database associations as well as backing-file identities.
    for arguments, expected, exit_code, output in initial:
        value = runner.run([package_query, *arguments], expected=expected)
        if (value["exit"], value["output"]) != (exit_code, output):
            raise ValueError("Installed package association changed during capture")
    for key, before in watched.items():
        if snapshot(mappings.node_path(root, key)) != before:
            raise ValueError("Ownership input changed before publication")
    return {
        "schema_version": 1,
        "status": "IMPLEMENTED_UNVERIFIED",
        "scope": SCOPE,
        "rights_qualified": False,
        "sources": source_bindings,
        "mapping": mapping_binding,
        "selection": selection_binding,
        "installed_receipts": installed,
        "package_tool": package_tool,
        "nodes": nodes,
        "groups": groups,
        "listed_bytes": total,
        "commands": runner.receipts,
    }


def check_summary(root, summary, local=False, runner=None):
    if (
        summary["schema_version"] != 1
        or summary["status"] != "IMPLEMENTED_UNVERIFIED"
        or summary["scope"] != SCOPE
        or summary["rights_qualified"] is not False
    ):
        raise ValueError("Unsupported owner summary or rights claim")
    if [entry["path"] for entry in summary["sources"]] != SOURCES:
        raise ValueError("Incomplete owner auditor source binding")
    for value in [*summary["sources"], summary["mapping"], summary["selection"]]:
        verify_reference(root, value, True)
    mapped = load_json(mappings.relative_path(root, summary["mapping"]["path"]))
    mappings.check_summary(root, mapped, False)
    selection = load_json(mappings.relative_path(root, summary["selection"]["path"]))
    expected_receipts = {
        entry["prefix"]: entry["receipt"] for entry in selection["worker_receipts"]
    }
    actual_receipts = {
        entry["prefix"]: entry["path"] for entry in summary["installed_receipts"]
    }
    if actual_receipts != expected_receipts or len(actual_receipts) != len(
        summary["installed_receipts"]
    ):
        raise ValueError("Installed owner receipt inventory is incomplete")
    validate_file_binding(summary["package_tool"])
    nodes, groups = summary["nodes"], summary["groups"]
    if (
        set(nodes) != set(mapped["baseline_unseen"])
        or not 0 < len(nodes) <= MAX_NODES
        or not 0 < len(groups) <= MAX_GROUPS
    ):
        raise ValueError("Owner node/group inventory is incomplete")
    if {node["group"] for node in nodes.values()} != set(groups):
        raise ValueError("Owner groups are unreferenced or missing")
    total = 0
    for key, node in nodes.items():
        if any(
            node[field] != mapped["nodes"][key][field] for field in ("sha256", "bytes")
        ):
            raise ValueError("Owner fingerprint differs from mapped bytes")
        kind = groups[node["group"]]["kind"]
        if kind != (
            "installed-distribution" if key.startswith("repo:") else "system-package"
        ):
            raise ValueError("Owner kind differs from mapping namespace")
        if kind == "installed-distribution" and not key.startswith(
            "repo:" + groups[node["group"]]["prefix"] + "/"
        ):
            raise ValueError("Owner prefix differs from mapped file")
        total += node["bytes"]
    notices, notice_bytes = 0, 0
    for name, group in groups.items():
        if group["gaps"] != GAPS + (
            [] if group["notices"] else ["SEPARATE_INSTALLED_NOTICE_ABSENT"]
        ):
            raise ValueError("Source/terms gaps are missing")
        if group["kind"] == "installed-distribution":
            safe_relative(group["prefix"])
            if group["receipt"] != expected_receipts.get(group["prefix"]):
                raise ValueError("Owner receipt differs from selected prefix")
            if name != "worker:" + group["prefix"] + ":" + group["name"]:
                raise ValueError("Installed owner identifier differs")
            for value in (group["metadata"], group["record"]):
                validate_file_binding(value)
                if not value["path"].startswith("repo:" + group["prefix"] + "/"):
                    raise ValueError("Metadata escapes distribution prefix")
        elif name != "system:" + group["name"]:
            raise ValueError("System owner identifier differs")
        for value in group["notices"]:
            validate_file_binding(value)
            # Namespace/path lexical checks without touching unavailable host files.
            path = value["path"]
            if group["kind"] == "installed-distribution":
                if not path.startswith("repo:" + group["prefix"] + "/"):
                    raise ValueError("Notice escapes distribution prefix")
                safe_relative(path[5:])
            elif not PurePosixPath(path.removeprefix("system:")).is_relative_to(
                "/usr/share/licenses"
            ):
                raise ValueError("Invalid system notice namespace")
            notices += 1
            notice_bytes += value["bytes"]
    if (
        total != summary["listed_bytes"]
        or notices > MAX_NOTICES
        or notice_bytes > MAX_NOTICE_BYTES
    ):
        raise ValueError("Owner byte/notice totals exceed budget or differ")
    for value in summary["installed_receipts"]:
        verify_reference(
            root, {key: value[key] for key in ("path", "sha256", "bytes")}, local
        )
    verify_reference(root, summary["receipt"], local)
    if local:
        raw = load_json(mappings.relative_path(root, summary["receipt"]["path"]))
        if public_fields(raw) != public_fields(summary):
            raise ValueError("Raw owner receipt differs from public summary")
        current = capture(
            root, summary["mapping"]["path"], summary["selection"]["path"], runner
        )
        if public_fields(current) != public_fields(summary):
            raise ValueError("Current installed ownership/notices differ")
    return {
        "nodes": len(nodes),
        "groups": len(groups),
        "notices": notices,
        "local_evidence_checked": local,
        "rights_qualified": False,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--mapping", default="third_party/backend-bindings-20261015.json"
    )
    parser.add_argument(
        "--selection", default="third_party/runtime-selection-20261014.json"
    )
    operation = parser.add_mutually_exclusive_group(required=True)
    operation.add_argument("--output", type=Path)
    operation.add_argument("--summary", type=Path)
    parser.add_argument("--local-evidence", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    try:
        if args.output:
            if sys.platform != "linux" or args.local_evidence:
                raise ValueError(
                    "Owner capture requires Linux without check-mode options"
                )
            output = args.output.resolve()
            if (
                not output.is_relative_to(root / "artifacts")
                or args.output.exists()
                or args.output.is_symlink()
            ):
                raise ValueError("Select a new owner receipt inside ignored artifacts/")
            report = capture(root, args.mapping, args.selection)
            publish_receipt(output, report)
            result = {
                "nodes": len(report["nodes"]),
                "groups": len(report["groups"]),
                "rights_qualified": False,
            }
        else:
            result = check_summary(root, load_json(args.summary), args.local_evidence)
        print(json.dumps(result, sort_keys=True))
        return 0
    except (OSError, ValueError, KeyError, TypeError) as error:
        print("Binding owner audit failed: " + str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
