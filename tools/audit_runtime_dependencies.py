#!/usr/bin/env python3
"""Capture selected Linux ELF loader dependencies and ownership offline.

This invokes the selected glibc loader's --list mode, never application entry
points or Python backend imports. Extension library paths are explicit inspection
contexts, not observations of an actual Python import. Lazy plugins, GPU execution,
source archives and redistribution rights remain unqualified.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re
import selectors
import subprocess
import sys
import time

from audit_worker_environment import file_digest, publish_receipt
from check_dependency_manifest import load_json, read_file, receipt_packages

MAX_ROOTS = 32
MAX_NODES = 512
MAX_COMMANDS = 2048
MAX_OUTPUT = 2 * 1024**2
MAX_TOTAL_BYTES = 16 * 1024**3
MAX_NOTICES = 256
MAX_NOTICE_BYTES = 16 * 1024**2
ENVIRONMENT = {"PATH": "/usr/bin:/bin", "LC_ALL": "C"}


def label(path, root):
    path = path.resolve(strict=True)
    if path.is_relative_to(root):
        return "repo:" + path.relative_to(root).as_posix()
    if path.is_relative_to(Path("/usr")) or path.is_relative_to(Path("/opt/cuda")):
        return "system:" + path.as_posix()
    raise ValueError("Runtime input is outside selected repository/system roots")


def selected_path(text, root, directory=False):
    if not isinstance(text, str) or not text or len(text) > 4096 or any(c in text for c in "\n\r\0"):
        raise ValueError("Invalid selected runtime path")
    path = Path(text)
    path = (path if path.is_absolute() else root / path).resolve(strict=True)
    label(path, root)
    if directory:
        if not path.is_dir() or ":" in str(path):
            raise ValueError("Library path is not a directory or contains a separator")
    elif not path.is_file():
        raise ValueError("Runtime input is not regular")
    return path


def parse_elf(text):
    if "Class:" not in text or not re.search(r"Class:\s+ELF64", text) or not re.search(r"Machine:\s+Advanced Micro Devices X86-64", text):
        raise ValueError("Selected audit supports ELF64 x86-64 only")
    if not re.search(r"Data:\s+2's complement, little endian", text):
        raise ValueError("Unsupported ELF byte order")
    values = {}
    for tag, value in re.findall(r"\((NEEDED|RPATH|RUNPATH|SONAME)\).*?\[([^\]\n]*)\]", text):
        if not value or len(value) > 4096 or "\0" in value:
            raise ValueError("Invalid ELF dependency value")
        values.setdefault(tag.lower(), []).append(value)
    if len(values.get("needed", [])) > MAX_NODES or any(len(values.get(tag, [])) > 1 for tag in ("rpath", "runpath", "soname")):
        raise ValueError("Over-budget or ambiguous ELF metadata")
    interpreter = re.findall(r"\[Requesting program interpreter: ([^\]\n]+)\]", text)
    if len(interpreter) > 1:
        raise ValueError("Ambiguous ELF interpreter")
    return {"needed": values.get("needed", []), "rpath": values.get("rpath", []),
            "runpath": values.get("runpath", []), "soname": values.get("soname", []),
            "interpreter": interpreter[0] if interpreter else None}


def parse_loader(text):
    resolved = {}
    virtual = []
    missing = []
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        if re.fullmatch(r"linux-vdso\.so\.\d+ \(0x[0-9a-f]+\)", line):
            virtual.append(line.split()[0])
            continue
        unavailable = re.fullmatch(r"(\S+) => not found", line)
        if unavailable:
            missing.append(unavailable[1])
            continue
        match = re.fullmatch(r"(?:(\S+) => )?(/.+?) \(0x[0-9a-f]+\)", line)
        if not match:
            raise ValueError("Unrecognized loader output")
        name, path = match.groups()
        name = name or Path(path).name
        if name in resolved and resolved[name] != path:
            raise ValueError("Ambiguous loader resolution")
        resolved[name] = path
    if len(resolved) > MAX_NODES or len(missing) > MAX_NODES:
        raise ValueError("Loader dependency count exceeds budget")
    return {"resolved": resolved, "missing": missing, "virtual": virtual}


def resolve_edges(root_key, resolved, nodes):
    aliases = dict(resolved)
    loaded = {root_key, *resolved.values()}
    for key in loaded:
        for name in nodes[key]["elf"]["soname"]:
            if name in aliases and aliases[name] != key:
                raise ValueError("Ambiguous loaded SONAME alias")
            aliases[name] = key
    edges = []
    unresolved = []
    for key in sorted(loaded):
        for needed in nodes[key]["elf"]["needed"]:
            target = aliases.get(needed)
            edges.append({"from": key, "needed": needed, "to": target})
            if target is None:
                unresolved.append(key + ":" + needed)
    return aliases, edges, unresolved


class commands:
    def __init__(self):
        self.receipts = []

    def run(self, argv, expected=(0,)):
        if len(self.receipts) >= MAX_COMMANDS:
            raise ValueError("Runtime command count exceeds budget")
        data = bytearray()
        deadline = time.monotonic() + 15
        with subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              env=ENVIRONMENT) as process:
            try:
                with selectors.DefaultSelector() as selector:
                    selector.register(process.stdout, selectors.EVENT_READ)
                    while selector.get_map():
                        remaining = deadline - time.monotonic()
                        if remaining <= 0:
                            raise subprocess.TimeoutExpired(argv, 15)
                        for key, _ in selector.select(remaining):
                            chunk = key.fileobj.read1(min(65536, MAX_OUTPUT - len(data) + 1))
                            if not chunk:
                                selector.unregister(key.fileobj)
                                continue
                            data.extend(chunk)
                            if len(data) > MAX_OUTPUT:
                                raise ValueError("Runtime command output exceeds budget")
                exit_code = process.wait(timeout=max(0, deadline - time.monotonic()))
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()
        receipt = {"argv": argv, "exit": exit_code, "output_sha256": hashlib.sha256(data).hexdigest(),
                   "output": data.decode("utf-8", errors="strict")}
        self.receipts.append(receipt)
        if exit_code not in expected:
            raise ValueError("Selected runtime inspection command failed")
        return receipt


def worker_owners(root, selections):
    owners = {}
    bindings = []
    for selection in selections:
        prefix = selected_path(selection["prefix"], root, directory=True)
        path = selected_path(selection["receipt"], root)
        data = load_json(path)
        environment = receipt_packages(data)
        binding = {"path": label(path, root), "sha256": file_digest(path)[0]}
        bindings.append(binding)
        for package in environment["packages"]:
            for item in package["files"]:
                # Only potential ELF entries enter this inventory. Never scan
                # unrelated caches, deserialize checkpoints or import packages.
                if ".so" not in Path(item["path"]).name:
                    continue
                selected = (prefix / item["path"]).resolve(strict=True)
                if not selected.is_relative_to(prefix):
                    raise ValueError("Worker ELF receipt escapes selected environment")
                row = {"kind": "installed-distribution", "name": package["name"], "version": package["version"],
                       "sha256": item["sha256"], "metadata_sha256": package["metadata_sha256"],
                       "record_sha256": package["record_sha256"]}
                if selected in owners and owners[selected] != row:
                    raise ValueError("Ambiguous installed ELF ownership")
                owners[selected] = row
    return owners, bindings


def capture(root, selection, runner=None):
    if selection.get("schema_version") != 1 or not 1 <= len(selection["roots"]) <= MAX_ROOTS:
        raise ValueError("Invalid runtime selection schema/root count")
    if len({entry["id"] for entry in selection["roots"]}) != len(selection["roots"]):
        raise ValueError("Duplicate runtime root ID")
    runner = runner or commands()
    tools = {name: selected_path(selection[name], root) for name in ("loader", "readelf", "package_tool")}
    tool_hashes = {name: file_digest(path)[0] for name, path in tools.items()}
    owners, bindings = worker_owners(root, selection.get("worker_receipts", []))
    nodes = {}
    roots = []
    packages = {}
    total = 0
    notice_count = 0
    notice_bytes = 0
    observations = {}
    interpreter_metadata = []

    def observe(path):
        info = path.stat()
        return (info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns, info.st_ctime_ns)

    def package_info(name):
        nonlocal notice_count, notice_bytes
        if name in packages:
            return
        text = runner.run([str(tools["package_tool"]), "-Qi", "--", name])["output"]
        fields = dict(re.findall(r"^(Name|Version|URL|Licenses)\s+: (.*)$", text, re.M))
        if fields.get("Name") != name or any(key not in fields for key in ("Version", "URL", "Licenses")):
            raise ValueError("Invalid selected system package metadata")
        paths = runner.run([str(tools["package_tool"]), "-Qlq", "--", name])["output"].splitlines()
        notices = []
        for text in paths:
            path = Path(text)
            if not path.is_relative_to(Path("/usr/share/licenses")) or not path.is_file():
                continue
            before = observe(path)
            data = read_file(path, MAX_NOTICE_BYTES - notice_bytes)
            notice_count += 1
            notice_bytes += len(data)
            if notice_count > MAX_NOTICES:
                raise ValueError("Runtime notices exceed count budget")
            notices.append({"path": label(path, root), "sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data)})
            if observe(path) != before:
                raise ValueError("Runtime notice changed during identity capture")
            observations[path.resolve(strict=True)] = before
        packages[name] = {"version": fields["Version"], "source_url": fields["URL"],
                          "declared_license": fields["Licenses"], "notices": notices,
                          "gaps": ["SYSTEM_SOURCE_AND_BINARY_ARCHIVES_UNAVAILABLE", "TRANSITIVE_RIGHTS_UNREVIEWED"] +
                          ([] if notices else ["SEPARATE_INSTALLED_NOTICE_ABSENT"])}

    def inspect(path):
        nonlocal total
        path = selected_path(str(path), root)
        key = label(path, root)
        if key in nodes:
            return key
        if len(nodes) >= MAX_NODES:
            raise ValueError("Runtime node count exceeds budget")
        with path.open("rb") as stream:
            if stream.read(4) != b"\x7fELF":
                raise ValueError("Selected dependency is not ELF")
        before = observe(path)
        sha256, size = file_digest(path, min(4 * 1024**3, MAX_TOTAL_BYTES - total))
        total += size
        metadata = parse_elf(runner.run([str(tools["readelf"]), "--wide", "--file-header", "--program-headers", "--dynamic", str(path)])["output"])
        owner = owners.get(path)
        if owner is not None:
            if owner["sha256"] != sha256:
                raise ValueError("Current installed ELF differs from bound RECORD")
        elif path.is_relative_to(root):
            owner = {"kind": "project-build" if path.is_relative_to(root / "build") else "local-runtime-unattributed"}
        else:
            observed = runner.run([str(tools["package_tool"]), "-Qoq", "--", str(path)], expected=(0, 1))
            if observed["exit"] == 0:
                name = observed["output"].strip()
                if not re.fullmatch(r"[A-Za-z0-9@_.+-]{1,128}", name):
                    raise ValueError("Ambiguous selected system ownership")
                package_info(name)
                owner = {"kind": "system-package", "name": name, "version": packages[name]["version"]}
            else:
                owner = {"kind": "system-runtime-unattributed"}
        nodes[key] = {"sha256": sha256, "bytes": size, "elf": metadata, "owner": owner}
        if observe(path) != before:
            raise ValueError("Runtime node changed during metadata inspection")
        observations[path] = before
        return key

    for entry in selection["roots"]:
        path = selected_path(entry["path"], root)
        root_key = inspect(path)
        if nodes[root_key]["sha256"] != entry["sha256"]:
            raise ValueError("Runtime root differs from explicit selection")
        libraries = [selected_path(text, root, directory=True) for text in entry.get("library_path", [])]
        argv = [str(tools["loader"]), "--list"]
        if libraries:
            argv += ["--library-path", ":".join(str(path) for path in libraries)]
        argv.append(str(path))
        observation = runner.run(argv, expected=(0, 127))
        row = {"id": entry["id"], "path": root_key, "scope": entry["scope"],
               "library_path": [label(path, root) for path in libraries], "loader_exit": observation["exit"],
               "resolved": {}, "edges": [], "unresolved": []}
        if observation["exit"] == 0:
            parsed = parse_loader(observation["output"])
            row["virtual"] = parsed["virtual"]
            for name, resolved in parsed["resolved"].items():
                row["resolved"][name] = inspect(Path(resolved))
            row["unresolved"] += parsed["missing"]
            row["aliases"], row["edges"], missing = resolve_edges(root_key, row["resolved"], nodes)
            row["unresolved"] += missing
        else:
            row["unresolved"].append("LOADER_CONTEXT_FAILED")
        roots.append(row)
        if "interpreter_metadata" in entry:
            metadata = {}
            for kind, text in entry["interpreter_metadata"].items():
                selected = selected_path(text, root)
                before = observe(selected)
                data = read_file(selected)
                metadata[kind] = {"path": label(selected, root), "sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data)}
                if observe(selected) != before:
                    raise ValueError("Interpreter metadata changed during identity capture")
                observations[selected] = before
                if kind == "pyvenv_cfg":
                    fields = dict(re.findall(r"^(home|implementation|version_info)\s*=\s*(.*)$", data.decode(), re.M))
                    if (fields.get("implementation") != "CPython" or not re.fullmatch(r"\d+\.\d+\.\d+", fields.get("version_info", ""))
                            or selected_path(fields["home"], root, directory=True) != path.parent):
                        raise ValueError("Interpreter metadata does not identify selected binary directory")
                    metadata["declared_version"] = fields["version_info"]
            interpreter_metadata.append({"root": entry["id"], "files": metadata,
                                         "scope": "local interpreter metadata/notice bytes; source archive and bundled terms unqualified"})
        print(json.dumps({"root": entry["id"], "loader_exit": observation["exit"], "nodes": len(nodes)}), flush=True)
    # Recheck identities of selected roots/tools/receipts before immutable publish.
    for entry in selection["roots"]:
        if file_digest(selected_path(entry["path"], root))[0] != entry["sha256"]:
            raise ValueError("Runtime root changed during audit")
    for name, path in tools.items():
        if file_digest(path)[0] != tool_hashes[name]:
            raise ValueError("Inspection tool changed during audit")
    for binding in bindings:
        path = root / binding["path"].removeprefix("repo:")
        if file_digest(path)[0] != binding["sha256"]:
            raise ValueError("Installed receipt changed during audit")
    for path, before in observations.items():
        if observe(path) != before:
            raise ValueError("Runtime node/notice changed before publication")
    return {"schema_version": 1, "status": "IMPLEMENTED_UNVERIFIED", "rights_qualified": False,
            "scope": "selected ELF startup/default and explicit extension inspection contexts; not lazy/plugin/runtime execution or source/rights closure",
            "environment": ENVIRONMENT, "tools": {name: {"path": label(path, root), "sha256": tool_hashes[name]} for name, path in tools.items()},
            "installed_receipts": bindings, "nodes": nodes, "roots": roots, "system_packages": packages,
            "interpreter_metadata": interpreter_metadata,
            "listed_bytes": total, "commands": runner.receipts,
            "remaining": ["DLOPEN_PLUGINS_AND_GPU_DRIVER_SELECTION_UNOBSERVED", "PYTHON_IMPORT_CONTEXT_UNOBSERVED",
                          "INTERPRETER_SOURCE_ARCHIVE_AND_NOTICES_UNQUALIFIED", "SOURCE_BUILD_GRAPH_AND_REDISTRIBUTION_RIGHTS_UNREVIEWED"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--selection", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--require-resolved", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    try:
        if sys.platform != "linux":
            raise ValueError("Runtime loader capture requires Linux")
        output = args.output.resolve()
        if not output.is_relative_to(root / "artifacts") or args.output.exists():
            raise ValueError("Select a new receipt inside ignored artifacts/")
        selection = load_json(args.selection)
        selection_sha256 = file_digest(args.selection)[0]
        auditor_sha256 = file_digest(Path(__file__))[0]
        report = capture(root, selection)
        if file_digest(args.selection)[0] != selection_sha256:
            raise ValueError("Runtime selection changed during audit")
        report["selection_sha256"] = selection_sha256
        if file_digest(Path(__file__))[0] != auditor_sha256:
            raise ValueError("Auditor source changed during capture")
        report["auditor_sha256"] = auditor_sha256
        publish_receipt(output, report)
        unresolved = sum(len(row["unresolved"]) for row in report["roots"])
        print(json.dumps({"roots": len(report["roots"]), "nodes": len(report["nodes"]), "unresolved": unresolved,
                          "rights_qualified": False, "receipt": str(output.relative_to(root))}))
        return 3 if args.require_resolved and unresolved else 0
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        print("Runtime dependency audit failed: " + str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
