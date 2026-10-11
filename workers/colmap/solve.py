#!/usr/bin/env python3
"""Pinned official COLMAP bindings; CPU sparse solve in an isolated process."""
import argparse
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--images", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--threads", type=int, required=True)
    parser.add_argument("--undistort-model", type=Path)
    parser.add_argument("--recipe", choices=("sequential", "exhaustive_guided"), default="sequential")
    args = parser.parse_args()
    import pycolmap
    if pycolmap.__version__ != "4.2.1":
        raise RuntimeError("Official pycolmap 4.2.1 is required")
    if args.undistort_model:
        pycolmap.undistort_images(args.output, args.undistort_model, args.images,
                                  num_threads=args.threads)
        return
    database = args.output / "database.db"
    sparse = args.output / "sparse"
    sparse.mkdir()
    pycolmap.extract_features(database, args.images, camera_mode=pycolmap.CameraMode.SINGLE,
        reader_options={"camera_model": "OPENCV"},
        extraction_options={"num_threads": args.threads, "use_gpu": False},
        device=pycolmap.Device.cpu)
    matching = {"num_threads": args.threads, "use_gpu": False,
                "guided_matching": args.recipe == "exhaustive_guided"}
    if args.recipe == "exhaustive_guided":
        pycolmap.match_exhaustive(database, matching_options=matching, device=pycolmap.Device.cpu)
    else:
        pycolmap.match_sequential(database, matching_options=matching,
            pairing_options={"overlap": 10}, device=pycolmap.Device.cpu)
    models = pycolmap.incremental_mapping(database, args.images, sparse,
        options={"num_threads": args.threads, "random_seed": 42})
    print(json.dumps({"pycolmap": pycolmap.__version__, "components": len(models),
                      "registered": [model.num_reg_images() for model in models.values()]}))


if __name__ == "__main__":
    main()
