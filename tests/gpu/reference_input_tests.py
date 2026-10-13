"""CPU admission checks in the pinned reference environment; no GPU required."""
import copy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

import numpy as np

from compare_reference import load_real, sha256


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
                  "rasterize_mode": "classic", "dataset": {"frames": [frame] * 3,
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
                "intrinsics": [40, 40, 31.5, 15.5], "fit_frame_name": "fixture.png"}
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
                     "views": [dict(copy.deepcopy(view), name=name, fit_frame_index=index)
                               for index, name in enumerate(["fit_first", "fit_middle", "fit_last"])]}

    def load(self):
        return load_real(self.root, copy.deepcopy(self.data))

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


if __name__ == "__main__":
    unittest.main()
