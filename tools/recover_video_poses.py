#!/usr/bin/env python3
"""Finish a retained COLMAP solve in a fresh transactional pose job."""
import argparse
import json
from pathlib import Path
import shutil
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--request", type=Path, required=True, help="Original batch-request.json")
    parser.add_argument("--failed-stage", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    sys.path.insert(0, str(root / "workers"))
    import worker
    from colmap.journal import run

    request = worker.read_json(args.request.resolve(strict=True))
    original = Path(request["job_dir"]).resolve(strict=True)
    failed = args.failed_stage.resolve(strict=True)
    output = args.output.resolve()
    artifacts = (root / "artifacts").resolve(strict=True)
    if (not original.is_relative_to(artifacts) or failed.parent != original / "stages"
            or not failed.name.startswith(".poses-") or not output.is_relative_to(artifacts)
            or output.is_relative_to(original) or original.is_relative_to(output)):
        parser.error("recovery requires a retained failed pose stage and disjoint artifact output")
    pointer = worker.read_json(original / "decode.json")
    decoded = Path(pointer["output"]).resolve(strict=True)
    if (decoded.parent != original / "stages" or decoded.name != "decode-" + pointer["key"]
            or not worker.verify_stage(decoded, pointer["key"], worker.LIMITS["max_output_bytes"])):
        parser.error("original committed decode stage failed verification")
    records = (failed / "commands.jsonl").read_text().splitlines()
    commands = [json.loads(line)["argv"] for line in records]
    def belongs_to_decode(argv):
        return (len(argv) > 2 and argv[1] == str(root / "workers/colmap/solve.py")
                and "--images" in argv and "--output" in argv
                and argv[argv.index("--images") + 1] == str(decoded / "images")
                and argv[argv.index("--output") + 1] == str(failed))
    if not any(belongs_to_decode(argv) for argv in commands):
        parser.error("failed solve does not reference this verified decode stage")
    sparse = failed / "sparse"
    inventory = worker.file_inventory(sparse, worker.LIMITS["max_output_bytes"])
    if not inventory:
        parser.error("retained solve has no model assets")
    output.mkdir(parents=True, exist_ok=False)
    shutil.copytree(original / "input", output / "input")
    recovered = request | {"job_dir": str(output), "journal": str(output / "input/journal.json")}
    worker.atomic_json(output / "recovery.json", {
        "schema_version": 1, "original_job": str(original), "failed_stage": str(failed),
        "decode_completion_sha256": worker.sha256(decoded / "completion.json"),
        "retained_sparse": inventory,
    })
    worker.atomic_json(output / "batch-request.json", recovered)
    run(worker, recovered, retained_sparse=sparse)
    if worker.file_inventory(sparse, worker.LIMITS["max_output_bytes"]) != inventory:
        raise RuntimeError("original retained solve changed during recovery")


if __name__ == "__main__":
    main()
