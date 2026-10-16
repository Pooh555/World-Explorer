"""Synthetic readbacks exercise packing-evaluation input boundaries without models."""

import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
with patch.object(sys, "path", [str(ROOT / "tools"), *sys.path]):
    SPEC = importlib.util.spec_from_file_location("packing_evaluation", ROOT / "tools/evaluate_packing.py")
    evaluation = importlib.util.module_from_spec(SPEC)
    SPEC.loader.exec_module(evaluation)


class PackingEvaluationTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.path = self.root / "comparison.json"
        self.path_record = {"schema_version": 1, "profile": "source_view_local_loop_v1",
                            "scene_content_sha256": "1" * 64, "width": 63, "height": 63,
                            "views": [{"index": index} for index in range(100)]}
        self.report = {"schema_version": 1, "status": "complete",
                       "scope": "local retained-scene GPU packing distortion",
                       "q6_qualified": False, "ground_truth_available": False,
                       "geometry_quantization": "identical FP32 geometry bytes in both GPU profiles",
                       "color_domain": "display-encoded RGB8 in [0,1]; full image; no fitted correction",
                       "sh_order": "gsplat real basis; coefficient-major RGB; degree per splat",
                       "scene_content_sha256": "1" * 64, "manifest_sha256": "2" * 64,
                       "scene_splats": 10, "width": 63, "height": 63, "runs": []}
        self.image = b"P6\n63 63\n255\n" + bytes([30, 60, 90]) * (63 * 63)
        for profile, stride in (("fp32", 256), ("fp16_sh", 160)):
            images = []
            for index in range(100):
                file = f"{profile}-{index}.ppm"
                (self.root / file).write_bytes(self.image)
                images.append({"index": index, "file": file,
                               "sha256": hashlib.sha256(self.image).hexdigest()})
            self.report["runs"].append({"profile": profile, "splat_stride_bytes": stride,
                                        "validation_errors": 0, "images": images})
        self.save_path()

    def save_path(self):
        data = json.dumps(self.path_record).encode()
        (self.root / "path.json").write_bytes(data)
        self.report["path_sha256"] = hashlib.sha256(data).hexdigest()

    def read(self):
        self.path.write_text(json.dumps(self.report))
        return evaluation.comparison_inputs(self.path)

    def test_all_pairs_checked_in_fixed_order_without_writing_inputs(self):
        self.read()
        before = {p.name: p.read_bytes() for p in self.root.iterdir()}
        report, digest, width, height, pairs = self.read()
        self.assertEqual((width, height, len(pairs)), (63, 63, 100))
        self.assertEqual(pairs[-1][1][0].name, "fp16_sh-99.ppm")
        self.assertFalse(report["q6_qualified"])
        self.assertEqual(len(digest), 64)
        self.assertEqual(before, {p.name: p.read_bytes() for p in self.root.iterdir()})

    def test_readback_mutation_rejected(self):
        selected = self.root / "fp16_sh-99.ppm"
        selected.write_bytes(self.image[:-1] + b"\0")
        with self.assertRaises(ValueError):
            self.read()

    def test_rehashed_malformed_ppm_is_rejected(self):
        for data in (self.image[:-1], self.image + b"\0", self.image.replace(b"255", b"256", 1)):
            with self.subTest(bytes=len(data)):
                (self.root / "fp16_sh-99.ppm").write_bytes(data)
                self.report["runs"][1]["images"][99]["sha256"] = hashlib.sha256(data).hexdigest()
                with self.assertRaises(ValueError):
                    self.read()

    def test_missing_duplicate_and_reordered_views_are_rejected(self):
        original = copy.deepcopy(self.report)
        for indices in (list(range(99)), list(range(99)) + [98], list(reversed(range(100)))):
            self.report = copy.deepcopy(original)
            self.report["runs"][1]["images"] = [original["runs"][1]["images"][i] for i in indices]
            with self.assertRaises(ValueError):
                self.read()

    def test_profile_errors_and_qualification_claims_are_rejected(self):
        original = copy.deepcopy(self.report)
        for mutate in (lambda r: r.update(q6_qualified=True),
                       lambda r: r.update(ground_truth_available=True),
                       lambda r: r.update(width=8192),
                       lambda r: r["runs"][1].update(splat_stride_bytes=256),
                       lambda r: r["runs"][1].update(validation_errors=1),
                       lambda r: r.update(runs=[r["runs"][0]]),
                       lambda r: r["runs"][1]["images"][0].update(file="../fp16_sh-0.ppm")):
            self.report = copy.deepcopy(original)
            mutate(self.report)
            with self.assertRaises(ValueError):
                self.read()

    def test_changed_frozen_path_and_rehashed_wrong_selection_are_rejected(self):
        (self.root / "path.json").write_text("{}")
        with self.assertRaises(ValueError):
            self.read()
        self.path_record["views"][99]["index"] = 98
        self.save_path()
        with self.assertRaises(ValueError):
            self.read()


if __name__ == "__main__":
    unittest.main()
