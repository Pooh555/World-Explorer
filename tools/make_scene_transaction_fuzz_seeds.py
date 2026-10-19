#!/usr/bin/env python3
"""Create synthetic scene-transaction bytecode seeds under ignored build/artifact paths."""

import argparse
import hashlib
import json
from pathlib import Path
import struct

__all__ = [
    "HEADER",
    "ROOT",
    "main",
    "record",
]

ROOT = Path(__file__).resolve().parents[1]
HEADER = bytes((4, 8, 8, 0))


def record(
    action=1,
    chunk=1,
    fault=0,
    flags=0,
    generation=0,
    splats=1,
    frames=1,
    operation=0,
    pose=2,
    degree=4,
    axis=0,
    replay=0,
    budget=0,
    scalar_bits=0,
):
    return bytes(
        (
            action,
            chunk,
            fault,
            flags,
            generation,
            splats,
            frames,
            operation,
            pose,
            degree,
            axis,
            replay,
            budget,
            0,
            0,
            0,
        )
    ) + struct.pack("<Q", scalar_bits)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "output", type=Path, help="fresh seed directory under build/ or artifacts/"
    )
    args = parser.parse_args()
    output = args.output.resolve()
    if not any(
        output.is_relative_to(ROOT / name) and output != ROOT / name
        for name in ("build", "artifacts")
    ):
        parser.error(
            "output must be a fresh subdirectory of this repository's build/ or artifacts/"
        )
    if output.exists() or output.is_symlink():
        parser.error("output already exists; no files were changed")
    output.mkdir(parents=True)
    corpus = output / "corpus"
    corpus.mkdir()
    manifest = {
        "source_kind": "synthetic_diagnostic",
        "fixture": "scene-transaction-fuzz-v1",
        "header_bytes": 4,
        "record_bytes": 24,
        "max_records": 32,
        "files": {},
    }

    def write(name, records, header=HEADER):
        data = header + b"".join(records)
        assert len(data) <= 772
        with (corpus / name).open("xb") as stream:
            stream.write(data)
        manifest["files"][name] = {
            "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
        }

    write(
        "lifecycle",
        [
            record(0, 2),
            record(1, 2),
            record(4, 2),
            record(3, 2),
            record(2, 2),
            record(0, 2),
            record(5),
            record(6),
        ],
    )
    write("multi-create-delete", [record(0, 4, flags=17), record(2, 4, flags=17)])
    write("duplicate-chunk", [record(flags=8)])
    write("too-many-operations", [record(0, 4, flags=33)])
    write(
        "repeated-operation",
        [record(operation=3), record(operation=3), record(5), record(6)],
    )
    write("generation-exhausted", [record(4), record(1)], bytes((4, 8, 8, 2)))
    write(
        "revision-exhausted", [record(), record(0, 2), record(5)], bytes((4, 8, 8, 1))
    )
    write("zero-window-budget", [record(), record(2), record(5)], bytes(4))
    write(
        "window-eviction",
        [record(replay=1), record(4, replay=2), record(3, replay=1), record(5)],
        bytes((1, 1, 8, 0)),
    )
    write(
        "aggregate-budget", [record(0, 2, splats=8), record(splats=9), record(frames=5)]
    )
    write("replay-short-budget", [record(replay=2, budget=1), record(replay=3)])
    write("zero-chunk-id", [record(0, 0)])
    write("missing-operation", [record(operation=1)])
    write("stale-generation", [record(generation=1), record(generation=2)])
    for fault in range(1, 25):
        action = 4 if fault in (16, 17, 18) else 1
        # Double +inf and float quiet NaN exercise raw numerical rejection.
        bits = 0x7FF000007FC00000 if fault >= 19 else 0x7FF0000000000000
        write(
            f"fault-{fault:02d}",
            [record(action, fault=fault, scalar_bits=bits), record(5)],
        )
    for name, bits in (
        ("negative-zero", 0x8000000000000000),
        ("quiet-nan", 0x7FF8000000000001),
        ("subnormal", 1),
        ("negative-scale", 0xBFF0000000000000),
        ("translation", 0x4008000000000000),
    ):
        write(
            name,
            [record(4, fault=16 if name != "translation" else 18, scalar_bits=bits)],
        )
    write(
        "max-sequence",
        [
            record(1 if index % 2 == 0 else 4, replay=index % 4, budget=index % 2)
            for index in range(32)
        ],
    )
    with (output / "manifest.json").open("x") as stream:
        json.dump(manifest, stream, indent=2)
        stream.write("\n")
    print(
        f"Created {len(manifest['files'])} synthetic seeds; manifest: {output / 'manifest.json'}"
    )


if __name__ == "__main__":
    main()
