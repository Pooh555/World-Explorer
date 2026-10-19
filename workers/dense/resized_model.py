"""Adapt an owned COLMAP 4.2.1 model to checked resized pinhole grids.

No solve or fitting. Original observations, tracks, poses and 3D coordinates are
preserved; image-domain keypoints and camera intrinsics change explicitly.
"""

from pathlib import Path
import math


__all__ = [
    "adapt_model",
    "neighbor_rows",
]


def neighbor_rows(frames, selection, radius=3):
    if (
        type(radius) is not int
        or radius != 3
        or len(selection) != 9
        or any(type(i) is not int for i in selection)
        or selection != list(range(selection[0], selection[0] + 9))
        or selection[0] < 0
        or selection[-1] >= len(frames)
    ):
        raise ValueError("Expected nine consecutive manifest indices and radius three")
    selected = [frames[i] for i in selection]
    names = [Path(f["image"]).name for f in selected]
    if len(set(names)) != len(names):
        raise ValueError("Duplicate selected image names")
    return {
        name: names[max(0, i - radius) : i] + names[i + 1 : i + radius + 1]
        for i, name in enumerate(names)
    }


def adapt_model(reconstruction, native, resized, cancelled=lambda: False):
    import numpy as np
    import pycolmap

    if (
        len(native["frames"]) != len(resized["frames"])
        or not 1 <= len(native["frames"]) <= 120
        or reconstruction.num_cameras() != 1
    ):
        raise ValueError("Unsupported resized model inventory")
    by_name = {image.name: image for image in reconstruction.images.values()}
    if set(by_name) != {Path(f["image"]).name for f in native["frames"]}:
        raise ValueError("Model/native manifest image inventory differs")
    camera = next(iter(reconstruction.cameras.values()))
    if camera.model_name not in ("PINHOLE", "SIMPLE_PINHOLE"):
        raise ValueError("Expected undistorted pinhole native model")
    source_size = (camera.width, camera.height)
    k = camera.calibration_matrix().copy()
    resized_camera = None
    max_ray_error = 0.0
    points = 0
    for original, frame in zip(native["frames"], resized["frames"]):
        if cancelled():
            raise InterruptedError("Resized model adaptation cancelled")
        if (
            original["image"] != frame["image"]
            or original["calibrated_image_sha256"] != frame["native_image_sha256"]
            or (original["width"], original["height"]) != source_size
        ):
            raise ValueError("Resized model input dependency differs")
        image = by_name[Path(frame["image"]).name]
        if (
            image.image_id != frame["backend_image_id"]
            or image.camera_id != frame["backend_camera_id"]
            or image.camera_id != camera.camera_id
        ):
            raise ValueError("Model image/camera identity differs")
        pose = image.cam_from_world().inverse().matrix().copy()
        pose[1:3] *= -1
        canonical = frame["world_from_camera"]
        q = canonical["rotation_wxyz"]
        expected_pose = np.column_stack(
            (
                pycolmap.Rotation3d([q[1], q[2], q[3], q[0]]).matrix(),
                canonical["translation"],
            )
        )
        if (
            canonical != original["world_from_camera"]
            or canonical["scale"] != 1
            or not np.allclose(pose, expected_pose, rtol=0, atol=1e-8)
        ):
            raise ValueError("Resized model pose differs")
        width, height = frame["width"], frame["height"]
        if (
            type(width) is not int
            or type(height) is not int
            or not 11 <= width <= min(source_size[0], 4096)
            or not 11 <= height <= min(source_size[1], 4096)
        ):
            raise ValueError("Unsupported explicit resized model grid")
        sx, sy = width / source_size[0], height / source_size[1]
        params = np.array([k[0, 0] * sx, k[1, 1] * sy, k[0, 2] * sx, k[1, 2] * sy])
        declared = np.array(frame["intrinsics"], dtype=np.float64) + [0, 0, 0.5, 0.5]
        native_declared = np.array(original["intrinsics"], dtype=np.float64) + [
            0,
            0,
            0.5,
            0.5,
        ]
        if not np.allclose(
            native_declared, [k[0, 0], k[1, 1], k[0, 2], k[1, 2]], rtol=0, atol=1e-10
        ) or not np.allclose(params, declared, rtol=0, atol=1e-10):
            raise ValueError("Resized model calibration differs")
        candidate = (width, height, tuple(params))
        if resized_camera is not None and candidate != resized_camera:
            raise ValueError("Shared camera has inconsistent resized calibration")
        resized_camera = candidate
        if image.num_points2D() > 2_000_000:
            raise ValueError("Image keypoint count exceeds model profile")
        for index, point in enumerate(image.points2D):
            if index % 4096 == 0 and cancelled():
                raise InterruptedError("Resized model adaptation cancelled")
            xy = np.asarray(point.xy).copy()
            if not np.isfinite(xy).all():
                raise ValueError("Nonfinite native keypoint")
            # Native COLMAP coordinates have half-pixel centers, so this is
            # image-edge scaling. Canonical cx separately includes the -0.5.
            point.xy = xy * [sx, sy]
            points += 1
        # Asymmetric optical-space probes include off-axis and near-border rays.
        for ray in ((-0.37, 0.21), (0.19, -0.43), (0.011, 0.071)):
            native_pixel = k[:2, :2] @ ray + k[:2, 2]
            converted = native_pixel * [sx, sy] - 0.5
            x, y = converted
            recovered = [
                (x - frame["intrinsics"][2]) / frame["intrinsics"][0],
                (y - frame["intrinsics"][3]) / frame["intrinsics"][1],
            ]
            error = max(abs(a - b) for a, b in zip(ray, recovered))
            if not math.isfinite(error) or error > 1e-12:
                raise ValueError("Resized model ray correspondence failed")
            max_ray_error = max(max_ray_error, error)
    # SIMPLE_PINHOLE has only one focal length. Explicitly promote to PINHOLE
    # so unequal per-axis scale is preserved rather than averaged.
    camera.model = pycolmap.CameraModelId.PINHOLE
    camera.width, camera.height = resized_camera[:2]
    camera.params = np.array(resized_camera[2])
    return {
        "frames": len(by_name),
        "rescaled_keypoints": points,
        "source_grid": list(source_size),
        "resized_grid": list(resized_camera[:2]),
        "maximum_ray_error": max_ray_error,
        "camera_model": "PINHOLE",
        "poses_unchanged": True,
        "geometry_unchanged": True,
    }
