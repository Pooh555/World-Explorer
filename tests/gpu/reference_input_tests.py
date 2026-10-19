"""CPU admission checks in the pinned reference environment; no GPU required."""

import copy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

import numpy as np

from compare_reference import (
    rebasing_profile,
    load_rebasing,
    load_rebasing_baseline,
    image_comparison,
    MIN_PSNR,
    MAX_ERROR_LEVELS,
    CLIPPING_LABEL,
    clipping_regions,
    load_clipping,
    load_real,
    parse_fit_indices,
    sha256,
)


class real_reference_input_tests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        # Fabricated metadata exercises admission only, never real capture quality.
        frame = {
            "width": 64,
            "height": 32,
            "name": "fixture.png",
            "view": np.eye(4).tolist(),
            "intrinsic": [[40, 0, 32], [0, 40, 16], [0, 0, 1]],
        }
        config = {
            "source_sha256": "1" * 64,
            "color_domain": "srgb",
            "sh_degree": 3,
            "rasterize_mode": "classic",
            "dataset": {
                "frames": [
                    dict(copy.deepcopy(frame), name=f"fixture_{index}.png")
                    for index in range(3)
                ],
                "world_transform": "native COLMAP; canonical chunk applies Rx(pi)",
            },
        }
        fit_identity = hashlib.sha256(
            json.dumps(config, sort_keys=True).encode()
        ).hexdigest()
        result = {
            "identity": fit_identity,
            "source_sha256": "1" * 64,
            "ply_sha256": "2" * 64,
            "status": "complete",
            "splats": 1,
        }
        for name, value in [("fit-input.json", config), ("fit-result.json", result)]:
            (self.root / name).write_text(json.dumps(value))
        rows = np.zeros((1, 60), dtype="<f4")
        rows[0, 3:6] = 1
        rows[0, 6] = 1
        rows[0, 10] = 0.5
        rows[0, 59] = 3
        rows.tofile(self.root / "chunk.f32")
        view = {
            "position": [0.0, 0.0, 0.0],
            "right": [1.0, 0.0, 0.0],
            "down": [0.0, -1.0, 0.0],
            "forward": [0.0, 0.0, -1.0],
            "intrinsics": [40, 40, 31.5, 15.5],
        }
        self.data = {
            "schema": 2,
            "label": "real calibrated profile admission fixture",
            "row_format": "little-endian float32[60]: mean3,scale3,wxyz4,opacity,sh48,degree",
            "width": 64,
            "height": 32,
            "splat_count": 1,
            "render_origin": [0.0, 0.0, 0.0],
            "identity": {
                "fit_identity": fit_identity,
                "source_sha256": "1" * 64,
                "ply_sha256": "2" * 64,
                "fit_input_sha256": sha256(self.root / "fit-input.json"),
                "fit_result_sha256": sha256(self.root / "fit-result.json"),
            },
            "chunks": [
                {
                    "file": "chunk.f32",
                    "count": 1,
                    "rotation": [1, 0, 0, 0],
                    "translation": [0, 0, 0],
                    "scale": 1,
                    "sha256": sha256(self.root / "chunk.f32"),
                }
            ],
            "views": [
                dict(
                    copy.deepcopy(view),
                    name=name,
                    fit_frame_index=index,
                    fit_frame_name=f"fixture_{index}.png",
                )
                for index, name in enumerate(["fit_first", "fit_middle", "fit_last"])
            ],
        }

    def load(self, selection=None):
        return load_real(self.root, copy.deepcopy(self.data), selection)

    def select(self, indices):
        views = self.data["views"]
        self.data["fit_frame_indices"] = indices.copy()
        self.data["views"] = [
            dict(copy.deepcopy(views[index]), name=f"fit_index_{index}")
            for index in indices
        ]

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
        identity = hashlib.sha256(
            json.dumps(config, sort_keys=True).encode()
        ).hexdigest()
        result = json.loads((self.root / "fit-result.json").read_text())
        result["identity"] = identity
        for name, value in [("fit-input.json", config), ("fit-result.json", result)]:
            (self.root / name).write_text(json.dumps(value))
        self.data["identity"].update(
            fit_identity=identity,
            fit_input_sha256=sha256(self.root / "fit-input.json"),
            fit_result_sha256=sha256(self.root / "fit-result.json"),
        )
        self.data["views"][2]["position"] = [-2.5, 0.0, 0.0]
        self.data["views"][2]["intrinsics"][0] = 55
        self.select([2, 0])
        self.data["render_origin"] = [-2.5, 0.0, 0.0]
        self.assertEqual(self.load("2,0"), 1)
        self.data["render_origin"] = [0.0, 0.0, 0.0]
        with self.assertRaisesRegex(ValueError, "render origin"):
            self.load("2,0")

    def test_explicit_selection_requires_matching_request(self):
        self.select([0, 2])
        for request in [None, "2,0", "0", "0,1,2"]:
            with (
                self.subTest(request=request),
                self.assertRaisesRegex(ValueError, "selection"),
            ):
                self.load(request)

    def test_explicit_request_rejects_default_fixture(self):
        with self.assertRaisesRegex(ValueError, "selection"):
            self.load("0,1,2")

    def test_selected_view_correspondence(self):
        self.select([2, 0])
        original = copy.deepcopy(self.data)
        for key, value in [
            ("name", "../fit_index_2"),
            ("fit_frame_index", 0),
            ("fit_frame_index", False),
            ("fit_frame_name", "fixture_0.png"),
        ]:
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
        self.assertEqual(
            parse_fit_indices(",".join(map(str, range(120))), 120), list(range(120))
        )
        self.assertEqual(parse_fit_indices("0", 3), [0])

    def test_selection_parser_rejects_invalid_and_duplicate_indices(self):
        for value in [
            "",
            ",",
            "0,",
            ",0",
            "0,,1",
            "0,0",
            "1,0,1",
            "-1",
            "+1",
            "01",
            "000",
            "1.0",
            "true",
            " 1",
            "1 ",
            "1, 2",
            "80",
            "120",
            "999",
            "1000",
            "18446744073709551616",
            "0\n",
            "0" * 480,
            "0\0,1",
            None,
            [0],
            0,
        ]:
            with self.subTest(value=value), self.assertRaises(ValueError):
                parse_fit_indices(value, 80)
        for count in [0, 2, 121, True]:
            with self.subTest(count=count), self.assertRaises(ValueError):
                parse_fit_indices("0", count)


class clipping_boundary_tests(unittest.TestCase):
    def setUp(self):
        bits = np.array(
            [0x3F7FFFFF, 0x3F800000, 0x3F800001, 0x40FFFFFF, 0x41000000, 0x41000001],
            dtype=np.uint32,
        )
        splats = []
        for index, depth in enumerate(bits.view(np.float32)):
            color = np.array(
                [0.85, 0.35, 0.15] if index < 3 else [0.15, 0.45, 0.85],
                dtype=np.float32,
            )
            sh = np.zeros(48, dtype=np.float32)
            sh[:3] = (color - np.float32(0.5)) / np.float32(0.2820947918)
            splats.append(
                {
                    "mean": [
                        float(depth * ((index % 3) - 1) * 0.5),
                        float(depth * (0.125 if index < 3 else -0.125)),
                        -float(depth),
                    ],
                    "scale": [float(depth / np.float32(64))] * 3,
                    "rotation": [1, 0, 0, 0],
                    "opacity": float(np.float32(0.8)),
                    "sh_degree": 3,
                    "sh": sh.tolist(),
                }
            )
        self.data = {
            "schema": 1,
            "label": CLIPPING_LABEL,
            "width": 1280,
            "height": 720,
            "background": [0.045, 0.051, 0.063],
            "chunks": [
                {
                    "rotation": [1, 0, 0, 0],
                    "translation": [0, 0, 0],
                    "scale": 1,
                    "splats": splats,
                }
            ],
            "views": [
                {
                    "name": "clipping_boundaries",
                    "position": [0, 0, 0],
                    "right": [1, 0, 0],
                    "down": [0, -1, 0],
                    "forward": [0, 0, -1],
                    "near": 1.0,
                    "far": 8.0,
                    "intrinsics": [900, 840, 639.5, 359.5],
                }
            ],
        }
        self.background = np.rint(np.array(self.data["background"]) * 255).astype(
            np.uint8
        )

    def test_complete_fixed_profile(self):
        self.assertEqual(load_clipping(copy.deepcopy(self.data)), 6)

    def test_profile_plane_camera_and_count_mutations(self):
        for field, value in [
            ("near", 0.5),
            ("near", True),
            ("far", 9),
            ("position", [0, 0, 6]),
            ("name", "../clipping_boundaries"),
            ("intrinsics", [901, 840, 639.5, 359.5]),
        ]:
            data = copy.deepcopy(self.data)
            data["views"][0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                load_clipping(data)
        for count in [0, 5]:
            data = copy.deepcopy(self.data)
            data["chunks"][0]["splats"] = data["chunks"][0]["splats"][:count]
            with self.subTest(count=count), self.assertRaises(ValueError):
                load_clipping(data)

    def test_profile_payload_and_transform_mutations(self):
        for field, value in [
            ("mean", [0, 0, -1]),
            ("mean", [0, 0, float("inf")]),
            ("scale", [0, 0, 0]),
            ("opacity", 0.7),
            ("sh_degree", 0),
            ("sh", [float("nan")] * 48),
        ]:
            data = copy.deepcopy(self.data)
            data["chunks"][0]["splats"][0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                load_clipping(data)
        data = copy.deepcopy(self.data)
        data["chunks"][0]["translation"] = [1, 0, 0]
        with self.assertRaises(ValueError):
            load_clipping(data)

    def test_profile_number_types_and_background(self):
        for key, value in [
            ("schema", True),
            ("background", [float("nan"), 0, 0]),
            ("background", [0, 0, 0]),
        ]:
            data = copy.deepcopy(self.data)
            data[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                load_clipping(data)
        data = copy.deepcopy(self.data)
        data["chunks"][0]["rotation"][0] = True
        with self.assertRaises(ValueError):
            load_clipping(data)
        data = copy.deepcopy(self.data)
        data["chunks"][0]["splats"].reverse()
        with self.assertRaises(ValueError):
            load_clipping(data)

    def image(self):
        image = np.empty((720, 1280, 3), dtype=np.uint8)
        image[:] = self.background
        for index in [1, 2, 3, 4]:
            x, y = 190 + 450 * (index % 3), 255 if index < 3 else 465
            image[y - 8 : y + 8, x - 8 : x + 8] = [180, 100, 80]
        return image

    def test_independent_visible_and_background_regions(self):
        self.assertTrue(
            all(
                v["passed"]
                for v in clipping_regions(self.image(), self.data["background"])
            )
        )

    def test_background_only_cannot_pass(self):
        image = self.image()
        image[:] = self.background
        result = clipping_regions(image, self.data["background"])
        self.assertEqual(
            [v["passed"] for v in result], [True, False, False, False, False, True]
        )

    def test_omitted_visible_and_leaking_excluded_contributions_fail(self):
        image = self.image()
        image[247:263, 632:648] = self.background
        self.assertFalse(clipping_regions(image, self.data["background"])[1]["passed"])
        image = self.image()
        image[255, 190] = [255, 255, 255]
        self.assertFalse(clipping_regions(image, self.data["background"])[0]["passed"])

    def test_invalid_readback_and_background(self):
        for image in [
            np.zeros((1, 1, 3), dtype=np.uint8),
            self.image().astype(np.float32),
        ]:
            with self.assertRaises(ValueError):
                clipping_regions(image, self.data["background"])
        for background in [[0, 0], [0, 0, float("nan")], [-1, 0, 0]]:
            with self.assertRaises(ValueError):
                clipping_regions(self.image(), background)


class rebasing_input_tests(unittest.TestCase):
    def fixture(self, case="zero"):
        return dict(rebasing_profile(case), scene_content_sha256="a" * 64)

    def test_all_fixed_cases(self):
        for case in ("zero", "positive", "negative"):
            self.assertEqual(load_rebasing(self.fixture(case), case), 16)

    def test_no_camera_or_payload_subsets(self):
        for field in ("views", "chunks"):
            data = self.fixture()
            data[field].pop()
            with self.assertRaises(ValueError):
                load_rebasing(data, "zero")

    def test_changed_coordinates_and_payload_rejected(self):
        for change in (
            lambda d: d["chunks"][1]["translation"].__setitem__(0, 0.75),
            lambda d: d["chunks"][0]["splats"][0]["mean"].__setitem__(0, 0),
            lambda d: d["chunks"][0]["splats"][0]["sh"].__setitem__(47, 1),
            lambda d: d["views"][3]["position"].__setitem__(0, 0),
            lambda d: d["views"].reverse(),
            lambda d: d.__setitem__("rebase_excursion", 0),
            lambda d: d.__setitem__("render_origin", [0, 0, 0]),
            lambda d: d.__setitem__("world_offset", [1, 0, 0]),
        ):
            data = self.fixture()
            change(data)
            with self.assertRaises(ValueError):
                load_rebasing(data, "zero")

    def test_nonfinite_boolean_wrong_case_rejected(self):
        for value in (True, float("nan"), float("inf")):
            data = self.fixture()
            data["chunks"][0]["translation"][0] = value
            with self.assertRaises(ValueError):
                load_rebasing(data, "zero")
        with self.assertRaises(ValueError):
            load_rebasing(self.fixture("positive"), "negative")

    def test_invariance_rejects_hard_pixels_and_wrong_shape(self):
        left = np.zeros((720, 1280, 3), dtype=np.uint8)
        right = left.copy()
        self.assertTrue(image_comparison(left, right)["passed"])
        right[100, 100] = 9
        self.assertFalse(image_comparison(left, right)["passed"])
        with self.assertRaises(ValueError):
            image_comparison(left[:10], right)

    def test_baseline_hashes_and_failed_reports_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            fixture = root / "fixture.json"
            fixture.write_text(json.dumps(self.fixture()))
            views = []
            for name in ("centered", "off_center", "rolled", "translated"):
                # Opaque small bytes test report binding, not image quality.
                (root / (name + ".ppm")).write_bytes(b"native")
                (root / (name + "-reference.png")).write_bytes(b"reference")
                views.append(
                    {
                        "name": name,
                        "passed": True,
                        "native_sha256": sha256(root / (name + ".ppm")),
                        "reference_sha256": sha256(root / (name + "-reference.png")),
                    }
                )
            report = {
                "passed": True,
                "rebasing_case": "zero",
                "views": views,
                "fixture_sha256": sha256(fixture),
                "comparison_script_sha256": sha256(
                    Path(__file__).with_name("compare_reference.py")
                ),
                "tolerance": {
                    "min_psnr_db": MIN_PSNR,
                    "max_error_levels": MAX_ERROR_LEVELS,
                },
            }
            report_path = root / "comparison.json"
            report_path.write_text(json.dumps(report))
            self.assertTrue(load_rebasing_baseline(root)["passed"])
            for field, value in (
                ("passed", False),
                ("views", views[:3]),
                ("comparison_script_sha256", "0" * 64),
                ("tolerance", {"min_psnr_db": 0, "max_error_levels": 255}),
            ):
                report_path.write_text(json.dumps(dict(report, **{field: value})))
                with self.assertRaises(ValueError):
                    load_rebasing_baseline(root)
            report_path.write_text(json.dumps(report))
            (root / "translated-reference.png").write_bytes(b"changed")
            with self.assertRaises(ValueError):
                load_rebasing_baseline(root)


if __name__ == "__main__":
    unittest.main()
