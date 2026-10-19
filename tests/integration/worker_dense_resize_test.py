"""Synthetic resized dense-input safety and pixel tests, with no solver/GPU."""

import copy
import importlib.util
import io
import json
import math
from pathlib import Path
import struct
import sys
import tempfile
from types import SimpleNamespace
import unittest
import zlib

DIRECTORY = Path(__file__).resolve().parents[2] / "workers/dense"
sys.path.insert(0, str(DIRECTORY))
import resize_inputs as resize
from pilot import digest


class dense_resize_test(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="we_dense_resize_")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.stage = self.root / "stage"
        (self.stage / "training/images").mkdir(parents=True)
        (self.stage / "completion.json").write_bytes(b"{}")

        def chunk(kind, data):
            return (
                struct.pack(">I", len(data))
                + kind
                + data
                + struct.pack(">I", zlib.crc32(kind + data))
            )

        # Independent valid PNG encoder, unequal axes and a known linear RGB ramp.
        pixels = b"".join(
            b"\0" + bytes(v for x in range(17) for v in (10 * x, 15 * y, 5 * x + 7 * y))
            for y in range(13)
        )
        png = (
            b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", 17, 13, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(pixels))
            + chunk(b"IEND", b"")
        )
        self.image = self.stage / "training/images/000001.png"
        self.image.write_bytes(png)
        self.native = {
            "schema_version": 1,
            "kind": "DENSE_INPUTS_NATIVE_GRID",
            "preparation_recipe": {"resize": "none"},
            "pose_completion_sha256": digest(b"{}"),
            "frames": [
                {
                    "image": "training/images/000001.png",
                    "width": 17,
                    "height": 13,
                    "intrinsics": [21.0, 29.0, 6.75, 4.25],
                    "calibrated_image_sha256": digest(png),
                }
            ],
        }
        self.profile = {
            "recipe": resize.PROFILE,
            "crop": "none",
            "resize": "PILLOW_BILINEAR",
            "width": 11,
            "height": 11,
            "pixel_centers": "integer",
            "image_encoding": "PNG_RGB8",
            "pillow_version": "11.3.0",
            "worker_sha256": resize.worker_digest(),
            "environment_sha256": digest((DIRECTORY / "environment.txt").read_bytes()),
        }
        self.recipe = {
            "schema_version": 1,
            "input_profile": resize.PROFILE,
            "depth_producer": "MULTIVIEW_STEREO",
            "resize": self.profile,
        }
        self.args = SimpleNamespace(
            stage=self.stage,
            output=self.root / "output",
            manifest=self.root / "native.json",
            recipe=self.root / "recipe.json",
        )
        self.save()

    def save(self):
        self.args.manifest.write_text(json.dumps(self.native))
        self.args.manifest_sha256 = digest(self.args.manifest.read_bytes())
        self.args.recipe.write_text(json.dumps(self.recipe))
        self.args.recipe_sha256 = digest(self.args.recipe.read_bytes())

    def transform(self):
        return resize.transformed(
            self.native, self.args.manifest_sha256, self.recipe, self.args.recipe_sha256
        )

    def test_transform_preserves_inputs_and_asymmetric_camera_rays(self):
        original = copy.deepcopy(self.native)
        frame = self.transform()["frames"][0]
        fx, fy, cx, cy = frame["intrinsics"]
        # Fixed source pixel (12,8) mapped via image-edge coordinates.
        self.assertAlmostEqual(((12 + 0.5) * 11 / 17 - 0.5 - cx) / fx, (12 - 6.75) / 21)
        self.assertAlmostEqual(((8 + 0.5) * 11 / 13 - 0.5 - cy) / fy, (8 - 4.25) / 29)
        self.assertNotAlmostEqual(cx, 6.75 * 11 / 17)
        self.assertEqual(
            frame["native_image_sha256"],
            original["frames"][0]["calibrated_image_sha256"],
        )
        self.assertEqual(original, self.native)

    def test_reject_unsafe_names_duplicate_images_invalid_grid_and_implicit_resize(
        self,
    ):
        for name in (
            "../escape.png",
            "training/images/../escape.png",
            "training/images/a\\b.png",
            "training/images/C:bad.png",
        ):
            with self.subTest(name=name):
                altered = copy.deepcopy(self.native)
                altered["frames"][0]["image"] = name
                with self.assertRaises(ValueError):
                    resize.transformed(altered, "a" * 64, self.recipe, "b" * 64)
        for value in (True, -1, 0, 4097, 2**64):
            with self.subTest(value=value):
                recipe = copy.deepcopy(self.recipe)
                recipe["resize"]["width"] = value
                with self.assertRaises(ValueError):
                    resize.transformed(self.native, "a" * 64, recipe, "b" * 64)
        duplicate = copy.deepcopy(self.native)
        duplicate["frames"] *= 2
        with self.assertRaises(ValueError):
            resize.transformed(duplicate, "a" * 64, self.recipe, "b" * 64)
        recipe = copy.deepcopy(self.recipe)
        recipe["resize"]["width"] = 17
        recipe["resize"]["height"] = 13
        with self.assertRaises(ValueError):
            resize.transformed(self.native, "a" * 64, recipe, "b" * 64)

    def require_pixels(self):
        if not importlib.util.find_spec("PIL"):
            self.skipTest("Pixel path requires isolated pinned Pillow")
        import PIL

        if PIL.__version__ != "11.3.0":
            self.skipTest("Pixel path requires Pillow 11.3.0")
        if sys.platform == "win32":
            self.skipTest("Linux-only preparer directory fsync")

    def test_real_encoded_bytes_grid_and_known_ramp(self):
        self.require_pixels()
        from PIL import Image

        source = self.image.read_bytes()
        self.assertEqual(resize.run(self.args), 0)
        manifest = json.loads((self.args.output / "manifest.json").read_bytes())
        frame = manifest["frames"][0]
        encoded = (self.args.output / frame["image"]).read_bytes()
        self.assertEqual(digest(encoded), frame["calibrated_image_sha256"])
        with Image.open(io.BytesIO(encoded)) as image:
            self.assertEqual(image.size, (11, 11))
            self.assertEqual(image.mode, "RGB")

            # Independent widened triangular downsample filter, normalized at
            # boundaries. Discrete filter support need not preserve an affine
            # ramp exactly. Quantization occurs after each separable axis pass.
            def weights(index, source, target):
                scale = source / target
                center = (index + 0.5) * scale
                values = [
                    max(0, 1 - abs((j + 0.5 - center) / scale)) for j in range(source)
                ]
                total = sum(values)
                return [v / total for v in values]

            for y in range(11):
                wy = weights(y, 13, 11)
                for x in range(11):
                    wx = weights(x, 17, 11)
                    for c, actual in enumerate(image.getpixel((x, y))):
                        rows = []
                        for j in range(13):
                            ramp = [
                                (10 * i, 15 * j, 5 * i + 7 * j)[c] for i in range(17)
                            ]
                            rows.append(
                                math.floor(sum(w * v for w, v in zip(wx, ramp)) + 0.5)
                            )
                        expected = math.floor(
                            sum(w * v for w, v in zip(wy, rows)) + 0.5
                        )
                        self.assertLessEqual(abs(actual - expected), 1)
        self.assertEqual(source, self.image.read_bytes())
        with self.assertRaises(ValueError):
            resize.run(self.args)
        self.assertEqual(encoded, (self.args.output / frame["image"]).read_bytes())

    def test_cancel_before_and_after_pixel_publication_has_no_terminal_manifest(self):
        self.require_pixels()
        self.assertEqual(resize.run(self.args, lambda: True), 130)
        self.assertFalse(self.args.output.exists())
        calls = 0

        def cancel():
            nonlocal calls
            calls += 1
            return calls >= 4

        self.assertEqual(resize.run(self.args, cancel), 130)
        self.assertTrue((self.args.output / "training/images/000001.png").exists())
        self.assertFalse((self.args.output / "manifest.json").exists())

    def test_recipe_drift_corrupt_pixels_and_overlap_reject_without_publication(self):
        self.require_pixels()
        from PIL import UnidentifiedImageError

        self.recipe["resize"]["worker_sha256"] = "f" * 64
        self.save()
        with self.assertRaises(ValueError):
            resize.run(self.args)
        self.assertFalse(self.args.output.exists())
        self.recipe["resize"]["worker_sha256"] = resize.worker_digest()
        self.save()
        self.args.output = self.stage / "overlap"
        with self.assertRaises(ValueError):
            resize.run(self.args)
        self.assertFalse(self.args.output.exists())
        self.args.output = self.root / "output"
        self.image.write_bytes(b"bad png")
        self.native["frames"][0]["calibrated_image_sha256"] = digest(b"bad png")
        self.save()
        with self.assertRaises(UnidentifiedImageError):
            resize.run(self.args)
        self.assertFalse((self.args.output / "manifest.json").exists())

    @unittest.skipIf(sys.platform == "win32", "Local symlink privileges required")
    def test_linked_output_and_image_leave_caller_targets_untouched(self):
        self.require_pixels()
        target = self.root / "untouched"
        self.args.output.symlink_to(target)
        with self.assertRaises(ValueError):
            resize.run(self.args)
        self.assertFalse(target.exists())
        self.args.output.unlink()
        source = self.image.read_bytes()
        linked = self.root / "caller.png"
        linked.write_bytes(source)
        self.image.unlink()
        self.image.symlink_to(linked)
        with self.assertRaises(ValueError):
            resize.run(self.args)
        self.assertEqual(source, linked.read_bytes())
        self.assertFalse((self.args.output / "manifest.json").exists())


if __name__ == "__main__":
    unittest.main()
