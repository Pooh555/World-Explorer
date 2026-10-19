#!/usr/bin/env python3
"""Plan bounded sources from an application-admitted native manifest; no GPU solve."""

import argparse
import json
from pathlib import Path
import sys
import tempfile

from pilot import digest, parse, publish, read_pinned
from source_selection import DEFAULT_POLICY, plan_sources, report_digest


def run(args):
    native = parse(read_pinned(args.manifest, args.manifest_sha256, 1024**2))
    if native["schema_version"] != 1 or native["kind"] != "DENSE_INPUTS_NATIVE_GRID":
        raise ValueError("An application-admitted native manifest is required")
    stage = args.stage.resolve(strict=True)
    completion = parse(
        read_pinned(
            stage / "completion.json", native["pose_completion_sha256"], 16 * 1024**2
        )
    )
    inventory = {entry["path"]: entry for entry in completion["files"]}
    if len(inventory) != len(completion["files"]):
        raise ValueError("Duplicate committed model inventory")
    if args.output.exists() or args.output.is_symlink():
        raise ValueError("Source-plan output must be new")
    output = args.output.resolve(strict=False)
    for path in (stage, args.manifest.resolve(strict=True)):
        if output == path or output.is_relative_to(path) or path.is_relative_to(output):
            raise ValueError("Source-plan output overlaps an input")
    import socket

    def no_network(*_args, **_kwargs):
        raise RuntimeError("Network is disabled in the source planner")

    socket.socket = no_network
    import pycolmap

    if pycolmap.__version__ != "4.2.1":
        raise RuntimeError("Pinned COLMAP 4.2.1 required")
    hashes = {}
    with tempfile.TemporaryDirectory(
        dir=output.parent, prefix=".source-plan-"
    ) as directory:
        private = Path(directory)
        for name in (
            "cameras.bin",
            "images.bin",
            "points3D.bin",
            "rigs.bin",
            "frames.bin",
        ):
            key = "training/sparse/" + name
            if key not in inventory:
                if name in ("rigs.bin", "frames.bin"):
                    continue
                raise ValueError("Missing native model file")
            path = stage / key
            if path.resolve(strict=True).relative_to(stage).as_posix() != key:
                raise ValueError("Native model path uses a symlink")
            expected = inventory[key]["sha256"]
            if (
                name in ("cameras.bin", "images.bin")
                and expected != native[name.replace(".bin", "_sha256")]
            ):
                raise ValueError("Native model dependency differs")
            data = read_pinned(path, expected, 256 * 1024**2)
            (private / name).write_bytes(data)
            hashes[name] = digest(data)
        selection = plan_sources(
            pycolmap.Reconstruction(private), native, DEFAULT_POLICY
        )
    report = {
        "schema_version": 1,
        "native_manifest_sha256": args.manifest_sha256,
        "model_sha256": hashes,
        "selection": selection,
        "selection_sha256": report_digest(selection),
    }
    encoded = (
        json.dumps(report, sort_keys=True, indent=2, allow_nan=False) + "\n"
    ).encode()
    publish(output, encoded)
    print("Source plan SHA-256 " + digest(encoded))
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("manifest", "stage", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--manifest-sha256", required=True)
    try:
        return run(parser.parse_args())
    except Exception as error:
        print("Source planning failed: " + str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
