#!/usr/bin/env python3
"""Inspect a retained failed pose job without launching a solver or changing its inputs."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sqlite3
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "workers"))
from colmap.recovery_analysis import analyze_matches, read_matches
import worker

__all__ = [
    "ROOT",
    "inspect",
    "main",
    "publish",
    "sha",
]


def sha(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024**2), b""):
            digest.update(block)
    return digest.hexdigest()


def inspect(request_path, failed_stage):
    import pycolmap

    if pycolmap.__version__ != "4.2.1":
        raise ValueError("Official pycolmap 4.2.1 is required")
    identity = {}

    def track(path):
        if path.is_symlink() or not path.is_file():
            raise ValueError(
                "Recovery inputs must be regular files, without symbolic links"
            )
        identity[str(path)] = sha(path)

    track(request_path)
    request = worker.read_json(request_path)
    if (
        type(request.get("schema_version")) is not int
        or request["schema_version"] != 1
        or not isinstance(request.get("job_dir"), str)
        or not isinstance(request.get("journal_sha256"), str)
        or not re.fullmatch(r"[0-9a-f]{64}", request["journal_sha256"])
    ):
        raise ValueError("Invalid original batch request")
    original = Path(request["job_dir"]).resolve(strict=True)
    failed = failed_stage.resolve(strict=True)
    artifacts = (ROOT / "artifacts").resolve(strict=True)
    if (
        failed_stage.is_symlink()
        or not original.is_relative_to(artifacts)
        or failed.parent != original / "stages"
        or not failed.name.startswith(".poses-")
    ):
        raise ValueError(
            "Expected a retained failed stage in the original artifact job"
        )
    pointer_path = original / "decode.json"
    track(pointer_path)
    pointer = worker.read_json(pointer_path)
    if (
        not isinstance(pointer.get("output"), str)
        or not isinstance(pointer.get("key"), str)
        or not re.fullmatch(r"[0-9a-f]{64}", pointer["key"])
    ):
        raise ValueError("Invalid committed decode pointer")
    decoded = Path(pointer["output"]).resolve(strict=True)
    if (
        decoded.parent != original / "stages"
        or decoded.name != "decode-" + pointer["key"]
    ):
        raise ValueError("Original committed decode failed verification")
    track(decoded / "completion.json")
    track(decoded / "result.json")
    if not worker.verify_stage(
        decoded, pointer["key"], worker.LIMITS["max_output_bytes"]
    ):
        raise ValueError("Original committed decode failed verification")
    journal = failed / "commands.jsonl"
    if journal.stat().st_size > worker.MAX_LOG_BYTES:
        raise ValueError("Solver command log exceeds byte budget")
    track(journal)
    with journal.open("rb") as stream:
        command_bytes = stream.read(worker.MAX_LOG_BYTES + 1)
    if len(command_bytes) > worker.MAX_LOG_BYTES:
        raise ValueError("Solver command log exceeds byte budget")
    commands = []
    for line in command_bytes.splitlines():
        record = json.loads(line)
        if (
            not isinstance(record, dict)
            or not isinstance(record.get("argv"), list)
            or any(not isinstance(value, str) for value in record["argv"])
        ):
            raise ValueError("Invalid recorded solver command")
        commands.append(record["argv"])

    def matches_decode(argv):
        return (
            isinstance(argv, list)
            and len(argv) > 2
            and argv[1] == str(ROOT / "workers/colmap/solve.py")
            and "--images" in argv
            and "--output" in argv
            and argv.index("--images") + 1 < len(argv)
            and argv.index("--output") + 1 < len(argv)
            and argv[argv.index("--images") + 1] == str(decoded / "images")
            and argv[argv.index("--output") + 1] == str(failed)
        )

    if not any(matches_decode(argv) for argv in commands):
        raise ValueError("Failed solve is not bound to this decode")
    sparse = failed / "sparse"
    if sparse.is_symlink():
        raise ValueError("Sparse directory may not be a symbolic link")
    inventory = worker.file_inventory(sparse, 1024**3)
    paths = sorted(
        p for p in sparse.iterdir() if p.is_dir() and (p / "cameras.bin").is_file()
    )
    if not 1 <= len(paths) <= 50:
        raise ValueError("Invalid retained model count")
    # Check the independent database budget before hashing or backend loading.
    database = failed / "database.db"
    if database.stat().st_size > 1024**3:
        raise ValueError("Match database exceeds byte budget")
    track(database)
    images, pairs = read_matches(database)
    frames = worker.read_json(decoded / "result.json")
    if request["journal_sha256"] != frames.get("journal_sha256"):
        raise ValueError("Request journal differs from the verified decode")
    if (
        not isinstance(frames.get("source_sha256"), str)
        or not re.fullmatch(r"[0-9a-f]{64}", frames["source_sha256"])
        or not isinstance(frames.get("frames"), list)
        or not 3 <= len(frames["frames"]) <= 120
        or any(
            not isinstance(frame, dict)
            or not isinstance(frame.get("file"), str)
            or not re.fullmatch(r"images/[0-9]{6}\.png", frame["file"])
            for frame in frames["frames"]
        )
    ):
        raise ValueError("Invalid verified frame metadata")
    selected = [Path(frame["file"]).name for frame in frames["frames"]]
    models = []
    for path in paths:
        model = pycolmap.Reconstruction(path)
        models.append(
            {
                "model": path.name,
                "images": [
                    image.name for image in model.images.values() if image.has_pose
                ],
            }
        )
    options = pycolmap.IncrementalMapperOptions()
    configurations = {
        int(value): name
        for name, value in pycolmap.TwoViewGeometryConfiguration.__members__.items()
    }
    report = analyze_matches(
        images,
        pairs,
        selected,
        models,
        configurations,
        {
            "mapper_registration_pair_count": options.abs_pose_min_num_inliers,
            "mapper_seed_pair_count": options.init_min_num_inliers,
        },
    )
    if (
        any(sha(Path(path)) != digest for path, digest in identity.items())
        or worker.file_inventory(sparse, 1024**3) != inventory
        or not worker.verify_stage(
            decoded, pointer["key"], worker.LIMITS["max_output_bytes"]
        )
    ):
        raise ValueError("Recovery inputs changed during inspection")
    return {
        "schema_version": 1,
        "scope": "read-only recovery analysis; no reconstruction gate pass",
        "backend": {"pycolmap": pycolmap.__version__},
        "source_sha256": frames["source_sha256"],
        "failed_stage": str(failed),
        "implementation_sha256": {
            str(p.relative_to(ROOT)): sha(p)
            for p in (
                Path(__file__),
                ROOT / "workers/colmap/recovery_analysis.py",
                ROOT / "workers/colmap/model_selection.py",
                ROOT / "workers/worker.py",
            )
        },
        "input_sha256": identity,
        "sparse_inventory": inventory,
        **report,
    }


def publish(output, report):
    data = (json.dumps(report, allow_nan=False, indent=2) + "\n").encode()
    # Atomic exclusive publication: never truncate an existing report or input.
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(
            dir=output.parent, prefix=".recovery-", delete=False
        ) as f:
            temporary = Path(f.name)
            f.write(data)
            f.flush()
            os.fsync(f.fileno())
        os.link(temporary, output)
    finally:
        if temporary is not None:
            temporary.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--request", type=Path, required=True)
    parser.add_argument("--failed-stage", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.output.exists() or args.output.is_symlink():
            raise ValueError("Output must not exist")
        report = inspect(args.request.resolve(strict=True), args.failed_stage)
        output = args.output.parent.resolve(strict=True) / args.output.name
        job = Path(report["failed_stage"]).parent.parent
        if output.is_relative_to(job):
            raise ValueError("Recovery report must be outside the retained job")
        publish(output, report)
    except (
        OSError,
        ValueError,
        KeyError,
        RuntimeError,
        sqlite3.Error,
        worker.WorkerError,
    ) as error:
        parser.exit(2, str(error) + "\n")


if __name__ == "__main__":
    main()
