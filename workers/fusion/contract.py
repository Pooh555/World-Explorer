"""Project-owned bounded pilot contract; importing this does not load a backend."""

import hashlib
import json
from pathlib import Path
import sys

DENSE = Path(__file__).resolve().parents[1] / "dense"
sys.path.insert(0, str(DENSE))
from pilot import digest, parse, read_pinned, publish
from cached_sources import new_output

__all__ = [
    "DENSE",
    "file_record",
    "implementation_digest",
    "load_request",
    "selected_recipe",
]


def implementation_digest():
    root = Path(__file__).parent
    files = {
        str(path.relative_to(root.parent)): digest(path.read_bytes())
        for path in [root / name for name in ("contract.py", "geometry.py", "run.py")]
        + [
            DENSE / name
            for name in (
                "admitted_visibility.py",
                "cached_sources.py",
                "pilot.py",
                "colmap_io.py",
                "source_selection.py",
                "visibility.py",
            )
        ]
    }
    return digest(json.dumps(files, sort_keys=True, separators=(",", ":")).encode())


def selected_recipe():
    return {
        "schema_version": 1,
        "kind": "OBSERVED_TSDF_CPU_PILOT_V1",
        "implementation_sha256": implementation_digest(),
        "environment_lock_sha256": digest(
            Path(__file__).with_name("environment-lock.txt").read_bytes()
        ),
        "backend": "open3d-cpu==0.19.0",
        "units": "ARBITRARY_POSE_CAMERA_UNITS",
        "extent": "ALL_POSITIVE_WEIGHT_DEPTH_WORLD_POINTS_AXIS_P01_P99_MAX_SPAN",
        "voxel_divisor": 512,
        "truncation_voxels": 4,
        "block_resolution": 8,
        "minimum_consistent_sources": 2,
        "relative_depth_error": 0.01,
        "roundtrip_pixels": 1.0,
        "reject_known_occlusion": True,
        "weight": "REFERENCE_CONFIDENCE_ONLY_NOT_PROBABILITY",
        "free_space": "OBSERVED_POSITIVE_TSDF_IN_LOCAL_TRUNCATION_BAND_ONLY",
        "max_blocks": 32768,
        "chunk_voxels": 65536,
        "max_output_bytes": 512 * 1024**2,
        "max_rss_bytes": 4 * 1024**3,
        "max_seconds": 300,
        "omp_threads": 4,
        "scope": "PARTIAL_OBSERVED_GEOMETRY_NO_Q5_N2_OR_WALK",
    }


def load_request(path, sha):
    path = Path(path).absolute()
    request = parse(read_pinned(path, sha, 1024**2))
    if (
        set(request)
        != {"schema_version", "cache", "receipt_sha256", "recipe", "recipe_sha256"}
        or type(request["schema_version"]) is not int
        or request["schema_version"] != 1
    ):
        raise ValueError("Unsupported fusion request")
    for key in ("receipt_sha256", "recipe_sha256"):
        value = request[key]
        if (
            not isinstance(value, str)
            or len(value) != 64
            or any(c not in "0123456789abcdef" for c in value)
        ):
            raise ValueError("Invalid fusion dependency SHA")
    for key in ("cache", "recipe"):
        if not isinstance(request[key], str) or not request[key]:
            raise ValueError("Invalid fusion dependency path")
    cache = path.parent / request["cache"]
    recipe_path = path.parent / request["recipe"]
    recipe = parse(read_pinned(recipe_path, request["recipe_sha256"], 1024**2))
    # Fixed experimental policy, including code/environment identities; no silent overrides.
    if recipe != selected_recipe() or any(
        type(recipe[k]) is not type(v) for k, v in selected_recipe().items()
    ):
        raise ValueError(
            "Fusion recipe differs from fixed pilot policy/code/environment"
        )
    return request, recipe, cache, [path, recipe_path, cache]


def file_record(path):
    sha = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024**2):
            sha.update(block)
    return {"sha256": sha.hexdigest(), "bytes": path.stat().st_size}
