#!/usr/bin/env python3
"""Create a local reconstruction request in the per-video artifact layout."""

import argparse
import hashlib
import json
import math
import re
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("video", type=Path, help="Existing video inside data/")
    parser.add_argument("--sample-fps", type=float, default=3.0)
    parser.add_argument(
        "--name", help="Artifact folder name (default: video filename stem)"
    )
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    try:
        video = args.video.resolve(strict=True)
    except OSError as error:
        parser.error(f"cannot open video: {error}")
    if not video.is_file() or not video.is_relative_to((root / "data").resolve()):
        parser.error(
            "video must be a regular file inside this repository's data directory"
        )
    if video.suffix.lower() not in {".mp4", ".avi", ".mov", ".mkv", ".webm", ".m4v"}:
        parser.error("unsupported video extension")
    if not math.isfinite(args.sample_fps) or not 0.1 <= args.sample_fps <= 4:
        parser.error("sample-fps must be finite and in [0.1, 4]")
    with video.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    name = args.name or video.stem.lower()
    if not re.fullmatch(r"[a-z0-9][a-z0-9_-]{0,63}", name):
        parser.error(
            "name must use 1–64 lowercase letters, digits, underscores or hyphens"
        )
    directory = root / "artifacts" / name
    if directory.exists():
        parser.error(
            "artifact folder already exists; use --name for a separate attempt"
        )
    worker = str(root / "workers/worker.py")
    request = {
        "schema_version": 2,
        "recipe": "local_gsplat_v1",
        "action": "start",
        "video": str(video),
        "pose_action": "solve",
        "pose_job": str(directory / "poses"),
        "map_directory": str(directory / "map"),
        "fit_directory": str(directory / "fit"),
        "scene_output": str(directory / "scene.wexplore"),
        "gsplat_source": str(root / ".deps/gsplat"),
        "fit_python": str(root / ".venv-train/bin/python"),
        "fit_worker_script": str(root / "workers/gsplat/fit.py"),
        "steps": 7000,
        "max_image_dimension": 1280,
        "max_splats": 2000000,
        "inspection": {
            "action": "inspect",
            "job_directory": str(directory / "inspection"),
            "worker_script": worker,
            "python": "python3",
            "minimum_free_bytes": 4 * 1024**3,
        },
        "decode": {
            "action": "decode",
            "job_directory": str(directory / "decode"),
            "worker_script": worker,
            "python": "python3",
            "sample_fps": args.sample_fps,
            "max_frames": 120,
            "cpu_threads": 4,
            "max_output_bytes": 32 * 1024**3,
        },
        "pose_worker": {
            "worker_script": worker,
            "worker_python": "python3",
            "pose_python": str(root / ".venv-worker/bin/python"),
            "recipe": "exhaustive_guided",
            "cpu_threads": 4,
            "journal_bytes": 2 * 1024**3,
        },
    }
    directory.mkdir(parents=True, exist_ok=False)
    (directory / "source.json").write_text(
        json.dumps(
            {
                "schema_version": 1,
                "video": str(video.relative_to(root)),
                "sha256": digest,
            },
            indent=2,
        )
        + "\n"
    )
    path = directory / "request.json"
    path.write_text(json.dumps(request, indent=2) + "\n")
    print(path.relative_to(root))


if __name__ == "__main__":
    main()
