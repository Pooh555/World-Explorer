"""Synthetic PNG/fit fixtures for read-only evaluation admission and aggregation."""

import copy
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
import zlib

ROOT = Path(__file__).resolve().parents[2]
with patch.object(sys, "path", [str(ROOT / "tools"), *sys.path]):
    SPEC = importlib.util.spec_from_file_location("pair_evaluation", ROOT / "tools/evaluate_image_pairs.py")
    evaluation = importlib.util.module_from_spec(SPEC)
    SPEC.loader.exec_module(evaluation)
    PREP_SPEC = importlib.util.spec_from_file_location("holdout_prepare", ROOT / "tools/prepare_holdout_evaluation.py")
    prepare = importlib.util.module_from_spec(PREP_SPEC)
    PREP_SPEC.loader.exec_module(prepare)


def png(width=96, height=80, depth=8, color=2):
    def chunk(name, data):
        return struct.pack(">I", len(data)) + name + data + struct.pack(">I", zlib.crc32(name + data))
    header = struct.pack(">IIBBBBB", width, height, depth, color, 0, 0, 0)
    pixels = (b"\0" + bytes([40, 80, 120]) * min(width, 96)) * min(height, 80)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(pixels)) + chunk(b"IEND", b"")


class ImageEvaluationTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.fit = self.root / "fit"
        self.renders = self.fit / "references"
        self.images = self.root / "images"
        self.renders.mkdir(parents=True)
        self.images.mkdir()
        frames = []
        for index in range(7):
            name = f"{index + 1:06d}.png"
            data = png()
            (self.images / name).write_bytes(data)
            (self.renders / name).write_bytes(data)
            frames.append({"id": index + 1, "name": name, "sha256": hashlib.sha256(data).hexdigest(),
                           "width": 96, "height": 80, "original_width": 96, "original_height": 80})
        self.config = {"recipe": "local_gsplat_v1", "color_domain": "srgb", "source_sha256": "1" * 64,
                       "steps": 7000, "max_image_dimension": 96,
                       "metric_crop": "five-pixel border for SSIM; full image PSNR",
                       "dataset": {"frames": frames, "training_indices": [0, 1, 2],
                                   "guard_indices": [3, 4], "holdout_indices": [5, 6],
                                   "initial_colors": "training observations only",
                                   "resize": "bicubic; half-pixel coordinates scaled separately in x/y"}}
        self.result = {"schema_version": 1, "recipe": "local_gsplat_v1", "status": "complete",
                       "source_sha256": "1" * 64, "steps": 7000, "splats": 12,
                       "holdout": [{"frame": "000006.png"}, {"frame": "000007.png"}]}
        self.commit_fit()
        self.metric = self.root / "metric.json"
        self.metric.write_text(json.dumps({"schema_version": 1, "identity_ready": True}))

    def commit_fit(self):
        self.result["identity"] = hashlib.sha256(json.dumps(self.config, sort_keys=True).encode()).hexdigest()
        (self.fit / "input_manifest.json").write_text(json.dumps(self.config))
        (self.fit / "result.json").write_text(json.dumps(self.result))

    def request(self, margin=0):
        return prepare.prepare(self.fit, self.images, self.metric, "fixture", margin)

    def test_all_holdouts_are_pinned_in_order_with_immutable_fit_and_explicit_domain(self):
        before = {p: p.read_bytes() for p in self.root.rglob("*") if p.is_file()}
        request = self.request()
        self.assertEqual([p["frame"] for p in request["pairs"]], ["000006.png", "000007.png"])
        self.assertEqual(request["scope"], evaluation.SCOPE)
        self.assertFalse(request["domain"]["observed_region_verified"])
        self.assertEqual(request["provenance"]["training_frames"], 3)
        self.assertEqual(request["provenance"]["guard_frames"], 2)
        self.assertEqual(request["pairs"][0]["crop"], [0, 0, 96, 80])
        evaluation.verify_images(request, evaluation.validate_request(request))
        self.assertEqual(before, {p: p.read_bytes() for p in self.root.rglob("*") if p.is_file()})

    def test_common_crop_is_explicit_and_small_or_negative_crop_is_rejected(self):
        self.assertEqual(self.request(5)["pairs"][0]["crop"], [5, 5, 86, 70])
        for margin in [-1, True, 9, 8193]:
            with self.subTest(margin=margin), self.assertRaises(ValueError):
                self.request(margin)

    def test_complete_fit_identity_and_color_resize_conventions_are_required(self):
        config = copy.deepcopy(self.config)
        for key, value in [("color_domain", "linear"), ("recipe", "unknown"), ("max_image_dimension", 63), ("steps", True), ("steps", 7500)]:
            with self.subTest(key=key):
                self.config = copy.deepcopy(config)
                self.config[key] = value
                self.commit_fit()
                with self.assertRaises(ValueError):
                    self.request()
        self.config = config
        self.commit_fit()
        self.result["identity"] = "0" * 64
        (self.fit / "result.json").write_text(json.dumps(self.result))
        with self.assertRaisesRegex(ValueError, "identity"):
            self.request()

    def test_train_guard_holdout_changes_are_rejected_even_with_a_recomputed_fit_digest(self):
        original = copy.deepcopy(self.config)
        for key, values in [("holdout_indices", [2, 6]), ("guard_indices", [3, True]),
                            ("training_indices", [0, 1, 2, 5])]:
            with self.subTest(key=key):
                self.config = copy.deepcopy(original)
                self.config["dataset"][key] = values
                self.commit_fit()
                with self.assertRaisesRegex(ValueError, "partition"):
                    self.request()

    def test_changed_source_missing_render_and_changed_fit_provenance_cannot_be_scored(self):
        request = self.request()
        roots = evaluation.validate_request(request)
        target = self.images / "000006.png"
        original = target.read_bytes()
        target.write_bytes(original + b"changed")
        with self.assertRaisesRegex(ValueError, "checksum"):
            evaluation.verify_images(request, roots)
        target.write_bytes(original)
        (self.renders / "000007.png").unlink()
        with self.assertRaises(OSError):
            evaluation.verify_images(request, roots)
        (self.renders / "000007.png").write_bytes(png())
        (self.fit / "result.json").write_text("{}")
        with self.assertRaises(ValueError):
            evaluation.validate_request(request)

    def test_evaluation_selection_rejects_subset_reorder_duplicates_and_training_leakage(self):
        original = self.request()
        subsets = [original["pairs"][:1], list(reversed(original["pairs"])),
                   [original["pairs"][0], original["pairs"][0]]]
        leaked = copy.deepcopy(original["pairs"])
        leaked[0]["frame"] = "000001.png"
        subsets.append(leaked)
        for pairs in subsets:
            request = copy.deepcopy(original)
            request["pairs"] = pairs
            with self.subTest(pairs=pairs), self.assertRaises(ValueError):
                evaluation.validate_request(request)

    def test_mask_color_and_unpinned_metric_selection_cannot_silently_change_scope(self):
        for key, value in [("mask", "arbitrary"), ("color", "linear"), ("test_exposure_fitting", True)]:
            request = self.request()
            request["domain"] = dict(request["domain"])
            request["domain"][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                evaluation.validate_request(request)
        self.metric.write_text('{"schema_version":1,"identity_ready":false}')
        with self.assertRaises(ValueError):
            self.request()

    def test_paths_dimensions_digests_and_encoded_budget_are_checked_before_decode(self):
        original = self.request()
        for field, value in [("path", "../outside.png"), ("path", "C:outside.png"),
                             ("path", "nested\\outside.png"), ("path", "/absolute.png"),
                             ("width", True), ("width", 8193), ("sha256", "bad"),
                             ("bytes", evaluation.MAX_ENCODED_BYTES + 1)]:
            request = copy.deepcopy(original)
            request["pairs"][0]["render"][field] = value
            with self.subTest(field=field, value=value), self.assertRaises((ValueError, OSError)):
                evaluation.validate_request(request)
        with patch.object(evaluation, "MAX_PAIRS", 1), self.assertRaises(ValueError):
            evaluation.validate_request(original)
        with patch.object(evaluation, "MAX_TOTAL_ENCODED_BYTES", 1), self.assertRaises(ValueError):
            evaluation.validate_request(original)

    def test_png_headers_forbid_implicit_rgba_palette_hdr_and_decompression_bombs(self):
        for data in [b"not PNG", png(depth=16), png(color=6), png(color=3), png(8192, 8192)]:
            with self.subTest(length=len(data)), self.assertRaises(ValueError):
                evaluation.png_dimensions(data)
        header = bytearray(png())
        header[29] ^= 1
        with self.assertRaisesRegex(ValueError, "checksum"):
            evaluation.png_dimensions(header)

    def test_file_mutation_during_the_read_cannot_produce_a_mixed_identity(self):
        selected = self.images / "000006.png"
        original_open = Path.open
        def mutate_on_read(path, *args, **kwargs):
            if path == selected:
                with original_open(path, "ab") as stream:
                    stream.write(b"changed after stat")
            return original_open(path, *args, **kwargs)
        with patch.object(Path, "open", mutate_on_read), self.assertRaisesRegex(ValueError, "changed"):
            evaluation.read_image_bytes(selected)

    def test_psnr_and_scene_means_use_per_image_db_and_equal_scene_weights(self):
        rows = []
        for scene, mse, ssim in [("a", 0.01, 0.2), ("a", 0.001, 0.4), ("b", 0.0001, 0.9)]:
            rows.append({"scene": scene, **evaluation.psnr_from_mse(mse), "ssim": ssim, "lpips": 1 - ssim})
        result = evaluation.aggregate(rows)
        self.assertEqual(result["per_scene"][0]["psnr_db"], 25)
        self.assertEqual(result["equal_weight_scene_mean"]["psnr_db"], 32.5)
        self.assertAlmostEqual(result["equal_weight_scene_mean"]["ssim"], 0.6)
        self.assertNotAlmostEqual(result["per_scene"][0]["psnr_db"], -10 * math.log10(0.0055))

    def test_identical_image_psnr_is_infinite_without_a_fitted_cap_or_exclusion(self):
        row = {"scene": "a", **evaluation.psnr_from_mse(0), "ssim": 1, "lpips": 0}
        self.assertIsNone(row["psnr_db"])
        self.assertEqual(row["psnr_status"], "IDENTICAL_INFINITE")
        result = evaluation.aggregate([row])
        self.assertIsNone(result["equal_weight_scene_mean"]["psnr_db"])
        json.dumps(result, allow_nan=False)
        for mse in [-1, float("nan"), float("inf")]:
            with self.assertRaises(ValueError):
                evaluation.psnr_from_mse(mse)
        row["lpips"] = float("nan")
        with self.assertRaises(ValueError):
            evaluation.aggregate([row])

    def test_duplicate_nonfinite_json_and_invalid_output_are_rejected_without_publication(self):
        path = self.root / "invalid.json"
        for text in ['[]', '{"a":1,"a":2}', '{"a":NaN}']:
            path.write_text(text)
            with self.assertRaises(ValueError):
                evaluation.read_json(path)
        output = self.root / "outside.json"
        argv = ["prepare_holdout_evaluation.py", "--fit", str(self.fit), "--images", str(self.images),
                "--metric-manifest", str(self.metric), "--scene", "fixture", "--crop-margin", "0",
                "--mask", "none", "--output", str(output)]
        with patch.object(sys, "argv", argv):
            self.assertEqual(prepare.main(), 2)
        self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
