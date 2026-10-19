#!/usr/bin/env python3
"""Observe selected backend imports and backing ELF bytes in an owned process.

Linux-only, offline diagnostics: no checkpoints, optimizer, reconstruction or
compiler subprocess. CUDA availability queries do not allocate training tensors.
The Python audit guard is a diagnostic policy, not a native-code security sandbox.
"""

import argparse
import hashlib
import importlib
import json
import os
from pathlib import Path
import re
import socket
import stat
import sys
import tempfile

MAX_MAP_BYTES = 8 * 1024**2
MAX_REGIONS = 16384
MAX_FILES = 512
MAX_FILE_BYTES = 4 * 1024**3
MAX_TOTAL_BYTES = 16 * 1024**3
PROFILES = {
    "training": [
        "torch",
        "numpy",
        "PIL.Image",
        "scipy.spatial",
        "pycolmap",
        "gsplat",
        "gsplat.csrc",
        "triton",
        "triton._C.libtriton",
    ],
    "poses": ["numpy", "PIL.Image", "pycolmap"],
    "metrics-cpu": ["torch", "torchvision", "torchmetrics"],
}


def parse_maps(data):
    if len(data) > MAX_MAP_BYTES:
        raise ValueError("Process map exceeds byte budget")
    regions = data.decode("utf-8").splitlines()
    if len(regions) > MAX_REGIONS:
        raise ValueError("Process map exceeds region budget")
    files = {}
    excluded = {"anonymous_or_virtual": 0, "deleted_or_ambiguous": 0}
    pattern = re.compile(
        r"([0-9a-f]+)-([0-9a-f]+) ([r-][w-][x-][ps]) ([0-9a-f]+) ([0-9a-f]+):([0-9a-f]+) ([0-9]+)(?: +(.*))?"
    )
    for line in regions:
        match = pattern.fullmatch(line)
        if not match:
            raise ValueError("Malformed process map region")
        begin, end, permissions, offset, major, minor, inode, path = match.groups()
        if int(begin, 16) >= int(end, 16):
            raise ValueError("Invalid process map address interval")
        if not path or path.startswith("[") or int(inode) == 0:
            excluded["anonymous_or_virtual"] += 1
            continue
        if path.endswith(" (deleted)") or "\\" in path:
            excluded["deleted_or_ambiguous"] += 1
            continue
        if not path.startswith("/") or len(path) > 4096 or "\0" in path:
            raise ValueError("Invalid mapped pathname")
        identity = (int(major, 16), int(minor, 16), int(inode))
        if path in files and files[path]["identity"] != identity:
            raise ValueError("Ambiguous mapped file identity")
        files.setdefault(path, {"identity": identity, "executable": False})
        files[path]["executable"] |= "x" in permissions
    if len(files) > MAX_FILES:
        raise ValueError("Process mapped-file count exceeds budget")
    return files, excluded


def file_identity(info):
    return (os.major(info.st_dev), os.minor(info.st_dev), info.st_ino)


def stable_identity(info):
    return (info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns, info.st_ctime_ns)


def fingerprint_mapping(path, identity, maximum):
    before = path.stat()
    if not stat.S_ISREG(before.st_mode):
        return None
    if file_identity(before) != tuple(identity):
        raise ValueError("Mapped inode/device differs from current backing file")
    with path.open("rb") as stream:
        opened = os.fstat(stream.fileno())
        if stable_identity(opened) != stable_identity(before):
            raise ValueError("Mapped backing file changed before open")
        if stream.read(4) != b"\x7fELF":
            return None
        if before.st_size > maximum:
            raise ValueError("Mapped ELF exceeds byte budget")
        stream.seek(0)
        sha256 = hashlib.sha256()
        count = 0
        while block := stream.read(1024**2):
            count += len(block)
            if count > maximum:
                raise ValueError("Mapped ELF grew past byte budget")
            sha256.update(block)
        after_open = os.fstat(stream.fileno())
    if (
        count != before.st_size
        or stable_identity(before) != stable_identity(after_open)
        or stable_identity(before) != stable_identity(path.stat())
    ):
        raise ValueError("Mapped backing file changed during fingerprint")
    return {
        "sha256": sha256.hexdigest(),
        "bytes": count,
        "file_state": list(stable_identity(before)),
    }


def maps_snapshot(pid):
    path = Path(f"/proc/{pid}/maps")
    with path.open("rb") as stream:
        data = stream.read(MAX_MAP_BYTES + 1)
    return parse_maps(data)


def collect_mapped_elf(pid, root):
    files, excluded = maps_snapshot(pid)
    nodes = {}
    observed = {}
    total = 0
    excluded.update(non_elf_or_device=0, outside_selected_roots=0)
    for text, mapping in sorted(files.items()):
        path = Path(text).resolve(strict=True)
        if path.is_relative_to(root):
            key = "repo:" + path.relative_to(root).as_posix()
        elif path.is_relative_to(Path("/usr")) or path.is_relative_to(
            Path("/opt/cuda")
        ):
            key = "system:" + path.as_posix()
        else:
            excluded["outside_selected_roots"] += 1
            continue
        value = fingerprint_mapping(
            path, mapping["identity"], min(MAX_FILE_BYTES, MAX_TOTAL_BYTES - total)
        )
        if value is None:
            excluded["non_elf_or_device"] += 1
            continue
        if key in nodes and nodes[key]["mapping_identity"] != list(mapping["identity"]):
            raise ValueError("Canonical mapped ELF has conflicting identities")
        if key not in nodes:
            total += value["bytes"]
        executable = mapping["executable"] or (
            key in nodes and nodes[key]["executable_mapping"]
        )
        value.update(
            mapping_identity=list(mapping["identity"]), executable_mapping=executable
        )
        nodes[key] = value
        observed[text] = mapping["identity"]
    later, _ = maps_snapshot(pid)
    for text, identity in observed.items():
        if text not in later or later[text]["identity"] != identity:
            raise ValueError("Mapped ELF disappeared or changed before publication")
    for key, node in nodes.items():
        text = (
            key.removeprefix("repo:")
            if key.startswith("repo:")
            else key.removeprefix("system:")
        )
        path = root / text if key.startswith("repo:") else Path(text)
        if list(stable_identity(path.stat())) != node["file_state"]:
            raise ValueError("Mapped ELF backing bytes changed before publication")
    return {
        "nodes": nodes,
        "listed_bytes": total,
        "excluded": excluded,
        "scope": "observed mapped backing ELF files; relocated/private memory and excluded regions are not fingerprinted",
    }


def diagnostic_guard(work):
    work = work.resolve(strict=True)
    denied = []

    def reject(event):
        if len(denied) < 128:
            denied.append(event)
        raise RuntimeError("Runtime import probe blocks " + event)

    def hook(event, args):
        if event in {
            "subprocess.Popen",
            "os.system",
            "os.posix_spawn",
            "os.exec",
            "pickle.find_class",
            "socket.getaddrinfo",
        }:
            reject(event)
        if event == "socket.connect" and args[0].family in {
            socket.AF_INET,
            socket.AF_INET6,
        }:
            reject(event)
        if event == "open" and isinstance(args[0], (str, bytes, os.PathLike)):
            path = Path(os.fsdecode(args[0]))
            if path.suffix.lower() in {
                ".pt",
                ".pth",
                ".pkl",
                ".pickle",
                ".safetensors",
                ".mp4",
                ".mov",
            }:
                reject("checkpoint_or_video_open")
            mode, flags = args[1:3]
            writing = (
                isinstance(mode, str) and any(c in mode for c in "wax+")
            ) or bool(flags & (os.O_WRONLY | os.O_RDWR | os.O_CREAT | os.O_TRUNC))
            if writing and not path.resolve().is_relative_to(work):
                reject("write_outside_probe_work")

    return hook, denied


def run_imports(profile):
    if profile not in PROFILES:
        raise ValueError("Unknown backend import profile")
    modules = {}
    for name in PROFILES[profile]:
        module = importlib.import_module(name)
        modules[name] = {
            "version": getattr(module, "__version__", None),
            "file": getattr(module, "__file__", None),
        }
        if name == "torch":

            def blocked_load(*args, **kwargs):
                raise RuntimeError("Runtime import probe does not load checkpoints")

            module.load = blocked_load
    capabilities = {}
    if "torch" in modules:
        torch = sys.modules["torch"]
        capabilities = {
            "torch_cuda_build": torch.version.cuda,
            "cuda_available": bool(torch.cuda.is_available()),
            "cuda_device_count": int(torch.cuda.device_count()),
            "cuda_initialized": bool(torch.cuda.is_initialized()),
        }
    return modules, capabilities


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", required=True, choices=sorted(PROFILES))
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    source_sha256 = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    output = args.output.resolve()
    if (
        sys.platform != "linux"
        or not output.is_relative_to(root / "artifacts")
        or args.output.exists()
        or args.output.is_symlink()
    ):
        print(
            "Select a new Linux probe receipt inside ignored artifacts/",
            file=sys.stderr,
        )
        return 2
    output.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=".import-probe-", dir=output.parent)
    try:
        work = Path(os.environ["WE_BINDING_WORK"]).resolve(strict=True)
        if not work.is_relative_to(output.parent):
            raise ValueError("Probe work directory is outside selected output tree")
        hook, denied = diagnostic_guard(work)
        sys.addaudithook(hook)
        status, error = "IMPLEMENTED_UNVERIFIED", None
        try:
            modules, capabilities = run_imports(args.profile)
        except Exception as failure:
            status, error = "BLOCKED", str(failure)
            modules, capabilities = {}, {}
        observed = collect_mapped_elf(os.getpid(), root)
        if hashlib.sha256(Path(__file__).read_bytes()).hexdigest() != source_sha256:
            raise ValueError("Import observer changed before publication")
        report = {
            "schema_version": 1,
            "profile": args.profile,
            "status": status,
            "error": error,
            "modules": modules,
            "capabilities": capabilities,
            "guard_denials": denied,
            "observation": observed,
            "rights_qualified": False,
            "observer_sha256": source_sha256,
        }
        data = (
            json.dumps(report, sort_keys=True, indent=2, allow_nan=False).encode()
            + b"\n"
        )
        if len(data) > MAX_MAP_BYTES:
            raise ValueError("Import receipt exceeds byte budget")
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        # Publication is a deliberately selected diagnostic write, after imports.
        os.link(temporary, output)
        print(
            json.dumps(
                {
                    "profile": args.profile,
                    "status": status,
                    "nodes": len(observed["nodes"]),
                    "capabilities": capabilities,
                    "error": error,
                }
            ),
            flush=True,
        )
        return 3 if status == "BLOCKED" or denied else 0
    except (OSError, ValueError, KeyError, TypeError, RuntimeError) as failure:
        print("Backend binding probe failed: " + str(failure), file=sys.stderr)
        return 2
    finally:
        try:
            os.close(descriptor)
        except OSError:
            pass  # The owned fd may already have been closed by fdopen's scope.
        Path(temporary).unlink(missing_ok=True)


if __name__ == "__main__":
    raise SystemExit(main())
