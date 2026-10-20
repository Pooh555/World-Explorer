#!/usr/bin/env python3
"""Own a resumable, retained-cache M3 partial-geometry job on Linux.

This development slice adopts a pinned native visibility cache. It does not
solve depth, publish a scene, establish metric scale, or enable walking.
"""

import argparse
import fcntl
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "workers" / "fusion"))
sys.path.insert(0, str(ROOT / "workers" / "dense"))

from admitted_visibility import iter_admitted_sequence, load_admitted_cache
from cached_sources import checked_path
from contract import load_request
from pilot import digest, parse, publish, read_pinned

__all__ = [
    "MAX_ASSET",
    "MAX_DOCUMENT",
    "ROOT",
    "STOPPED",
    "WORKER",
    "cancelled",
    "check_overlap",
    "checked_digest",
    "checked_stage",
    "execute",
    "main",
    "run_worker",
    "stop",
]

MAX_DOCUMENT = 1024**2
MAX_ASSET = 256 * 1024**2
WORKER = ROOT / "workers" / "fusion" / "run.py"
STOPPED = False


def stop(*_):
    global STOPPED
    STOPPED = True


def cancelled():
    return STOPPED


def checked_digest(value):
    if (
        not isinstance(value, str)
        or len(value) != 64
        or any(c not in "0123456789abcdef" for c in value)
    ):
        raise ValueError("Invalid lowercase SHA-256 pin")
    return value


def checked_stage(stage, owner):
    if stage.is_symlink() or not stage.is_dir():
        raise ValueError("Missing or linked geometry stage")
    entries = list(stage.iterdir())
    if any(entry.is_symlink() or not entry.is_file() for entry in entries):
        raise ValueError("Linked or non-file geometry stage entry")
    names = {entry.name for entry in entries}
    if names != {
        "manifest.json",
        "result.pending.json",
        "observed-voxels.npz",
        "mesh.npz",
    }:
        raise ValueError("Geometry stage inventory differs or is incomplete")
    manifest_path = checked_path(stage, "manifest.json")
    with manifest_path.open("rb") as stream:
        data = stream.read(MAX_DOCUMENT + 1)
    if len(data) > MAX_DOCUMENT:
        raise ValueError("Oversized geometry stage manifest")
    manifest = parse(data)
    if (
        type(manifest) is not dict
        or type(manifest.get("files")) is not dict
        or manifest.get("schema_version") != 1
        or manifest.get("kind") != "PARTIAL_OBSERVED_TSDF_V1"
        or manifest.get("walking_enabled") is not False
        or any(
            manifest.get(key) != owner[key]
            for key in ("request_sha256", "receipt_sha256", "recipe_sha256")
        )
        or manifest.get("pose_asset_sha256") != owner["pose_sha256"]
        or set(manifest.get("files", {})) != {"observed-voxels.npz", "mesh.npz"}
    ):
        raise ValueError("Geometry stage dependency or scope differs")
    for name, record in manifest["files"].items():
        if (
            type(record) is not dict
            or set(record) != {"bytes", "sha256"}
            or type(record["bytes"]) is not int
            or not 0 <= record["bytes"] <= MAX_ASSET
        ):
            raise ValueError("Invalid geometry stage asset record")
        asset = checked_path(stage, name)
        if asset.stat().st_size != record["bytes"]:
            raise ValueError("Geometry stage asset size differs")
        read_pinned(asset, checked_digest(record["sha256"]), record["bytes"])
    return digest(data)


def run_worker(request, request_sha, stage, parent_supervised=False):
    command = [sys.executable, str(WORKER), str(request), request_sha, str(stage)]
    # A native owner supervises the entire process group. Do not let the solver
    # escape its TERM/KILL boundary; standalone use still owns a private group.
    child = subprocess.Popen(command, start_new_session=not parent_supervised)
    while child.poll() is None and not cancelled():
        time.sleep(0.05)
    if cancelled() and child.poll() is None:
        if parent_supervised:
            child.terminate()
        else:
            os.killpg(child.pid, signal.SIGTERM)
        try:
            child.wait(timeout=3)
        except subprocess.TimeoutExpired:
            if parent_supervised:
                child.kill()
            else:
                os.killpg(child.pid, signal.SIGKILL)
            child.wait()
    else:
        child.wait()
    if cancelled():
        raise InterruptedError("Geometry job cancelled; partial attempt retained")
    if child.returncode:
        raise RuntimeError(
            f"Fusion worker exited {child.returncode}; partial attempt retained"
        )


def execute(args):
    if sys.platform != "linux":
        raise ValueError("Owned fusion pilot currently requires Linux")
    request = Path(args.request).resolve(strict=True)
    pose = Path(args.pose).resolve(strict=True)
    job = Path(args.job).absolute()
    request_data, recipe, cache, _ = load_request(
        request, checked_digest(args.request_sha256)
    )
    pose_sha = checked_digest(args.pose_sha256)
    cache = cache.resolve(strict=True)
    inputs = [
        request,
        pose,
        cache,
        (request.parent / request_data["recipe"]).resolve(strict=True),
        WORKER,
        Path(__file__).resolve(strict=True),
    ]
    check_overlap(job, inputs)
    # The full admitted reader hashes and validates every retained source plane.
    if recipe["kind"] == "OBSERVED_TSDF_CPU_SEQUENCE_V1":
        receipt = parse(
            read_pinned(
                cache / "manifest.json", request_data["receipt_sha256"], MAX_DOCUMENT
            )
        )
        for views, _ in iter_admitted_sequence(
            cache, request_data["receipt_sha256"], cancelled
        ):
            del views
    else:
        _, receipt = load_admitted_cache(
            cache, request_data["receipt_sha256"], cancelled
        )
    if receipt["pose_asset_sha256"] != pose_sha:
        raise ValueError("Current pose pin differs from retained visibility cache")
    read_pinned(pose, pose_sha, MAX_ASSET)
    if cancelled():
        raise InterruptedError("Geometry admission cancelled")
    owner = {
        "schema_version": 1,
        "kind": "RETAINED_OBSERVED_GEOMETRY_JOB_V1",
        "request_sha256": args.request_sha256,
        "receipt_sha256": request_data["receipt_sha256"],
        "recipe_sha256": request_data["recipe_sha256"],
        "pose_sha256": pose_sha,
        "request": str(request),
        "cache": str(cache),
        "pose": str(pose),
        "python": str(Path(sys.executable).resolve(strict=True)),
        "worker": str(WORKER),
        "worker_sha256": digest(WORKER.read_bytes()),
        "coordinator_sha256": digest(Path(__file__).read_bytes()),
    }
    owner_bytes = (
        json.dumps(owner, sort_keys=True, separators=(",", ":")) + "\n"
    ).encode()
    owner_sha = digest(owner_bytes)
    if job.exists() or job.is_symlink():
        if job.is_symlink() or not job.is_dir():
            raise ValueError("Linked or invalid geometry job directory")
        existing = {entry.name for entry in job.iterdir()}
        if (
            not existing <= {"job.lock", "owner.json", "stages", "manifest.json"}
            or "owner.json" not in existing
        ):
            raise ValueError("Unowned or occupied geometry job directory")
        if read_pinned(job / "owner.json", owner_sha, MAX_DOCUMENT) != owner_bytes:
            raise ValueError("Geometry job owner differs")
    job.mkdir(parents=True, exist_ok=True)
    if job.is_symlink() or not job.is_dir():
        raise ValueError("Linked or invalid geometry job directory")
    if (job / "job.lock").is_symlink():
        raise ValueError("Linked geometry job lock")
    with (job / "job.lock").open("a+b") as lock:
        fcntl.flock(lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        names = {entry.name for entry in job.iterdir()}
        if not names <= {"job.lock", "owner.json", "stages", "manifest.json"}:
            raise ValueError("Unknown geometry job entry")
        owner_path = job / "owner.json"
        if owner_path.exists() or owner_path.is_symlink():
            if read_pinned(owner_path, owner_sha, MAX_DOCUMENT) != owner_bytes:
                raise ValueError("Geometry job owner differs")
        else:
            if names != {"job.lock"}:
                raise ValueError("Unowned geometry job directory is occupied")
            publish(owner_path, owner_bytes)
        stages = job / "stages"
        stages.mkdir(exist_ok=True)
        if stages.is_symlink() or not stages.is_dir():
            raise ValueError("Invalid geometry stage directory")
        prefix = "fusion-" + owner_sha
        stage_names = {entry.name for entry in stages.iterdir()}
        if any(
            not name.startswith(prefix + "-attempt-")
            or not name[len(prefix + "-attempt-") :].isdigit()
            for name in stage_names
        ):
            raise ValueError("Unknown geometry stage entry")
        completed = []
        for name in sorted(stage_names):
            stage = stages / name
            if (stage / "manifest.json").exists() or (
                stage / "manifest.json"
            ).is_symlink():
                completed.append((stage, checked_stage(stage, owner)))
        if len(completed) > 1:
            raise ValueError("Multiple terminal geometry stages")
        if completed:
            stage, stage_sha = completed[0]
        else:
            index = 0
            while f"{prefix}-attempt-{index}" in stage_names:
                index += 1
            stage = stages / f"{prefix}-attempt-{index}"
            if cancelled():
                raise InterruptedError("Geometry job cancelled before worker start")
            run_worker(request, args.request_sha256, stage, args.parent_supervised)
            stage_sha = checked_stage(stage, owner)
        terminal = {
            "schema_version": 1,
            "kind": "OWNED_PARTIAL_OBSERVED_GEOMETRY_V1",
            "owner_sha256": owner_sha,
            "stage": stage.name,
            "stage_manifest_sha256": stage_sha,
            "scope": "PARTIAL_OBSERVED_GEOMETRY_NO_Q5_N2_OR_WALK",
        }
        terminal_bytes = (
            json.dumps(terminal, sort_keys=True, separators=(",", ":")) + "\n"
        ).encode()
        terminal_path = job / "manifest.json"
        if terminal_path.exists() or terminal_path.is_symlink():
            if (
                read_pinned(terminal_path, digest(terminal_bytes), MAX_DOCUMENT)
                != terminal_bytes
            ):
                raise ValueError("Geometry job terminal receipt differs")
        else:
            if cancelled():
                raise InterruptedError("Geometry job cancelled before publication")
            publish(terminal_path, terminal_bytes)
        print(json.dumps(terminal, sort_keys=True))


def check_overlap(output, inputs):
    target = output.resolve(strict=False)
    for source in inputs:
        source = source.resolve(strict=True)
        if (
            target == source
            or target.is_relative_to(source)
            or source.is_relative_to(target)
        ):
            raise ValueError("Geometry job overlaps an input")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("request")
    parser.add_argument("request_sha256")
    parser.add_argument("pose")
    parser.add_argument("pose_sha256")
    parser.add_argument("job")
    parser.add_argument("--parent-supervised", action="store_true")
    args = parser.parse_args()
    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)
    try:
        execute(args)
        return 0
    except InterruptedError as error:
        print(error, file=sys.stderr)
        return 130
    except (ValueError, OSError, KeyError, TypeError, RuntimeError) as error:
        print(error, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
