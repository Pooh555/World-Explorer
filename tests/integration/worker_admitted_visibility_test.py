"""Synthetic receipt consumer checks; native admission is covered separately in C++."""

import copy
import importlib.util
import io
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import weakref

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "workers/dense"))
from admitted_visibility import iter_admitted_sequence, load_admitted_cache
from pilot import digest


@unittest.skipUnless(
    importlib.util.find_spec("numpy") and importlib.util.find_spec("PIL"),
    "Requires isolated NumPy/Pillow",
)
class AdmittedVisibilityTest(unittest.TestCase):
    def fixture(self):
        import numpy as np
        from PIL import Image

        temporary = tempfile.TemporaryDirectory(prefix="we-admitted-cache-")
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        (root / "images").mkdir()
        (root / "assets").mkdir()
        files = {}

        def write(name, data):
            (root / name).write_bytes(data)
            files[name] = {"bytes": len(data), "sha256": digest(data)}
            return digest(data)

        def document(name, value):
            return write(name, (json.dumps(value, sort_keys=True) + "\n").encode())

        # Explicitly synthetic pose-file placeholder: these tests verify the
        # consumer's integrity checks, never claim to perform native admission.
        poses = write("poses.weposes", b"synthetic pose receipt fixture")
        document(
            "native-recipe.json", {"input_profile": "colmap_native_dense_input_v1"}
        )
        recipe = document(
            "depth-recipe.json", {"input_profile": "colmap_resized_dense_input_v1"}
        )
        frames, originals = [], []
        for index in range(3):
            name = f"{index:06}.png"
            rgb = np.zeros((96, 128, 3), dtype=np.uint8)
            rgb[..., 0] = np.arange(128)[None, :]
            rgb[..., 1] = np.arange(96)[:, None]
            encoded = io.BytesIO()
            Image.fromarray(rgb).save(encoded, format="PNG")
            image_sha = write("images/" + name, encoded.getvalue())
            frame = {
                "image": "training/images/" + name,
                "frame": index + 7,
                "calibration": 3,
                "width": 128,
                "height": 96,
                "intrinsics": [93.0, 117.0, 62.25, 43.75],
                "calibrated_image_sha256": image_sha,
                "observation_image_sha256": "b" * 64,
                "world_from_camera": {
                    "rotation_wxyz": [1, 0, 0, 0],
                    "translation": [index * 0.3, 0, 0],
                    "scale": 1,
                },
            }
            originals.append(copy.deepcopy(frame))
            depth = np.full((96, 128), 4, dtype="<f4")
            valid = np.ones((96, 128), dtype=np.uint8)
            confidence = np.full((96, 128), 0.5, dtype="<f4")
            support = np.full((96, 128), 3, dtype="<u4")
            valid[:, :7] = 0
            depth[:, :7] = confidence[:, :7] = support[:, :7] = 0
            confidence[8, 8] = 0  # Valid zero-weight samples remain valid.
            header = struct.pack(
                "<8s7I4Q4d",
                b"WEXDEP01",
                1,
                1,
                2,
                0,
                128,
                96,
                int(index == 1),
                0,
                1,
                index + 7,
                3,
                *frame["intrinsics"],
            )
            header += (poses + "b" * 64 + image_sha + recipe).encode()
            payload = (
                depth.tobytes()
                + valid.tobytes()
                + confidence.tobytes()
                + support.tobytes()
            )
            if index == 1:
                payload += np.zeros_like(depth).tobytes()
            depth_sha = write("assets/" + name + ".wedepth", header + payload)
            frame.update(
                image="images/" + name,
                manifest_index=index,
                depth_asset="assets/" + name + ".wedepth",
                depth_sha256=depth_sha,
                valid_pixels=int(valid.sum()),
                unknown_pixels=int((valid == 0).sum()),
                uncertainty=(
                    "CAMERA_UNIT_STANDARD_DEVIATION" if index == 1 else "UNAVAILABLE"
                ),
            )
            frames.append(frame)
        manifest = document(
            "dense-inputs.json",
            {
                "kind": "DENSE_INPUTS_RESIZED_GRID",
                "world_epoch": 0,
                "pose_revision": 1,
                "pose_asset_sha256": poses,
                "depth_recipe_sha256": recipe,
                "frames": originals,
            },
        )
        receipt = {
            "schema_version": 1,
            "kind": "POSE_ADMITTED_DEPTH_VISIBILITY_V1",
            "files": files,
            "frames": frames,
            "pose_asset_sha256": poses,
            "input_manifest_sha256": manifest,
            "depth_recipe_sha256": recipe,
            "world_epoch": 0,
            "pose_revision": 1,
            "units": "ARBITRARY_POSE_CAMERA_UNITS",
            "pixel_centers": "integer",
            "camera_axes": "+x_right,+y_down,+z_forward",
            "depth_convention": "AXIAL_CAMERA_Z",
        }
        (root / "manifest.json").write_text(json.dumps(receipt, sort_keys=True))
        return root, receipt

    def load(self, root):
        return load_admitted_cache(root, digest((root / "manifest.json").read_bytes()))

    def sequence_fixture(self):
        root, original = self.fixture()
        sequence = root / "sequence"
        sequence.mkdir()
        batches = []
        for index, (start, end) in enumerate(((0, 1), (1, 3))):
            child = copy.deepcopy(original)
            child["frames"] = child["frames"][start:end]
            names = {
                "poses.weposes",
                "dense-inputs.json",
                "native-recipe.json",
                "depth-recipe.json",
            }
            for frame in child["frames"]:
                names.update((frame["image"], frame["depth_asset"]))
            child["files"] = {k: v for k, v in child["files"].items() if k in names}
            folder = sequence / f"batch-{index}"
            folder.mkdir()
            for name in names:
                (folder / name).parent.mkdir(parents=True, exist_ok=True)
                (folder / name).write_bytes((root / name).read_bytes())
            data = json.dumps(child).encode()
            (folder / "manifest.json").write_bytes(data)
            batches.append(
                dict(
                    cache=folder.name,
                    sha256=digest(data),
                    first_manifest_index=start,
                    frame_count=end - start,
                )
            )
        receipt = {
            key: original[key]
            for key in (
                "frames",
                "input_manifest_sha256",
                "depth_recipe_sha256",
                "pose_asset_sha256",
                "world_epoch",
                "pose_revision",
            )
        }
        receipt.update(
            schema_version=1,
            kind="POSE_ADMITTED_DEPTH_SEQUENCE_V1",
            batches=batches,
            valid_pixels=sum(f["valid_pixels"] for f in original["frames"]),
            unknown_pixels=sum(f["unknown_pixels"] for f in original["frames"]),
        )
        (sequence / "manifest.json").write_text(json.dumps(receipt))
        return sequence, receipt

    def test_sequence_streams_disjoint_complete_batches_and_releases_previous_planes(
        self,
    ):
        root, receipt = self.sequence_fixture()
        stream = iter_admitted_sequence(
            root, digest((root / "manifest.json").read_bytes())
        )
        views, child = next(stream)
        prior = weakref.ref(views[0]["depth"])
        self.assertEqual([f["manifest_index"] for f in child["frames"]], [0])
        del views
        views, child = next(stream)
        self.assertIsNone(prior())
        self.assertEqual([f["manifest_index"] for f in child["frames"]], [1, 2])
        self.assertIsNotNone(views[0]["sigma"])
        self.assertIsNone(views[1]["sigma"])
        self.assertEqual(views[1]["confidence"][8, 8], 0)
        self.assertEqual(views[1]["valid"][8, 8], 1)
        with self.assertRaises(StopIteration):
            next(stream)

    def test_sequence_rejects_incomplete_duplicated_changed_and_unsafe_child_metadata(
        self,
    ):
        for mode in (
            "missing",
            "duplicate",
            "path",
            "pose",
            "summary",
            "boolean",
            "oversize",
        ):
            root, receipt = self.sequence_fixture()
            if mode == "missing":
                receipt["batches"].pop()
            if mode == "duplicate":
                receipt["batches"][1]["first_manifest_index"] = 0
            if mode == "path":
                receipt["batches"][1]["cache"] = "../batch-1"
            if mode == "pose":
                receipt["pose_revision"] += 1
            if mode == "summary":
                receipt["unknown_pixels"] -= 1
            if mode == "boolean":
                receipt["batches"][0]["frame_count"] = True
            if mode == "oversize":
                receipt["batches"][0]["frame_count"] = 19
            (root / "manifest.json").write_text(json.dumps(receipt))
            with self.subTest(mode=mode), self.assertRaises(ValueError):
                next(
                    iter_admitted_sequence(
                        root, digest((root / "manifest.json").read_bytes())
                    )
                )

    def test_sequence_late_corruption_and_cancellation_never_repair_child_bytes(self):
        root, _ = self.sequence_fixture()
        pin = digest((root / "manifest.json").read_bytes())
        stream = iter_admitted_sequence(root, pin)
        next(stream)
        asset = next((root / "batch-1/assets").iterdir())
        asset.write_bytes(b"corrupt later batch")
        with self.assertRaises(ValueError):
            next(stream)
        self.assertEqual(asset.read_bytes(), b"corrupt later batch")
        with self.assertRaises(InterruptedError):
            next(iter_admitted_sequence(root, pin, lambda: True))

    def test_owned_planes_unknowns_optional_sigma_and_asymmetric_pose_are_preserved(
        self,
    ):
        import numpy as np

        root, _ = self.fixture()
        views, _ = self.load(root)
        self.assertEqual(len(views), 3)
        self.assertIsNone(views[0]["sigma"])
        self.assertTrue((views[1]["sigma"] == 0).all())
        for index, view in enumerate(views):
            self.assertFalse(view["depth"].flags.writeable)
            self.assertTrue((view["valid"][:, :7] == 0).all())
            self.assertTrue((view["support"][:, :7] == 0).all())
            self.assertEqual(view["valid"][8, 8], 1)
            self.assertEqual(view["confidence"][8, 8], 0)
            self.assertTrue(np.array_equal(view["cam_from_world"][:, :3], np.eye(3)))
            self.assertAlmostEqual(view["cam_from_world"][0, 3], -index * 0.3)

    def test_wxyz_rotation_is_inverted_as_an_optical_camera_transform(self):
        import math
        import numpy as np

        root, receipt = self.fixture()
        manifest = json.loads((root / "dense-inputs.json").read_bytes())
        pose = {
            "rotation_wxyz": [math.sqrt(0.5), 0, math.sqrt(0.5), 0],
            "translation": [0.3, -0.7, 0.9],
            "scale": 1,
        }
        manifest["frames"][0]["world_from_camera"] = pose
        receipt["frames"][0]["world_from_camera"] = pose
        encoded = json.dumps(manifest).encode()
        (root / "dense-inputs.json").write_bytes(encoded)
        receipt["input_manifest_sha256"] = digest(encoded)
        receipt["files"]["dense-inputs.json"] = {
            "bytes": len(encoded),
            "sha256": digest(encoded),
        }
        (root / "manifest.json").write_text(json.dumps(receipt))
        views, _ = self.load(root)
        expected = np.array([[0.0, 0.0, -1.0], [0.0, 1.0, 0.0], [1.0, 0.0, 0.0]])
        self.assertTrue(
            np.allclose(views[0]["cam_from_world"][:, :3], expected, rtol=0, atol=1e-15)
        )
        self.assertTrue(
            np.allclose(
                views[0]["cam_from_world"][:, 3],
                -expected @ np.array(pose["translation"]),
                rtol=0,
                atol=1e-15,
            )
        )

    def test_changed_or_rehashed_invalid_planes_header_and_payload_are_rejected(self):
        for mode in (
            "checksum",
            "negative-zero",
            "invalid-mask",
            "confidence",
            "support",
            "nan",
            "frame",
            "trailing",
        ):
            root, receipt = self.fixture()
            name = receipt["frames"][0]["depth_asset"]
            data = bytearray((root / name).read_bytes())
            count = 128 * 96
            if mode in ("checksum", "trailing"):
                data += b"\0"
            if mode == "negative-zero":
                data[356:360] = struct.pack("<I", 0x80000000)
            if mode == "invalid-mask":
                data[356 + count * 4] = 2
            if mode == "confidence":
                data[356 + count * 5 + 8 * 4 : 356 + count * 5 + 9 * 4] = struct.pack(
                    "<f", 2
                )
            if mode == "support":
                data[356 + count * 9 + 8 * 4 : 356 + count * 9 + 9 * 4] = struct.pack(
                    "<I", 0
                )
            if mode == "nan":
                data[356 + 8 * 4 : 356 + 9 * 4] = struct.pack("<f", float("nan"))
            if mode == "frame":
                data[52:60] = struct.pack("<Q", 999)
            (root / name).write_bytes(data)
            if mode != "checksum":
                receipt["files"][name] = {"bytes": len(data), "sha256": digest(data)}
                receipt["frames"][0]["depth_sha256"] = digest(data)
                (root / "manifest.json").write_text(json.dumps(receipt))
            with self.subTest(mode=mode):
                with self.assertRaises(ValueError):
                    self.load(root)

    def test_rehashed_receipt_domain_inventory_grid_and_pose_forgery_reject(self):
        for mode in (
            "pose",
            "grid",
            "image",
            "duplicate",
            "extra-file",
            "boolean-count",
            "budget",
        ):
            root, receipt = self.fixture()
            if mode == "pose":
                receipt["frames"][0]["world_from_camera"]["translation"][0] = 5
            if mode == "grid":
                receipt["frames"][0]["intrinsics"][2] += 0.5
            if mode == "image":
                receipt["frames"][0]["image"] = "../escape.png"
            if mode == "duplicate":
                receipt["frames"][1]["manifest_index"] = 0
            if mode == "extra-file":
                receipt["files"]["unlisted"] = {"bytes": 0, "sha256": "f" * 64}
            if mode == "boolean-count":
                receipt["frames"][0]["valid_pixels"] = True
            if mode == "budget":
                receipt["files"]["poses.weposes"]["bytes"] = 256 * 1024**2 + 1
            (root / "manifest.json").write_text(json.dumps(receipt))
            with self.subTest(mode=mode):
                with self.assertRaises((ValueError, FileNotFoundError)):
                    self.load(root)

    def test_cancellation_wrong_receipt_hash_and_parent_symlink_preserve_caller_bytes(
        self,
    ):
        root, _ = self.fixture()
        before = {
            str(path): digest(path.read_bytes())
            for path in root.rglob("*")
            if path.is_file()
        }
        with self.assertRaises(InterruptedError):
            load_admitted_cache(
                root, digest((root / "manifest.json").read_bytes()), lambda: True
            )
        with self.assertRaises(ValueError):
            load_admitted_cache(root, "f" * 64)
        self.assertEqual(
            before,
            {
                str(path): digest(path.read_bytes())
                for path in root.rglob("*")
                if path.is_file()
            },
        )
        (root / "images").rename(root / "moved")
        try:
            (root / "images").symlink_to(root / "moved", target_is_directory=True)
        except PermissionError:
            if os.name == "nt":
                self.skipTest(
                    "Native symlink creation unavailable to this Windows account"
                )
            raise
        with self.assertRaises(ValueError):
            self.load(root)


if __name__ == "__main__":
    unittest.main()
