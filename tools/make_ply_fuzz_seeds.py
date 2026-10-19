#!/usr/bin/env python3
"""Generate fresh synthetic PLY fuzz seeds only under ignored build/artifact directories."""

import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "output", type=Path, help="new seed directory under build/ or artifacts/"
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
        "fixture": "gsplat-ply-fuzz-v1",
        "files": {},
    }

    def write(name, data):
        path = corpus / name
        with path.open("xb") as stream:
            stream.write(data)
        manifest["files"][name] = {
            "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
        }

    for degree in range(4):
        properties = [
            "x",
            "y",
            "z",
            "f_dc_0",
            "f_dc_1",
            "f_dc_2",
            "opacity",
            "scale_0",
            "scale_1",
            "scale_2",
            "rot_0",
            "rot_1",
            "rot_2",
            "rot_3",
        ]
        rest = ((degree + 1) ** 2 - 1) * 3
        properties += [f"f_rest_{index}" for index in range(rest)]
        lines = (
            [
                "ply",
                "format ascii 1.0",
                "comment exporter gsplat-1.5.3",
                "comment color_domain srgb",
                "element vertex 1",
            ]
            + [f"property float {name}" for name in properties]
            + ["end_header"]
        )
        row = [
            "1",
            "2",
            "3",
            "0.25",
            "0.5",
            "0.75",
            "-2" if degree % 2 else "2",
            "0",
            "0",
            "0",
            "2",
            "0",
            "0",
            "0",
        ] + [str((index + 1) / 16) for index in range(rest)]
        for ending, suffix in (("\n", "lf"), ("\r\n", "crlf")):
            write(
                f"sh{degree}-{suffix}.ply",
                (ending.join(lines + [" ".join(row), ""])).encode("ascii"),
            )
    write("over-header-budget.ply", b"ply\n" + b"\r" * 8193 + b"end_header\n")
    write(
        "invalid-quaternion.ply",
        (corpus / "sh0-lf.ply").read_bytes().replace(b"2 0 0 0\n", b"1e300 0 0 0\n"),
    )
    with (output / "manifest.json").open("x") as stream:
        json.dump(manifest, stream, indent=2)
        stream.write("\n")
    print(
        f"Created {len(manifest['files'])} synthetic seeds; manifest: {output / 'manifest.json'}"
    )


if __name__ == "__main__":
    main()
