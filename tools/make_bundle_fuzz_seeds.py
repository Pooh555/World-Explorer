#!/usr/bin/env python3
"""Create synthetic bundle-mutation bytecode seeds under ignored build/artifact paths."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="fresh seed directory under build/ or artifacts/")
    args = parser.parse_args()
    output = args.output.resolve()
    if not any(output.is_relative_to(ROOT / name) and output != ROOT / name
               for name in ("build", "artifacts")):
        parser.error("output must be a fresh subdirectory of this repository's build/ or artifacts/")
    if output.exists() or output.is_symlink():
        parser.error("output already exists; no files were changed")
    output.mkdir(parents=True)
    corpus = output / "corpus"
    corpus.mkdir()
    manifest = {"source_kind": "synthetic_diagnostic", "fixture": "bundle-fuzz-v1",
                "header_bytes": 8, "max_payload_bytes": 4096, "files": {}}

    def write(name, target=6, operation=0, flags=0, offset=0, selector=0,
              budget=0, value=0, payload=b""):
        data = bytes((target, operation, flags, offset & 255, offset >> 8,
                      selector, budget, value)) + payload
        assert len(data) <= 4104
        with (corpus / name).open("xb") as stream:
            stream.write(data)
        manifest["files"][name] = {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}

    write("baseline")
    write("max-input", payload=b"x" * 4096)
    for field in range(21):
        for label, value in (("null", None), ("negative", -1), ("large", 2**64),
                             ("object", {})):
            write(f"manifest-{field:02d}-{label}", target=5, selector=field,
                  payload=json.dumps(value).encode())
    for name, field, value in (("schema", 0, 2), ("id", 1, 0),
                               ("scale", 15, "METRIC_MEASURED"),
                               ("traversal", 4, "../caller.txt"),
                               ("absolute", 4, "/caller.txt"),
                               ("empty-path", 4, ""), ("dot-path", 4, "."),
                               ("backslash", 7, "bad\\path.json"),
                               ("long-path", 4, "a" * 129),
                               ("hash", 5, "f" * 64)):
        write(f"manifest-{name}", target=5, selector=field, payload=json.dumps(value).encode())
    for value in (b"null", b"[]", b"{}", b"false", b'"text"'):
        write(f"manifest-root-{value.hex()}", target=5, flags=2, payload=value)
    write("manifest-raw-invalid", target=0, operation=3, payload=b'{"scene_id":')
    write("manifest-raw-deep", target=0, operation=3, payload=b"[" * 256 + b"]" * 256)
    for target in range(1, 5):
        for rehash in range(2):
            prefix = f"target-{target}-rehash-{rehash}"
            write(prefix + "-magic", target=target, flags=rehash, payload=b"INVALID!")
            write(prefix + "-truncated", target=target, flags=rehash, operation=1, offset=17)
            write(prefix + "-trailing", target=target, flags=rehash, operation=2, payload=b"x")
    # Index offsets: 16-byte header and 158-byte entries from the production writer.
    for label, offset, payload in (
            ("count", 8, struct.pack("<Q", 2**64 - 1)),
            ("zero-id", 16, bytes(8)), ("mismatch-id", 16, struct.pack("<Q", 3)),
            ("size", 24, struct.pack("<Q", 2**64 - 1)),
            ("hash", 32, b"z" * 64), ("name-length", 96, b"\xff" * 4),
            ("path", 100, b"../caller.txt"),
            ("duplicate-id", 174, struct.pack("<Q", 1))):
        write("index-" + label, target=1, flags=1, offset=offset, payload=payload)
    for target in (2, 3):
        for label, offset, payload in (
                ("id", 8, bytes(8)), ("generation", 16, bytes(8)),
                ("geometry", 32, struct.pack("<Q", 2)),
                ("transform-nan", 40, struct.pack("<d", float("nan"))),
                ("scale", 96, struct.pack("<d", -1)),
                ("bounds", 104, struct.pack("<d", float("inf"))),
                ("splats", 152, b"\xff" * 8), ("frames", 160, b"\xff" * 8),
                ("zero-frame", 168, bytes(8)), ("duplicate-frame", 176, struct.pack("<Q", 11)),
                ("mean", 184, struct.pack("<f", float("nan"))),
                ("splat-scale", 196, struct.pack("<f", -1)),
                ("rotation", 208, bytes(16)), ("opacity", 224, struct.pack("<f", 2)),
                ("sh", 228, struct.pack("<f", float("inf"))),
                ("degree", 420, struct.pack("<I", 4)),
                ("valid-mean", 184, struct.pack("<f", 4))):
            write(f"chunk-{target}-{label}", target=target, flags=1, offset=offset, payload=payload)
    for label, value in (("schema", {"schema_version": 2}),
                         ("metadata", {"schema_version": 1, "metadata": {"a": 3}}),
                         ("valid", {"schema_version": 1, "metadata": {
                             "source_kind": "synthetic_diagnostic", "fixture": "mutated"}})):
        write("provenance-" + label, target=4, operation=3, flags=1,
              payload=json.dumps(value).encode())
    for budget in range(1, 7):
        for value in (0, 1, 2, 255):
            write(f"budget-{budget}-{value}", budget=budget, value=value)
    with (output / "manifest.json").open("x") as stream:
        json.dump(manifest, stream, indent=2)
        stream.write("\n")
    print(f"Created {len(manifest['files'])} synthetic seeds; manifest: {output / 'manifest.json'}")


if __name__ == "__main__":
    main()
