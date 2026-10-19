#!/usr/bin/env python3
"""Check scoped Linux import/viewer mappings; never infer source or license closure."""

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import sys

from probe_backend_bindings import (
    MAX_FILES,
    MAX_FILE_BYTES,
    MAX_MAP_BYTES,
    MAX_TOTAL_BYTES,
    PROFILES,
)

__all__ = [
    "MAX_PROFILES",
    "SCOPE",
    "binding",
    "check_binding",
    "check_summary",
    "fingerprint",
    "main",
    "merge_nodes",
    "node_path",
    "project_receipt",
    "read_json",
    "relative_path",
    "valid_fingerprint",
]

SCOPE = "selected imports and empty viewer mappings; backing ELF bytes, not relocated memory or all lazy paths"
MAX_PROFILES = 8


def relative_path(root, text):
    if not isinstance(text, str) or "\\" in text or "\0" in text:
        raise ValueError("Invalid binding path")
    path = PurePosixPath(text)
    if (
        path.is_absolute()
        or not path.parts
        or ".." in path.parts
        or path.as_posix() != text
    ):
        raise ValueError("Invalid relative binding path")
    resolved = (root / text).resolve(strict=True)
    if not resolved.is_relative_to(root):
        raise ValueError("Binding escapes repository")
    return resolved


def node_path(root, text):
    if text.startswith("repo:"):
        return relative_path(root, text[5:])
    if not text.startswith("system:"):
        raise ValueError("Invalid mapped-file namespace")
    path = PurePosixPath(text[7:])
    if (
        not path.is_absolute()
        or ".." in path.parts
        or path.as_posix() != text[7:]
        or not (path.is_relative_to("/usr") or path.is_relative_to("/opt/cuda"))
    ):
        raise ValueError("Invalid system mapped-file path")
    resolved = Path(path).resolve(strict=True)
    if not (resolved.is_relative_to("/usr") or resolved.is_relative_to("/opt/cuda")):
        raise ValueError("System mapping escapes selected roots")
    return resolved


def fingerprint(path, maximum):
    before = path.stat()
    if not path.is_file() or before.st_size > maximum:
        raise ValueError("Binding file exceeds size/type budget")
    digest = hashlib.sha256()
    count = 0
    with path.open("rb") as stream:
        while block := stream.read(1024**2):
            count += len(block)
            if count > maximum:
                raise ValueError("Binding file grew past budget")
            digest.update(block)
    after = path.stat()
    fields = lambda info: (
        info.st_dev,
        info.st_ino,
        info.st_size,
        info.st_mtime_ns,
        info.st_ctime_ns,
    )
    if count != before.st_size or fields(before) != fields(after):
        raise ValueError("Binding file changed while reading")
    return {"sha256": digest.hexdigest(), "bytes": count}


def read_json(path):
    with path.open("rb") as stream:
        data = stream.read(MAX_MAP_BYTES + 1)
    if len(data) > MAX_MAP_BYTES:
        raise ValueError("Binding JSON exceeds byte budget")

    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError("Duplicate binding JSON key")
            result[key] = value
        return result

    return json.loads(data, object_pairs_hook=pairs)


def valid_fingerprint(value, maximum):
    if (
        not re.fullmatch(r"[0-9a-f]{64}", value["sha256"])
        or type(value["bytes"]) is not int
        or not 0 < value["bytes"] <= maximum
    ):
        raise ValueError("Invalid binding fingerprint")


def binding(root, text):
    return {"path": text, **fingerprint(relative_path(root, text), MAX_MAP_BYTES)}


def check_binding(root, value, local):
    valid_fingerprint(value, MAX_MAP_BYTES)
    # Lexical validation also applies when private evidence is unavailable.
    text = value["path"]
    path = PurePosixPath(text)
    if (
        not text
        or path.is_absolute()
        or ".." in path.parts
        or path.as_posix() != text
        or "\\" in text
        or "\0" in text
    ):
        raise ValueError("Invalid receipt binding path")
    if local and fingerprint(relative_path(root, text), MAX_MAP_BYTES) != {
        k: value[k] for k in ("sha256", "bytes")
    }:
        raise ValueError("Bound source/receipt bytes differ")


def project_receipt(raw, kind):
    if kind == "imports":
        if (
            raw["schema_version"] != 1
            or raw["status"] != "IMPLEMENTED_UNVERIFIED"
            or raw["error"] is not None
            or raw["guard_denials"]
            or raw["rights_qualified"] is not False
            or raw["profile"] not in PROFILES
            or set(raw["modules"]) != set(PROFILES[raw["profile"]])
        ):
            raise ValueError("Import receipt is failed, guarded or incomplete")
        info = {
            "profile": raw["profile"],
            "versions": {
                key: value["version"] for key, value in raw["modules"].items()
            },
            "capabilities": raw["capabilities"],
        }
        observation = raw["observation"]
    elif kind == "viewer":
        if (
            raw["exit"] != 0
            or raw["mapped"] is None
            or raw["mapped"]["first_frame"] < 1
        ):
            raise ValueError("Viewer receipt lacks a successful rendered observation")
        info = {
            "binary_sha256": raw["binary_sha256"],
            "first_frame": raw["mapped"]["first_frame"],
        }
        observation = raw["mapped"]["observation"]
    else:
        raise ValueError("Unknown binding observation kind")
    nodes = {
        key: {
            field: value[field] for field in ("sha256", "bytes", "executable_mapping")
        }
        for key, value in observation["nodes"].items()
    }
    info.update(
        mapped_nodes=sorted(nodes),
        listed_bytes=observation["listed_bytes"],
        excluded=observation["excluded"],
    )
    return info, nodes


def merge_nodes(target, nodes):
    for key, value in nodes.items():
        if key in target:
            if any(target[key][field] != value[field] for field in ("sha256", "bytes")):
                raise ValueError("Mapped backing file differs between profiles")
            target[key]["executable_mapping"] |= value["executable_mapping"]
        else:
            target[key] = dict(value)


def check_summary(root, summary, local=False):
    if (
        summary["schema_version"] != 1
        or summary["status"] != "IMPLEMENTED_UNVERIFIED"
        or summary["rights_qualified"] is not False
        or summary["scope"] != SCOPE
    ):
        raise ValueError("Unsupported mapping summary or qualification claim")
    if summary["probe"]["path"] != "tools/probe_backend_bindings.py":
        raise ValueError("Wrong import observer")
    check_binding(root, summary["probe"], True)
    check_binding(root, summary["baseline"], True)
    baseline = read_json(relative_path(root, summary["baseline"]["path"]))
    nodes, profiles = summary["nodes"], summary["profiles"]
    if not 0 < len(profiles) <= MAX_PROFILES or not 0 < len(nodes) <= MAX_FILES:
        raise ValueError("Mapping count budget exceeded")
    seen, projected = set(), {}
    for name, row in profiles.items():
        if not re.fullmatch(r"[a-z0-9-]{1,64}", name) or row["context"] not in {
            "native",
            "sandbox",
        }:
            raise ValueError("Invalid observed profile/context")
        check_binding(root, row["receipt"], local)
        if row["kind"] not in {"imports", "viewer"}:
            raise ValueError("Unknown observation kind")
        info = row["observation"]
        selected = info["mapped_nodes"]
        if (
            selected != sorted(set(selected))
            or not selected
            or not set(selected) <= set(nodes)
        ):
            raise ValueError("Incomplete or duplicate mapped profile inventory")
        seen.update(selected)
        if info["listed_bytes"] != sum(nodes[key]["bytes"] for key in selected):
            raise ValueError("Profile mapped-byte sum differs")
        if any(
            type(value) is not int or value < 0 for value in info["excluded"].values()
        ):
            raise ValueError("Invalid map exclusion counts")
        if local:
            raw = read_json(relative_path(root, row["receipt"]["path"]))
            actual, observed = project_receipt(raw, row["kind"])
            if actual != info:
                raise ValueError("Raw mapping profile differs from summary")
            if raw["observer_sha256"] != summary["probe"]["sha256"]:
                raise ValueError("Profile used a different mapping observer")
            merge_nodes(projected, observed)
    if seen != set(nodes) or (local and projected != nodes):
        raise ValueError("Raw mapping node inventory differs from summary")
    total = 0
    for key, value in nodes.items():
        valid_fingerprint(value, MAX_FILE_BYTES)
        if type(value["executable_mapping"]) is not bool:
            raise ValueError("Invalid executable mapping flag")
        # Validate namespaces without requiring private installed files by default.
        path = (
            key[5:]
            if key.startswith("repo:")
            else key[7:] if key.startswith("system:") else ""
        )
        pure = PurePosixPath(path)
        if (
            not path
            or ".." in pure.parts
            or pure.as_posix() != path
            or "\\" in path
            or "\0" in path
            or (key.startswith("repo:") and pure.is_absolute())
            or (
                key.startswith("system:")
                and not (
                    pure.is_relative_to("/usr") or pure.is_relative_to("/opt/cuda")
                )
            )
        ):
            raise ValueError("Invalid mapped node path")
        total += value["bytes"]
        if total > MAX_TOTAL_BYTES:
            raise ValueError("Aggregate mapped byte budget exceeded")
        if local and fingerprint(node_path(root, key), MAX_FILE_BYTES) != {
            k: value[k] for k in ("sha256", "bytes")
        }:
            raise ValueError("Current mapped backing bytes differ")
    unseen = sorted(set(nodes) - set(baseline["nodes"]))
    changed = sorted(
        key
        for key in set(nodes) & set(baseline["nodes"])
        if any(
            nodes[key][field] != baseline["nodes"][key][field]
            for field in ("sha256", "bytes")
        )
    )
    if (
        total != summary["listed_bytes"]
        or unseen != summary["baseline_unseen"]
        or changed != summary["baseline_changed"]
    ):
        raise ValueError("Mapping totals or baseline comparison differ")
    return {
        "profiles": len(profiles),
        "nodes": len(nodes),
        "baseline_unseen": len(unseen),
        "baseline_changed": len(changed),
        "local_evidence_checked": local,
        "rights_qualified": False,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--summary",
        type=Path,
        default=Path("third_party/backend-bindings-20261015.json"),
    )
    parser.add_argument("--local-evidence", action="store_true")
    args = parser.parse_args()
    try:
        print(
            json.dumps(
                check_summary(
                    Path(__file__).resolve().parents[1],
                    read_json(args.summary),
                    args.local_evidence,
                )
            )
        )
        return 0
    except (OSError, ValueError, KeyError, TypeError) as error:
        print("Backend binding check failed: " + str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
