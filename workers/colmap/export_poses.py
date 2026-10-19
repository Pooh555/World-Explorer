#!/usr/bin/env python3
"""Canonical pose adapter for official pycolmap 4.2.1 (not rmbrualla/pycolmap)."""

import argparse
import json
import math
from pathlib import Path
import sys

__all__ = [
    "canonical_intrinsics",
    "canonical_pose",
    "main",
]


def canonical_pose(translation, quaternion_xyzw):
    """Left multiply COLMAP world_from_camera by Rx(pi); retain optical axes."""
    x, y, z = map(float, translation)
    qx, qy, qz, qw = map(float, quaternion_xyzw)
    values = [x, y, z, qx, qy, qz, qw]
    if not all(math.isfinite(value) for value in values):
        raise ValueError("Non-finite COLMAP pose")
    if abs(sum(q * q for q in (qx, qy, qz, qw)) - 1) > 1e-5:
        raise ValueError("COLMAP pose quaternion is not normalized")
    return {"translation": [x, -y, -z], "rotation_wxyz": [-qx, qw, -qz, qy]}


def canonical_intrinsics(matrix):
    values = [[float(value) for value in row] for row in matrix]
    if len(values) != 3 or any(len(row) != 3 for row in values):
        raise ValueError("Invalid calibration matrix")
    if not all(math.isfinite(value) for row in values for value in row):
        raise ValueError("Non-finite calibration matrix")
    # COLMAP's upper-left pixel center is (0.5,0.5), ours is (0,0).
    values[0][2] -= 0.5
    values[1][2] -= 0.5
    return values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--frames", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    import pycolmap

    if getattr(pycolmap, "__version__", "") != "4.2.1" or not hasattr(
        pycolmap, "Reconstruction"
    ):
        raise RuntimeError(
            "Official pycolmap 4.2.1 is required in the pose environment"
        )
    reconstruction = pycolmap.Reconstruction(str(args.model))
    frames = json.loads(args.frames.read_text())["frames"]
    identities = {Path(frame["file"]).name: frame for frame in frames}
    cameras = []
    for camera_id, camera in sorted(reconstruction.cameras.items()):
        if not camera.verify_params():
            raise ValueError("Invalid camera parameters")
        cameras.append(
            {
                "camera_id": camera_id,
                "revision": 1,
                "model": camera.model_name,
                "width": camera.width,
                "height": camera.height,
                "calibration_matrix": canonical_intrinsics(camera.calibration_matrix()),
                "backend_params": camera.params.tolist(),
                "backend_params_centers": "half_pixel",
                "canonical_pixel_centers": "integer",
            }
        )
    poses = []
    for image_id, image in sorted(reconstruction.images.items()):
        if not image.has_pose:
            continue
        if image.name not in identities:
            raise ValueError("COLMAP image is not in the selected frame manifest")
        identity = identities[image.name]
        transform = image.cam_from_world().inverse()
        poses.append(
            {
                "image_id": image_id,
                "frame_id": identity["frame_id"],
                "calibration_id": identity["calibration_id"],
                "camera_id": image.camera_id,
                "backend_rig_frame_id": image.frame_id,
                "pose_revision": 1,
                "state": "final",
                "pts": identity["pts"],
                "time_base": identity["time_base"],
                "world_from_camera": canonical_pose(
                    transform.translation, transform.rotation.quat
                ),
            }
        )
    if len(poses) < 3 or reconstruction.num_points3D() < 10:
        raise ValueError("Insufficient registered cameras or triangulated tracks")
    errors = [float(point.error) for point in reconstruction.points3D.values()]
    if not all(math.isfinite(value) for value in errors):
        raise ValueError("Non-finite reconstruction residual")
    value = {
        "schema_version": 1,
        "epoch": 0,
        "revision": 1,
        "registered_frames": len(poses),
        "scale_status": "ARBITRARY",
        "gravity": "unknown",
        "world_up": "+y",
        "backend_world_rotation_wxyz": [0, 1, 0, 0],
        "cameras": cameras,
        "poses": poses,
        "points": reconstruction.num_points3D(),
        "mean_reprojection_error": sum(errors) / len(errors),
        "native_model": "retained; rigs/frames/tracks are not rewritten",
    }
    args.output.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"Pose export failed: {error}", file=sys.stderr)
        raise SystemExit(2)
