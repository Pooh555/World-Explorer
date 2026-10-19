"""Synthetic calibrated exports check quality-request admission without GPU/models."""

import copy
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

import worker_image_evaluation_test as fixtures

ROOT = Path(__file__).resolve().parents[2]
with patch.object(sys, "path", [str(ROOT / "tools"), *sys.path]):
    SPEC = importlib.util.spec_from_file_location(
        "packing_quality", ROOT / "tools/evaluate_packing_quality.py"
    )
    quality = importlib.util.module_from_spec(SPEC)
    SPEC.loader.exec_module(quality)


def sha(data):
    return hashlib.sha256(data).hexdigest()


class PackingQualityTests(unittest.TestCase):
    def setUp(self):
        helper = fixtures.ImageEvaluationTests()
        helper.setUp()
        self.addCleanup(helper.doCleanups)
        self.helper = helper
        self.root, self.fit, self.images = helper.root, helper.fit, helper.images
        self.config, self.result = helper.config, helper.result
        self.config.update(sh_degree=3, rasterize_mode="classic")
        self.config["dataset"][
            "world_transform"
        ] = "native COLMAP; canonical chunk applies Rx(pi)"
        for index, frame in enumerate(self.config["dataset"]["frames"]):
            frame["view"] = [
                [1.0000001, 0, 0, index + 1],
                [0, 1, 0, 2],
                [0, 0, 1, 3],
                [0, 0, 0, 1],
            ]
            frame["intrinsic"] = [[60, 0, 48], [0, 65, 40], [0, 0, 1]]
        ply = b"synthetic diagnostic fitted bytes"
        (self.fit / "model.ply").write_bytes(ply)
        self.result.update(ply="model.ply", ply_sha256=sha(ply))
        helper.commit_fit()
        self.scene = self.root / "scene"
        self.scene.mkdir()
        data = json.dumps(
            {
                "metadata": {
                    "source_kind": "video_reconstruction",
                    "fit_identity": self.result["identity"],
                    "input_sha256": sha(ply),
                    "capture_sha256": self.result["source_sha256"],
                }
            }
        ).encode()
        (self.scene / "provenance.json").write_bytes(data)
        (self.scene / "index.bin").write_bytes(b"synthetic index")
        self.manifest = {
            "provenance": "provenance.json",
            "provenance_sha256": sha(data),
            "index": "index.bin",
            "index_sha256": sha(b"synthetic index"),
        }
        (self.scene / "manifest.json").write_text(json.dumps(self.manifest))
        self.identity = {
            "fit_identity": self.result["identity"],
            "source_sha256": self.result["source_sha256"],
            "ply_sha256": sha(ply),
            "scene_manifest_sha256": sha((self.scene / "manifest.json").read_bytes()),
            "fit_input_sha256": sha((self.fit / "input_manifest.json").read_bytes()),
            "fit_result_sha256": sha((self.fit / "result.json").read_bytes()),
        }
        self.exports = {}
        self.roots = {"fit": self.fit, "scene": self.scene, "target": self.images}
        for profile, stride in (("fp32", 256), ("fp16_sh", 160)):
            root = self.root / profile
            root.mkdir()
            self.roots[profile] = root
            for source, dest in (
                ("input_manifest.json", "fit-input.json"),
                ("result.json", "fit-result.json"),
            ):
                (root / dest).write_bytes((self.fit / source).read_bytes())
            chunk = bytes(240 * 12)
            (root / "chunk.f32").write_bytes(chunk)
            views, images = [], []
            for index, frame in enumerate(self.config["dataset"]["frames"]):
                views.append(
                    {
                        "name": f"fit_index_{index}",
                        "fit_frame_index": index,
                        "fit_frame_name": frame["name"],
                        "right": [1.0000001, 0, 0],
                        "down": [0, -1, 0],
                        "forward": [0, 0, -1],
                        "position": [-(index + 1) / 1.0000001, 2, 3],
                        "intrinsics": [60, 65, 47.5, 39.5],
                        "near": 0.009999999776482582,
                        "far": 100000,
                    }
                )
                image = b"P6\n96 80\n255\n" + bytes([40, 80, 120]) * (96 * 80)
                name = f"fit_index_{index}.ppm"
                (root / name).write_bytes(image)
                images.append({"file": name, "sha256": sha(image)})
            fixture = {
                "schema": 2,
                "label": "real calibrated retained fit; no quality/performance certification",
                "identity": self.identity,
                "gpu_storage": profile,
                "splat_stride_bytes": stride,
                "splat_count": 12,
                "scene_content_sha256": "c" * 64,
                "width": 96,
                "height": 80,
                "row_format": "little-endian float32[60]: mean3,scale3,wxyz4,opacity,sh48,degree",
                "fit_frame_indices": list(range(7)),
                "views": views,
                "render_origin": views[0]["position"],
                "background": [0.045, 0.051, 0.063],
                "chunks": [{"file": "chunk.f32", "count": 12, "sha256": sha(chunk)}],
            }
            native = {
                "validation_errors": 0,
                "splat_count": 12,
                "fp32_compositing": True,
                "images": images,
            }
            self.exports[profile] = fixture, native
            self.save(profile)

    def save(self, profile):
        fixture, native = self.exports[profile]
        root = self.roots[profile]
        data = json.dumps(fixture).encode()
        (root / "fixture.json").write_bytes(data)
        native["fixture_sha256"] = sha(data)
        (root / "native.json").write_text(json.dumps(native))

    def prepare(self, margin=0):
        return quality.prepare(self.roots, self.helper.metric, margin)

    def test_all_available_views_bound_in_original_order_with_split_and_preservation(
        self,
    ):
        before = {p: p.read_bytes() for p in self.root.rglob("*") if p.is_file()}
        request = self.prepare()
        quality.validate(request)
        self.assertEqual(request["available_camera_count"], 7)
        self.assertEqual(
            [p["split"] for p in request["pairs"]],
            ["training"] * 3 + ["guard"] * 2 + ["holdout"] * 2,
        )
        self.assertFalse(request["q6_qualified"])
        self.assertFalse(request["fixed_100_view_corpus"])
        self.assertEqual(
            before, {p: p.read_bytes() for p in self.root.rglob("*") if p.is_file()}
        )

    def test_changed_target_render_fit_and_scene_are_rejected(self):
        request = self.prepare()
        for path in (
            self.images / "000001.png",
            self.roots["fp16_sh"] / "fit_index_6.ppm",
            self.fit / "model.ply",
            self.fit / "input_manifest.json",
            self.scene / "index.bin",
        ):
            with self.subTest(path=path):
                data = path.read_bytes()
                path.write_bytes(data[:-1] + bytes([data[-1] ^ 1]))
                try:
                    with self.assertRaises((ValueError, KeyError)):
                        quality.validate(request)
                finally:
                    path.write_bytes(data)

    def test_rehashed_wrong_camera_origin_intrinsic_and_scene_are_rejected(self):
        profile = "fp16_sh"
        original = copy.deepcopy(self.exports[profile])
        for mutate in (
            lambda f: f["views"][6]["position"].__setitem__(0, 0),
            lambda f: f["views"][1]["intrinsics"].__setitem__(2, 48),
            lambda f: f["views"][0].update(far=20),
            lambda f: f.update(render_origin=[0, 0, 0]),
            lambda f: f.update(scene_content_sha256="d" * 64),
            lambda f: f["identity"].update(fit_identity="a" * 64),
        ):
            self.exports[profile] = copy.deepcopy(original)
            mutate(self.exports[profile][0])
            self.save(profile)
            with self.assertRaises(ValueError):
                self.prepare()

    def test_missing_duplicate_and_reordered_cameras_are_rejected(self):
        original = copy.deepcopy(self.exports["fp16_sh"])
        for indices in (
            list(range(6)),
            [0, 1, 2, 3, 4, 5, 5],
            list(reversed(range(7))),
        ):
            self.exports["fp16_sh"] = copy.deepcopy(original)
            self.exports["fp16_sh"][0]["fit_frame_indices"] = indices
            self.save("fp16_sh")
            with self.assertRaises(ValueError):
                self.prepare()

    def test_wrong_profile_stride_validation_count_and_background_are_rejected(self):
        original = copy.deepcopy(self.exports["fp16_sh"])
        for key, value in (
            ("gpu_storage", "fp32"),
            ("splat_stride_bytes", 256),
            ("splat_count", 11),
            ("background", [0, 0, 0]),
        ):
            self.exports["fp16_sh"] = copy.deepcopy(original)
            self.exports["fp16_sh"][0][key] = value
            self.save("fp16_sh")
            with self.assertRaises(ValueError):
                self.prepare()
        self.exports["fp16_sh"] = copy.deepcopy(original)
        self.exports["fp16_sh"][1]["validation_errors"] = 1
        self.save("fp16_sh")
        with self.assertRaises(ValueError):
            self.prepare()

    def test_rehashed_chunk_short_readback_and_unsafe_path_are_rejected(self):
        fixture, native = self.exports["fp16_sh"]
        for data in (b"short", bytes(240 * 11)):
            (self.roots["fp16_sh"] / "chunk.f32").write_bytes(data)
            fixture["chunks"][0]["sha256"] = sha(data)
            self.save("fp16_sh")
            with self.assertRaises(ValueError):
                self.prepare()
        chunk = bytes(240 * 12)
        (self.roots["fp16_sh"] / "chunk.f32").write_bytes(chunk)
        fixture["chunks"][0]["sha256"] = sha(chunk)
        path = self.roots["fp16_sh"] / "fit_index_0.ppm"
        original = path.read_bytes()
        path.write_bytes(original[:-1])
        native["images"][0]["sha256"] = sha(original[:-1])
        self.save("fp16_sh")
        with self.assertRaises(ValueError):
            self.prepare()
        path.write_bytes(original)
        native["images"][0]["sha256"] = sha(original)
        native["images"][0]["file"] = "../fp32/fit_index_0.ppm"
        self.save("fp16_sh")
        with self.assertRaises(ValueError):
            self.prepare()

    def test_common_crop_bound_and_rehashed_request_edits_are_rejected(self):
        self.assertEqual(self.prepare(5)["pairs"][0]["crop"], [5, 5, 86, 70])
        for margin in (-1, True, 9, 8193):
            with self.assertRaises(ValueError):
                self.prepare(margin)
        original = self.prepare()
        for mutate in (
            lambda r: r["pairs"][0].update(crop=[0, 0, 90, 70]),
            lambda r: r.update(q6_qualified=True),
            lambda r: r.update(available_camera_count=100),
            lambda r: r["implementation_sha256"].update(
                **{"evaluate_packing_quality.py": "e" * 64}
            ),
        ):
            request = copy.deepcopy(original)
            mutate(request)
            with self.assertRaises(ValueError):
                quality.validate(request)

    def test_frozen_numeric_camera_types_and_nonfinite_matrices_are_rejected(self):
        fixture, _ = self.exports["fp16_sh"]
        fixture["views"][1]["fit_frame_index"] = True
        self.save("fp16_sh")
        with self.assertRaises(ValueError):
            self.prepare()
        frame = copy.deepcopy(self.config["dataset"]["frames"][0])
        frame["view"][0][0] = math.nan
        with self.assertRaises(ValueError):
            quality.camera_matches(fixture["views"][0], frame)

    def test_symlink_escape_is_rejected(self):
        path = self.roots["fp16_sh"] / "fit_index_0.ppm"
        data = path.read_bytes()
        outside = self.root / "outside.ppm"
        outside.write_bytes(data)
        path.unlink()
        try:
            path.symlink_to(outside)
        except OSError:
            self.skipTest("symlinks unavailable on this host")
        with self.assertRaises(ValueError):
            self.prepare()

    def test_quality_loss_sign_limits_and_infinite_handling(self):
        baseline = {"psnr_db": 30.0, "lpips": 0.1}
        result = quality.quality_delta(baseline, {"psnr_db": 29.9, "lpips": 0.105})
        self.assertAlmostEqual(result["psnr_loss_db"], 0.1)
        self.assertAlmostEqual(result["lpips_increase"], 0.005)
        self.assertTrue(result["numeric_limits_met"])
        for packed in (
            {"psnr_db": 29.7, "lpips": 0.1},
            {"psnr_db": 30.0, "lpips": 0.111},
        ):
            self.assertFalse(
                quality.quality_delta(baseline, packed)["numeric_limits_met"]
            )
        self.assertTrue(
            quality.quality_delta(baseline, {"psnr_db": 31.0, "lpips": 0.09})[
                "numeric_limits_met"
            ]
        )
        for left, right in ((None, None), (None, 30.0), (30.0, None)):
            result = quality.quality_delta(
                {"psnr_db": left, "lpips": 0.1}, {"psnr_db": right, "lpips": 0.1}
            )
            self.assertIsNone(result["psnr_loss_db"])
            self.assertIsNone(result["numeric_limits_met"])
        with self.assertRaises(ValueError):
            quality.quality_delta(baseline, {"psnr_db": math.nan, "lpips": 0.1})


if __name__ == "__main__":
    unittest.main()
