#!/usr/bin/env python3
"""Inspect registered frame coverage using the pinned official COLMAP bindings."""

import argparse
import json
from pathlib import Path

from model_selection import select_candidate


__all__ = [
    "main",
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sparse", type=Path, required=True)
    parser.add_argument("--frames", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    import pycolmap

    if pycolmap.__version__ != "4.2.1":
        raise RuntimeError("Official pycolmap 4.2.1 is required")
    frames = json.loads(args.frames.read_text())["frames"]
    selected = {Path(frame["file"]).name for frame in frames}
    if len(selected) != len(frames) or not 3 <= len(frames) <= 120:
        raise ValueError("Invalid selected frame inventory")
    paths = [
        p
        for p in sorted(args.sparse.iterdir())
        if p.is_dir() and (p / "cameras.bin").exists()
    ]
    if not 1 <= len(paths) <= 50:
        raise ValueError("Invalid model count")
    candidates = []
    for path in paths:
        if path.is_symlink() or any(p.is_symlink() for p in path.rglob("*")):
            raise ValueError("Model assets may not be symbolic links")
        model = pycolmap.Reconstruction(path)
        images = sorted(image.name for image in model.images.values() if image.has_pose)
        if len(images) != len(set(images)):
            raise ValueError("Duplicate model image names")
        candidates.append(
            {"model": path.name, "images": images, "points": model.num_points3D()}
        )
    choice = select_candidate([set(c["images"]) for c in candidates], selected)
    args.output.write_text(
        json.dumps(
            {
                "schema_version": 1,
                "selected_model": None if choice is None else paths[choice].name,
                "policy": "unique_strict_superset_v1",
                "selected_frame_count": len(frames),
                "candidates": candidates,
            },
            indent=2,
        )
        + "\n"
    )


if __name__ == "__main__":
    main()
