"""Bounded undistorted COLMAP input; image domain remains explicit."""

from collections import OrderedDict
import hashlib
from pathlib import Path

import numpy as np
from PIL import Image
import pycolmap

from train import file_hash


class capture_dataset:
    def __init__(self, root, max_dimension, cache_bytes=128 * 1024**2):
        self.root = Path(root).resolve(strict=True)
        sparse = self.root / "sparse"
        if (sparse / "0").is_dir():
            sparse /= "0"
        reconstruction = pycolmap.Reconstruction(str(sparse))
        images = sorted(reconstruction.images.values(), key=lambda image: image.name)
        if not 5 <= len(images) <= 120 or not 3 <= len(reconstruction.points3D) <= 2000000:
            raise ValueError("Capture exceeds frame/point limits or has too little data")
        self.records = []
        for image in images:
            camera = reconstruction.cameras[image.camera_id]
            if camera.model.name not in ("PINHOLE", "SIMPLE_PINHOLE"):
                raise ValueError("Appearance input must be explicitly undistorted pinhole images")
            if not 11 <= min(camera.width, camera.height) or camera.width * camera.height > 4096**2:
                raise ValueError("Camera image dimensions exceed the supported budget")
            path = (self.root / "images" / image.name).resolve(strict=True)
            path.relative_to(self.root / "images")
            if path.stat().st_size > 128 * 1024**2:
                raise ValueError("Encoded image exceeds the per-file budget")
            ratio = min(1.0, max_dimension / max(camera.width, camera.height))
            width, height = round(camera.width * ratio), round(camera.height * ratio)
            intrinsic = camera.calibration_matrix().astype(np.float32)
            intrinsic[0] *= width / camera.width
            intrinsic[1] *= height / camera.height
            # COLMAP and gsplat share half-pixel centers here. Canonical export
            # subtracts 0.5; resize is x' = scale*x in the half-pixel domain.
            view = np.eye(4, dtype=np.float32)
            view[:3] = image.cam_from_world().matrix()
            self.records.append({"id": image.image_id, "name": image.name, "path": path,
                                 "sha256": file_hash(path), "width": width, "height": height,
                                 "original_width": camera.width, "original_height": camera.height,
                                 "intrinsic": intrinsic, "view": view})
        self.cache = OrderedDict()
        self.cache_bytes = cache_bytes
        self.resident_bytes = 0
        holdout_count = max(1, (len(images) + 4) // 5)
        boundary = len(images) - holdout_count
        self.train_indices = list(range(boundary - 2))
        self.holdout_indices = list(range(boundary, len(images)))
        self.guard_indices = list(range(boundary - 2, boundary))
        # Use only training observations for initial colors. SfM's stored point
        # RGB may include held-out images and cannot initialize training SH.
        point_ids = sorted(reconstruction.points3D)
        point_lookup = {point_id: index for index, point_id in enumerate(point_ids)}
        colors = np.zeros((len(point_ids), 3), dtype=np.float64)
        counts = np.zeros(len(point_ids), dtype=np.uint32)
        for index in self.train_indices:
            pixels = self.image(index)
            image = reconstruction.images[self.records[index]["id"]]
            points = image.points2D
            for point in points:
                if not point.has_point3D():
                    continue
                point_index = point_lookup.get(point.point3D_id)
                x = round(float(point.xy[0]) * pixels.shape[1] / self.records[index]["original_width"] - 0.5)
                y = round(float(point.xy[1]) * pixels.shape[0] / self.records[index]["original_height"] - 0.5)
                if point_index is not None and 0 <= x < pixels.shape[1] and 0 <= y < pixels.shape[0]:
                    colors[point_index] += pixels[y, x]
                    counts[point_index] += 1
        valid = counts > 0
        self.points = np.asarray([reconstruction.points3D[point_id].xyz for point_id in point_ids],
                                 dtype=np.float32)[valid]
        self.colors = (colors[valid] / counts[valid, None] / 255).astype(np.float32)
        centers = np.stack([np.linalg.inv(record["view"])[:3, 3] for record in self.records])
        self.scene_scale = float(np.linalg.norm(centers - centers.mean(axis=0), axis=1).max()) * 1.1
        if not np.isfinite(self.points).all() or not np.isfinite(self.scene_scale) or self.scene_scale <= 1e-8:
            raise ValueError("Non-finite or degenerate SfM input")

    def image(self, index):
        if index in self.cache:
            self.cache.move_to_end(index)
            return self.cache[index]
        record = self.records[index]
        with Image.open(record["path"]) as source:
            if source.size != (record["original_width"], record["original_height"]):
                raise ValueError("Image dimensions differ from calibrated input")
            pixels = np.array(source.convert("RGB").resize(
                (record["width"], record["height"]), Image.Resampling.BICUBIC), copy=True)
        if pixels.nbytes > self.cache_bytes:
            raise ValueError("Single image exceeds the image-cache budget")
        while self.resident_bytes + pixels.nbytes > self.cache_bytes:
            _, removed = self.cache.popitem(last=False)
            self.resident_bytes -= removed.nbytes
        self.cache[index] = pixels
        self.resident_bytes += pixels.nbytes
        return pixels

    def manifest(self):
        return {"frames": [{key: value.tolist() if isinstance(value, np.ndarray) else value
                            for key, value in record.items() if key != "path"}
                           for record in self.records],
                "initial_sfm_sha256": hashlib.sha256(self.points.tobytes() + self.colors.tobytes()).hexdigest(),
                "training_indices": self.train_indices, "holdout_indices": self.holdout_indices,
                "guard_indices": self.guard_indices, "scene_scale": self.scene_scale,
                "initial_colors": "training observations only",
                "resize": "bicubic; half-pixel coordinates scaled separately in x/y",
                "world_transform": "native COLMAP; canonical chunk applies Rx(pi)"}
