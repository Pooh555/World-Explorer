"""CPU admission checks in the pinned reference environment; no GPU required."""
import copy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

import numpy as np

from compare_reference import load_real, parse_fit_indices, sha256


class real_reference_input_tests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        # Fabricated metadata exercises admission only, never real capture quality.
        frame = {"width": 64, "height": 32, "name": "fixture.png",
                 "view": np.eye(4).tolist(),
                 "intrinsic": [[40, 0, 32], [0, 40, 16], [0, 0, 1]]}
        config = {"source_sha256": "1" * 64, "color_domain": "srgb", "sh_degree": 3,
                  "rasterize_mode": "classic", "dataset": {"frames": [
                      dict(copy.deepcopy(frame), name=f"fixture_{index}.png") for index in range(3)],
                  "world_transform": "native COLMAP; canonical chunk applies Rx(pi)"}}
        fit_identity = hashlib.sha256(json.dumps(config, sort_keys=True).encode()).hexdigest()
        result = {"identity": fit_identity, "source_sha256": "1" * 64,
                  "ply_sha256": "2" * 64, "status": "complete", "splats": 1}
        for name, value in [("fit-input.json", config), ("fit-result.json", result)]:
            (self.root / name).write_text(json.dumps(value))
        rows = np.zeros((1, 60), dtype="<f4")
        rows[0, 3:6] = 1
        rows[0, 6] = 1
        rows[0, 10] = .5
        rows[0, 59] = 3
        rows.tofile(self.root / "chunk.f32")
        view = {"position": [0., 0., 0.], "right": [1., 0., 0.],
                "down": [0., -1., 0.], "forward": [0., 0., -1.],
                "intrinsics": [40, 40, 31.5, 15.5]}
        self.data = {"schema": 2, "label": "real calibrated profile admission fixture",
                     "row_format": "little-endian float32[60]: mean3,scale3,wxyz4,opacity,sh48,degree",
                     "width": 64, "height": 32, "splat_count": 1,
                     "render_origin": [0., 0., 0.],
                     "identity": {"fit_identity": fit_identity, "source_sha256": "1" * 64,
                                  "ply_sha256": "2" * 64,
                                  "fit_input_sha256": sha256(self.root / "fit-input.json"),
                                  "fit_result_sha256": sha256(self.root / "fit-result.json")},
                     "chunks": [{"file": "chunk.f32", "count": 1,
                                 "rotation": [1, 0, 0, 0], "translation": [0, 0, 0], "scale": 1,
                                 "sha256": sha256(self.root / "chunk.f32")}],
                     "views": [dict(copy.deepcopy(view), name=name, fit_frame_index=index,
                                    fit_frame_name=f"fixture_{index}.png")
                               for index, name in enumerate(["fit_first", "fit_middle", "fit_last"])]}

    def load(self, selection=None):
        return load_real(self.root, copy.deepcopy(self.data), selection)

    def select(self, indices):
        views = self.data["views"]
        self.data["fit_frame_indices"] = indices.copy()
        self.data["views"] = [dict(copy.deepcopy(views[index]), name=f"fit_index_{index}") for index in indices]

    def test_full_payload(self):
        self.assertEqual(self.load(), 1)

    def test_truncated_payload(self):
        (self.root / "chunk.f32").write_bytes(b"short")
        with self.assertRaisesRegex(ValueError, "size/path"):
            self.load()

    def test_checksum(self):
        self.data["chunks"][0]["sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "checksum"):
            self.load()

    def test_path_escape(self):
        self.data["chunks"][0]["file"] = "../chunk.f32"
        with self.assertRaisesRegex(ValueError, "asset path"):
            self.load()

    def test_count_overflow(self):
        self.data["chunks"][0]["count"] = 1000001
        with self.assertRaisesRegex(ValueError, "count exceeds"):
            self.load()

    def test_nonfinite_payload(self):
        rows = np.fromfile(self.root / "chunk.f32", dtype="<f4")
        rows[0] = np.nan
        rows.tofile(self.root / "chunk.f32")
        self.data["chunks"][0]["sha256"] = sha256(self.root / "chunk.f32")
        with self.assertRaisesRegex(ValueError, "splat values"):
            self.load()

    def test_changed_original_fit(self):
        self.data["identity"]["fit_identity"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "configuration identity"):
            self.load()

    def test_changed_camera(self):
        self.data["views"][1]["intrinsics"][0] = 41
        with self.assertRaisesRegex(ValueError, "camera differs"):
            self.load()

    def test_changed_origin(self):
        self.data["render_origin"] = [1, 0, 0]
        with self.assertRaisesRegex(ValueError, "render origin"):
            self.load()

    def test_explicit_order_and_complete_selection(self):
        self.select([2, 0, 1])
        self.assertEqual(self.load("2,0,1"), 1)

    def test_single_selection(self):
        self.select([1])
        self.assertEqual(self.load("1"), 1)

    def test_selection_uses_original_camera_and_first_selected_origin(self):
        config = json.loads((self.root / "fit-input.json").read_text())
        config["dataset"]["frames"][2]["view"][0][3] = 2.5
        config["dataset"]["frames"][2]["intrinsic"][0][0] = 55
        identity = hashlib.sha256(json.dumps(config, sort_keys=True).encode()).hexdigest()
        result = json.loads((self.root / "fit-result.json").read_text())
        result["identity"] = identity
        for name, value in [("fit-input.json", config), ("fit-result.json", result)]:
            (self.root / name).write_text(json.dumps(value))
        self.data["identity"].update(fit_identity=identity,
            fit_input_sha256=sha256(self.root / "fit-input.json"),
            fit_result_sha256=sha256(self.root / "fit-result.json"))
        self.data["views"][2]["position"] = [-2.5, 0., 0.]
        self.data["views"][2]["intrinsics"][0] = 55
        self.select([2, 0])
        self.data["render_origin"] = [-2.5, 0., 0.]
        self.assertEqual(self.load("2,0"), 1)
        self.data["render_origin"] = [0., 0., 0.]
        with self.assertRaisesRegex(ValueError, "render origin"):
            self.load("2,0")

    def test_explicit_selection_requires_matching_request(self):
        self.select([0, 2])
        for request in [None, "2,0", "0", "0,1,2"]:
            with self.subTest(request=request), self.assertRaisesRegex(ValueError, "selection"):
                self.load(request)

    def test_explicit_request_rejects_default_fixture(self):
        with self.assertRaisesRegex(ValueError, "selection"):
            self.load("0,1,2")

    def test_selected_view_correspondence(self):
        self.select([2, 0])
        original = copy.deepcopy(self.data)
        for key, value in [("name", "../fit_index_2"), ("fit_frame_index", 0),
                           ("fit_frame_index", False), ("fit_frame_name", "fixture_0.png")]:
            with self.subTest(key=key, value=value):
                self.data = copy.deepcopy(original)
                self.data["views"][0][key] = value
                with self.assertRaisesRegex(ValueError, "view selection"):
                    self.load("2,0")

    def test_selected_view_count_and_recorded_type(self):
        self.select([0, 1])
        self.data["views"].pop()
        with self.assertRaisesRegex(ValueError, "view budget"):
            self.load("0,1")
        self.select([0])
        self.data["fit_frame_indices"] = [False]
        with self.assertRaisesRegex(ValueError, "selection"):
            self.load("0")

    def test_selection_parser_boundaries(self):
        self.assertEqual(parse_fit_indices("79,0,40", 80), [79, 0, 40])
        self.assertEqual(parse_fit_indices(",".join(map(str, range(120))), 120), list(range(120)))
        self.assertEqual(parse_fit_indices("0", 3), [0])

    def test_selection_parser_rejects_invalid_and_duplicate_indices(self):
        for value in ["", ",", "0,", ",0", "0,,1", "0,0", "1,0,1", "-1", "+1", "01", "000",
                      "1.0", "true", " 1", "1 ", "1, 2", "80", "120", "999", "1000",
                      "18446744073709551616", "0\n", "0" * 480, "0\0,1", None, [0], 0]:
            with self.subTest(value=value), self.assertRaises(ValueError):
                parse_fit_indices(value, 80)
        for count in [0, 2, 121, True]:
            with self.subTest(count=count), self.assertRaises(ValueError):
                parse_fit_indices("0", count)


if __name__ == "__main__":
    unittest.main()
