#!/usr/bin/env python3
"""Own bounded resized CUDA depth attempts; no scene or walking qualification."""

import argparse
import fcntl
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import sysconfig
import time
from types import SimpleNamespace

from geometry_job import cancelled, check_overlap, checked_digest, stop
from audit_worker_environment import audit_environment, file_digest, record_backbone

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "workers" / "dense"))
from cached_sources import checked_path
from neighbors import prepare
from pilot import digest, parse, publish, read_pinned

__all__ = [
    "ROOT",
    "admit",
    "checked_result",
    "execute",
    "input_path",
    "main",
    "run_child",
]


def input_path(base, name):
    if not isinstance(name, str) or not name or "\0" in name:
        raise ValueError("Invalid depth input path")
    path = (base / name).absolute()
    if path.resolve(strict=True) != path:
        raise ValueError("Depth input may not use symlink aliases or traversal")
    return path


def admit(path, pin, job):
    configuration = parse(read_pinned(path, checked_digest(pin), 1024**2))
    fields = {
        "schema_version",
        "kind",
        "manifest",
        "manifest_sha256",
        "native_manifest",
        "native_manifest_sha256",
        "recipe",
        "recipe_sha256",
        "stage",
        "poses",
        "pose_sha256",
        "environment",
        "environment_sha256",
    }
    if (
        type(configuration) is not dict
        or set(configuration) != fields
        or type(configuration["schema_version"]) is not int
        or configuration["schema_version"] != 1
        or configuration["kind"] != "OWNED_RESIZED_DEPTH_JOB_V1"
    ):
        raise ValueError("Unsupported owned depth request")
    paths = {}
    for key in (
        "manifest",
        "native_manifest",
        "recipe",
        "stage",
        "poses",
        "environment",
    ):
        paths[key] = input_path(path.parent, configuration[key])
    arguments = SimpleNamespace(
        **paths,
        output=job / "stages" / "validation",
        manifest_sha256=configuration["manifest_sha256"],
        native_manifest_sha256=configuration["native_manifest_sha256"],
        recipe_sha256=configuration["recipe_sha256"],
    )
    recipe, _, _, manifest, _, stage, inventory, _ = prepare(arguments)
    read_pinned(
        paths["poses"], checked_digest(configuration["pose_sha256"]), 256 * 1024**2
    )
    if manifest["pose_asset_sha256"] != configuration["pose_sha256"]:
        raise ValueError("Depth request pose pin differs from manifest")
    for frame in manifest["frames"]:
        if cancelled():
            raise InterruptedError("Depth input admission cancelled")
        read_pinned(
            checked_path(paths["manifest"].parent, frame["image"]),
            frame["calibrated_image_sha256"],
            64 * 1024**2,
        )
    for name in ("cameras.bin", "images.bin", "points3D.bin", "rigs.bin", "frames.bin"):
        key = "training/sparse/" + name
        if key in inventory:
            read_pinned(
                checked_path(stage, key), inventory[key]["sha256"], 256 * 1024**2
            )
        elif name not in ("rigs.bin", "frames.bin"):
            raise ValueError("Missing depth model input")
    expected = parse(
        read_pinned(
            paths["environment"],
            checked_digest(configuration["environment_sha256"]),
            16 * 1024**2,
        )
    )
    sites = {Path(sysconfig.get_path(key)) for key in ("purelib", "platlib")}
    actual = audit_environment(
        Path(sys.prefix), sites, ROOT / "workers/dense/environment.txt"
    )
    record_backbone(actual, None)
    actual["auditor_sha256"] = file_digest(
        ROOT / "tools/audit_worker_environment.py", 16 * 1024**2
    )[0]
    if (
        actual != expected
        or not actual["requirements_match"]
        or actual["unlisted_distributions"]
        or actual["uninspected_legacy_metadata"]
        or any(p["integrity"] != "VERIFIED_RECORD" for p in actual["packages"])
    ):
        raise ValueError(
            "Dense environment bytes/RECORD differ from the pinned clean receipt"
        )
    inputs = [
        path,
        *paths.values(),
        paths["manifest"].parent,
        ROOT / "workers/dense/neighbors.py",
        Path(__file__),
    ]
    check_overlap(job, inputs)
    owner = dict(
        schema_version=1,
        kind="OWNED_RESIZED_DEPTH_JOB_V1",
        request=str(path),
        request_sha256=pin,
        python=str(Path(sys.executable).resolve(strict=True)),
        python_sha256=file_digest(Path(sys.executable))[0],
        coordinator_sha256=digest(Path(__file__).read_bytes()),
        worker_sha256=recipe["worker_sha256"],
    )
    return configuration, arguments, manifest, recipe, owner


def checked_result(stage, configuration, manifest, recipe):
    if stage.is_symlink() or not stage.is_dir():
        raise ValueError("Invalid depth attempt")
    result_path = checked_path(stage, "result.json")
    with result_path.open("rb") as stream:
        data = stream.read(1024**2 + 1)
    if len(data) > 1024**2:
        raise ValueError("Depth result exceeds document budget")
    result = parse(data)
    selected = [manifest["frames"][i] for i in recipe["selection_indices"]]
    if (
        type(result) is not dict
        or result.get("schema_version") != 1
        or result.get("status") != "PARTIAL_NEIGHBOR_PILOT_COMPLETE"
        or result.get("manifest_sha256") != configuration["manifest_sha256"]
        or result.get("recipe_sha256") != configuration["recipe_sha256"]
        or result.get("worker_sha256") != recipe["worker_sha256"]
        or type(result.get("frames")) is not list
        or len(result["frames"]) != len(selected)
    ):
        raise ValueError("Depth result dependencies/inventory differ")
    prepared = checked_path(stage, "prepared.json")
    read_pinned(prepared, checked_digest(result["prepared_sha256"]), 1024**2)
    for frame, record in zip(selected, result["frames"]):
        if (
            record["frame"] != frame["frame"]
            or record["asset"] != "assets/" + Path(frame["image"]).name + ".wedepth"
        ):
            raise ValueError("Depth result frame order/path differs")
        read_pinned(
            checked_path(stage, record["asset"]),
            checked_digest(record["sha256"]),
            256 * 1024**2,
        )
    return digest(data)


def run_child(arguments, output, parent_supervised):
    command = [sys.executable, str(ROOT / "workers/dense/neighbors.py")]
    for key in (
        "manifest",
        "manifest_sha256",
        "native_manifest",
        "native_manifest_sha256",
        "recipe",
        "recipe_sha256",
        "stage",
    ):
        command += ["--" + key.replace("_", "-"), str(getattr(arguments, key))]
    command += ["--output", str(output)]
    child = subprocess.Popen(command, start_new_session=not parent_supervised)
    started = time.monotonic()
    try:
        while (
            child.poll() is None
            and not cancelled()
            and time.monotonic() - started < 900
        ):
            time.sleep(0.02)
        if child.poll() is None:
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
        if cancelled():
            raise InterruptedError("Depth job cancelled; attempt retained")
        if child.returncode or time.monotonic() - started >= 900:
            raise RuntimeError(
                "Depth backend failed or exhausted time budget; attempt retained"
            )
    finally:
        if child.poll() is None:
            if parent_supervised:
                child.kill()
            else:
                os.killpg(child.pid, signal.SIGKILL)
            child.wait()


def execute(args):
    if sys.platform != "linux":
        raise ValueError("Owned CUDA depth currently requires Linux")
    path = Path(args.request).absolute()
    if path.resolve(strict=True) != path:
        raise ValueError("Linked depth request")
    job = Path(args.output).absolute()
    if job.resolve(strict=False) != job:
        raise ValueError("Linked depth output")
    configuration, arguments, manifest, recipe, owner = admit(path, args.sha256, job)
    owner_bytes = (
        json.dumps(owner, sort_keys=True, separators=(",", ":")) + "\n"
    ).encode()
    owner_sha = digest(owner_bytes)
    if job.exists():
        if not job.is_dir() or not {p.name for p in job.iterdir()} <= {
            "owner.json",
            "job.lock",
            "stages",
            "manifest.json",
        }:
            raise ValueError("Foreign depth output")
        if (
            read_pinned(checked_path(job, "owner.json"), owner_sha, 1024**2)
            != owner_bytes
        ):
            raise ValueError("Depth job owner differs")
    if cancelled():
        raise InterruptedError("Depth job cancelled before ownership")
    job.mkdir(parents=True, exist_ok=True)
    if (job / "job.lock").is_symlink():
        raise ValueError("Linked depth lock")
    with (job / "job.lock").open("a+b") as lock:
        fcntl.flock(lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        owner_path = job / "owner.json"
        if owner_path.exists():
            read_pinned(checked_path(job, "owner.json"), owner_sha, 1024**2)
        else:
            if {p.name for p in job.iterdir()} != {"job.lock"}:
                raise ValueError("Unowned depth output")
            publish(owner_path, owner_bytes)
        stages = job / "stages"
        stages.mkdir(exist_ok=True)
        if stages.resolve(strict=True) != stages:
            raise ValueError("Linked depth stages")
        prefix = "depth-" + owner_sha + "-attempt-"
        entries = list(stages.iterdir())
        allowed = {prefix + str(i) for i in range(128)}
        if len(entries) > 128 or any(
            p.is_symlink() or not p.is_dir() or p.name not in allowed for p in entries
        ):
            raise ValueError("Invalid depth attempt inventory")
        complete = [
            (p, checked_result(p, configuration, manifest, recipe))
            for p in entries
            if (p / "result.json").exists() or (p / "result.json").is_symlink()
        ]
        if len(complete) > 1:
            raise ValueError("Ambiguous terminal depth attempts")
        if complete:
            stage, result_sha = complete[0]
        else:
            stage = next(
                (
                    stages / (prefix + str(i))
                    for i in range(128)
                    if not (stages / (prefix + str(i))).exists()
                ),
                None,
            )
            if stage is None:
                raise ValueError("Depth job exhausted 128 retained attempts")
            print(
                "Launching pinned CUDA depth; partial surfaces, no walking.", flush=True
            )
            run_child(arguments, stage, args.parent_supervised)
            result_sha = checked_result(stage, configuration, manifest, recipe)
        # Source/environment changes invalidate completed reuse too.
        if admit(path, args.sha256, job)[4] != owner:
            raise ValueError("Depth inputs changed during execution")
        if cancelled():
            raise InterruptedError("Depth publication cancelled; attempt retained")
        terminal = dict(
            schema_version=1,
            kind="OWNED_RESIZED_DEPTH_TERMINAL_V1",
            owner_sha256=owner_sha,
            stage=stage.name,
            result_sha256=result_sha,
            scope="PARTIAL_DEPTH_NO_Q5_N2_OR_WALK",
        )
        data = (
            json.dumps(terminal, sort_keys=True, separators=(",", ":")) + "\n"
        ).encode()
        target = job / "manifest.json"
        if target.exists() or target.is_symlink():
            read_pinned(checked_path(job, "manifest.json"), digest(data), 1024**2)
        else:
            publish(target, data)
        print(json.dumps(terminal, sort_keys=True), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("request")
    parser.add_argument("sha256")
    parser.add_argument("output")
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
    except (
        ValueError,
        OSError,
        KeyError,
        TypeError,
        RuntimeError,
        StopIteration,
    ) as error:
        print(error, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
