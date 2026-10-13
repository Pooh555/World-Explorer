#!/usr/bin/env python3
"""Reinitialize mapping on a copied, completed feature/match database."""
import argparse
from pathlib import Path
import sqlite3
import re


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--database", type=Path, required=True)
    parser.add_argument("--images", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    seed = parser.add_mutually_exclusive_group(required=True)
    seed.add_argument("--seed-images", nargs=2)
    seed.add_argument("--initial-model", type=Path)
    parser.add_argument("--threads", type=int, choices=range(1, 5), default=2)
    args = parser.parse_args()
    import pycolmap
    if pycolmap.__version__ != "4.2.1":
        raise RuntimeError("Official pycolmap 4.2.1 is required")
    with sqlite3.connect(args.database.resolve().as_uri() + '?mode=ro', uri=True) as database:
        if database.execute("PRAGMA integrity_check").fetchone()[0] != "ok":
            raise ValueError("Invalid retained match database")
        names = dict(database.execute("SELECT name,image_id FROM images"))
    for name in names:
        image = args.images / name
        if not re.fullmatch(r"[0-9]{6}\.png", name) or image.is_symlink() or not image.is_file():
            raise ValueError("Match database must reference verified decoded images")
    options = {"num_threads": args.threads, "random_seed": 42}
    if args.seed_images:
        first, second = (names[name] for name in args.seed_images)
        if first == second:
            raise ValueError("Initialization requires two distinct images")
        pair_id = min(first, second) * 2147483647 + max(first, second)
        with sqlite3.connect(args.database.resolve().as_uri() + '?mode=ro', uri=True) as database:
            bridge = database.execute("SELECT rows FROM two_view_geometries WHERE pair_id=?",
                                      (pair_id,)).fetchone()
        minimum = pycolmap.IncrementalMapperOptions().init_min_num_inliers
        if not bridge or bridge[0] < minimum:
            raise ValueError("Seed pair does not meet the mapper's existing verified-inlier minimum")
        options |= {"init_image_id1": first, "init_image_id2": second}
    args.output.mkdir(exist_ok=False)
    models = pycolmap.incremental_mapping(args.database, args.images, args.output,
        options=options, input_path=args.initial_model or "")
    if not models:
        raise ValueError("COLMAP rejected the seed pair's initialization geometry")


if __name__ == "__main__":
    main()
