#!/usr/bin/env python3
"""Finish a retained COLMAP solve in a fresh transactional pose job."""

import argparse
import json
from pathlib import Path
import shutil
import signal
import sys

__all__ = [
    "main",
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--request", type=Path, required=True, help="Original batch-request.json"
    )
    parser.add_argument("--failed-stage", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    seed = parser.add_mutually_exclusive_group()
    seed.add_argument(
        "--seed-images",
        nargs=2,
        help="Reinitialize mapping from this verified bridge; reuse features/matches",
    )
    seed.add_argument(
        "--initial-model",
        type=Path,
        help="Continue one retained model against all saved feature/match pairs",
    )
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    sys.path.insert(0, str(root / "workers"))
    import worker
    from colmap.journal import run

    for sig in (signal.SIGINT, signal.SIGTERM):
        signal.signal(sig, lambda *_: worker.CANCELLED.set())

    request = worker.read_json(args.request.resolve(strict=True))
    original = Path(request["job_dir"]).resolve(strict=True)
    failed = args.failed_stage.resolve(strict=True)
    output = args.output.resolve()
    artifacts = (root / "artifacts").resolve(strict=True)
    if (
        not original.is_relative_to(artifacts)
        or failed.parent != original / "stages"
        or not failed.name.startswith(".poses-")
        or not output.is_relative_to(artifacts)
        or output.is_relative_to(original)
        or original.is_relative_to(output)
    ):
        parser.error(
            "recovery requires a retained failed pose stage and disjoint artifact output"
        )
    pointer = worker.read_json(original / "decode.json")
    decoded = Path(pointer["output"]).resolve(strict=True)
    if (
        decoded.parent != original / "stages"
        or decoded.name != "decode-" + pointer["key"]
        or not worker.verify_stage(
            decoded, pointer["key"], worker.LIMITS["max_output_bytes"]
        )
    ):
        parser.error("original committed decode stage failed verification")
    records = (failed / "commands.jsonl").read_text().splitlines()
    commands = [json.loads(line)["argv"] for line in records]

    def belongs_to_decode(argv):
        return (
            len(argv) > 2
            and argv[1] == str(root / "workers/colmap/solve.py")
            and "--images" in argv
            and "--output" in argv
            and argv[argv.index("--images") + 1] == str(decoded / "images")
            and argv[argv.index("--output") + 1] == str(failed)
        )

    if not any(belongs_to_decode(argv) for argv in commands):
        parser.error("failed solve does not reference this verified decode stage")
    sparse = failed / "sparse"
    inventory = worker.file_inventory(sparse, worker.LIMITS["max_output_bytes"])
    if not inventory:
        parser.error("retained solve has no model assets")
    output.mkdir(parents=True, exist_ok=False)
    shutil.copytree(original / "input", output / "input")
    recovered = request | {
        "job_dir": str(output),
        "journal": str(output / "input/journal.json"),
    }
    worker.atomic_json(
        output / "recovery.json",
        {
            "schema_version": 1,
            "original_job": str(original),
            "failed_stage": str(failed),
            "decode_completion_sha256": worker.sha256(decoded / "completion.json"),
            "retained_sparse": inventory,
        },
    )
    worker.atomic_json(output / "batch-request.json", recovered)
    if args.seed_images or args.initial_model:
        mapping = output / "mapping"
        mapping.mkdir()
        database = failed / "database.db"
        initial_hash = worker.sha256(database)
        shutil.copyfile(database, mapping / "database.db")
        worker.atomic_json(
            mapping / "input.json",
            {
                "database_sha256": initial_hash,
                "seed_images": args.seed_images,
                "initial_model": (
                    str(args.initial_model) if args.initial_model else None
                ),
                "decode_completion_sha256": worker.sha256(decoded / "completion.json"),
            },
        )
        initialization = (
            ["--seed-images", *args.seed_images] if args.seed_images else []
        )
        if args.initial_model:
            model = args.initial_model.resolve(strict=True)
            if model.parent != failed / "sparse":
                parser.error("initial model must belong to the retained failed solve")
            worker.file_inventory(model, worker.LIMITS["max_output_bytes"])
            shutil.copytree(model, mapping / "initial-model")
            initialization = ["--initial-model", str(mapping / "initial-model")]
        worker.run_command(
            [
                request["pose_python"],
                str(root / "tools/remap_video_poses.py"),
                "--database",
                str(mapping / "database.db"),
                "--images",
                str(decoded / "images"),
                "--output",
                str(mapping / "sparse"),
                *initialization,
                "--threads",
                str(request.get("limits", {}).get("cpu_threads", 2)),
            ],
            timeout=worker.LIMITS["stage_timeout_seconds"],
            log=mapping / "backend.log",
            threads=request.get("limits", {}).get("cpu_threads", 2),
            output_directory=mapping,
            output_budget=worker.LIMITS["max_output_bytes"],
        )
        if worker.sha256(database) != initial_hash:
            raise RuntimeError("Original match database changed during recovery")
        sparse = mapping / "sparse"
    run(worker, recovered, retained_sparse=sparse)
    if (
        worker.file_inventory(failed / "sparse", worker.LIMITS["max_output_bytes"])
        != inventory
    ):
        raise RuntimeError("original retained solve changed during recovery")


if __name__ == "__main__":
    main()
