"""Synthetic visibility/cache safety checks; no GPU or measured reconstruction claim."""

import copy
import importlib.util
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "workers/dense"))
import cached_sources as cache
import visibility
from pilot import digest


@unittest.skipUnless(importlib.util.find_spec("numpy"), "Requires isolated NumPy")
class VisibilityTest(unittest.TestCase):
    def fixture(self):
        import numpy as np

        views = []
        x, y = np.meshgrid(np.arange(128), np.arange(96))
        for index in range(9):
            center = (index - 4) * 0.27
            world_x, world_y = (x - 62.25) * 4 / 93 + center, (y - 43.75) * 4 / 117
            gray = (
                0.5 + 0.17 * np.sin(world_x * 7) + 0.16 * np.cos(world_y * 11)
            ).astype(np.float32)
            views.append(
                {
                    "name": f"{index+1:06}.png",
                    "intrinsics": [93.0, 117.0, 62.25, 43.75],
                    "cam_from_world": np.column_stack((np.eye(3), [-center, 0, 0])),
                    "gray": gray,
                    "depth": np.full((96, 128), 4, dtype=np.float32),
                }
            )
        names = [v["name"] for v in views]
        rows = {
            name: names[max(0, i - 3) : i] + names[i + 1 : i + 4]
            for i, name in enumerate(names)
        }
        return views, rows

    def test_known_plane_axial_depth_asymmetric_intrinsics_and_integer_centers(self):
        import numpy as np

        views, _ = self.fixture()
        good, _, summary = visibility.pair_evidence(views[4], views[5])
        x, y = visibility.sample_grid((96, 128))
        # Independent closed-form translated parallel-camera plane oracle.
        u = x - 93 * 0.27 / 4
        inside = (u >= 0) & (u < 127)
        angle = []
        for px, py in zip(x, y):
            point = np.array([(px - 62.25) * 4 / 93, (py - 43.75) * 4 / 117, 4.0])
            first = point / np.linalg.norm(point)
            second = point - np.array([0.27, 0, 0])
            second /= np.linalg.norm(second)
            angle.append(np.degrees(np.arccos(np.clip(first @ second, -1, 1))))
        expected = inside & (np.array(angle) >= 3)
        self.assertEqual(summary["in_bounds"], int(inside.sum()))
        self.assertEqual(summary["geometric"], int(expected.sum()))
        self.assertLess(summary["roundtrip_median_pixels"], 1e-10)
        self.assertGreater(good.sum(), 500)
        self.assertTrue((good <= expected).all())

    def test_occlusion_unknown_depth_and_behind_camera_are_not_support(self):
        for kind in ("occluded", "zero", "negative", "behind"):
            views, _ = self.fixture()
            source = views[5]
            if kind == "occluded":
                source["depth"][:] = 2
            if kind == "zero":
                source["depth"][:] = 0
            if kind == "negative":
                source["depth"][:] = -1
            if kind == "behind":
                source["cam_from_world"][2, 3] = -8
            with self.subTest(kind=kind):
                good, _, summary = visibility.pair_evidence(views[4], source)
                self.assertFalse(good.any())
                self.assertEqual(summary["geometric"], 0)

    def test_textureless_and_inverted_appearance_fail_while_exposure_offset_survives(
        self,
    ):
        for kind in ("constant", "inverted", "exposure"):
            views, _ = self.fixture()
            if kind == "constant":
                views[5]["gray"][:] = 0.5
            if kind == "inverted":
                views[5]["gray"] = 1 - views[5]["gray"]
            if kind == "exposure":
                views[5]["gray"] = views[5]["gray"] * 0.8 + 0.1
            good, _, summary = visibility.pair_evidence(views[4], views[5])
            self.assertGreater(summary["geometric"], 500)
            if kind == "exposure":
                self.assertGreater(good.sum(), 500)
            else:
                self.assertFalse(good.any())

    def test_world_rotation_arbitrary_scale_and_disjoint_holdout_grid(self):
        import numpy as np

        views, _ = self.fixture()
        baseline = visibility.pair_evidence(views[4], views[5])[0]
        rotation = np.array([[0.0, 1.0, 0.0], [0.0, 0.0, 1.0], [1.0, 0.0, 0.0]])
        for scale in (1e-12, 1e12):
            transformed = copy.deepcopy(views)
            for view in transformed:
                view["cam_from_world"][:, :3] = (
                    view["cam_from_world"][:, :3] @ rotation.T
                )
                view["cam_from_world"][:, 3] *= scale
                view["depth"] *= scale
            visibility.validate_views(transformed)
            self.assertTrue(
                np.array_equal(
                    visibility.pair_evidence(transformed[4], transformed[5])[0],
                    baseline,
                )
            )
        train = set(zip(*visibility.sample_grid((96, 128))))
        heldout = set(zip(*visibility.sample_grid((96, 128), True)))
        self.assertEqual(len(train), 768)
        self.assertEqual(len(heldout), 768)
        self.assertFalse(train & heldout)

    def test_closed_deterministic_graph_keeps_all_references_and_borrows_arrays(self):
        views, rows = self.fixture()
        for view in views:
            view["depth"].flags.writeable = False
            view["gray"].flags.writeable = False
        report = visibility.score_sources(views, rows)
        self.assertEqual(report, visibility.score_sources(views, rows))
        self.assertEqual(report["reference_indices"], list(range(9)))
        self.assertFalse(report["gpu_recipe_ready"])
        self.assertEqual(set(report["neighbors"]), set(rows))
        for name, sources in report["neighbors"].items():
            self.assertEqual(len(sources), 6)
            self.assertEqual(len(set(sources)), 6)
            self.assertNotIn(name, sources)
            self.assertTrue(set(sources) <= set(rows))
        self.assertTrue(
            all(
                row["candidate"]["heldout_two_source_samples"] > 500
                for row in report["reference_comparison"]
            )
        )

    def test_invalid_numeric_rigid_pose_inventory_budgets_and_cancellation(self):
        import numpy as np

        for kind in (
            "nan",
            "gray",
            "focal",
            "rotation",
            "duplicate",
            "open",
            "self",
            "cancel",
        ):
            views, rows = self.fixture()
            if kind == "nan":
                views[0]["depth"][0, 0] = np.nan
            if kind == "gray":
                views[0]["gray"][0, 0] = 2
            if kind == "focal":
                views[0]["intrinsics"][0] = 0
            if kind == "rotation":
                views[0]["cam_from_world"][0, 0] = 2
            if kind == "duplicate":
                views[0]["name"] = views[1]["name"]
            if kind == "open":
                rows[views[0]["name"]][0] = "unknown.png"
            if kind == "self":
                rows[views[0]["name"]][0] = views[0]["name"]
            with self.subTest(kind=kind):
                with self.assertRaises((ValueError, InterruptedError)):
                    visibility.score_sources(views, rows, lambda: kind == "cancel")
        views, rows = self.fixture()
        with patch.object(visibility, "MAX_TOTAL_PIXELS", 1):
            with self.assertRaises(ValueError):
                visibility.score_sources(views, rows)


@unittest.skipUnless(
    importlib.util.find_spec("pycolmap"), "Requires isolated COLMAP/Pillow"
)
class CachedSourcesTest(unittest.TestCase):
    def setUp(self):
        import pycolmap

        if pycolmap.__version__ != "4.2.1":
            self.skipTest("Pinned isolated COLMAP 4.2.1 required")

    def fixture(self):
        import numpy as np
        from PIL import Image

        temp = tempfile.TemporaryDirectory(prefix="we-source-cache-test-")
        self.addCleanup(temp.cleanup)
        root = Path(temp.name)
        workspace = root / "workspace"
        for folder in ("sparse", "images", "stereo/depth_maps"):
            (workspace / folder).mkdir(parents=True, exist_ok=True)
        views, rows = VisibilityTest().fixture()
        (workspace / "sparse/cameras.bin").write_bytes(
            struct.pack("<QiiQQ4d", 1, 1, 1, 128, 96, 93, 117, 62.75, 44.25)
        )
        binary = struct.pack("<Q", 9)
        frames = []
        for index, view in enumerate(views):
            name = view["name"]
            center = (index - 4) * 0.27
            binary += (
                struct.pack("<i7di", index + 1, 1, 0, 0, 0, -center, 0, 0, 1)
                + name.encode()
                + b"\0"
                + struct.pack("<Q", 0)
            )
            gray = np.rint(view["gray"] * 255).astype(np.uint8)
            encoded = io.BytesIO()
            Image.fromarray(np.stack((gray, gray, gray), axis=-1)).save(
                encoded, format="PNG"
            )
            (workspace / "images" / name).write_bytes(encoded.getvalue())
            (workspace / "stereo/depth_maps" / (name + ".photometric.bin")).write_bytes(
                b"128&96&1&" + view["depth"].astype("<f4").tobytes()
            )
            frames.append(
                {
                    "image": "images/" + name,
                    "width": 128,
                    "height": 96,
                    "calibrated_image_sha256": digest(encoded.getvalue()),
                    "intrinsics": view["intrinsics"],
                    "backend_image_id": index + 1,
                    "backend_camera_id": 1,
                    "world_from_camera": {
                        "rotation_wxyz": [0, 1, 0, 0],
                        "translation": [center, 0, 0],
                        "scale": 1,
                    },
                }
            )
        (workspace / "sparse/images.bin").write_bytes(binary)
        (workspace / "sparse/points3D.bin").write_bytes(struct.pack("<Q", 0))

        def write(path, obj):
            path.write_text(json.dumps(obj, sort_keys=True) + "\n")
            return digest(path.read_bytes())

        recipe = {
            "scope": "PARTIAL_PARALLAX_NEIGHBOR_QUALIFICATION",
            "selection_indices": list(range(9)),
            "neighbors": rows,
            "worker_sha256": "a" * 64,
        }
        recipe_sha = write(root / "recipe.json", recipe)
        manifest_sha = write(
            root / "manifest.json",
            {
                "schema_version": 1,
                "kind": "DENSE_INPUTS_RESIZED_GRID",
                "depth_recipe_sha256": recipe_sha,
                "frames": frames,
            },
        )
        common = {
            "manifest_sha256": manifest_sha,
            "recipe_sha256": recipe_sha,
            "worker_sha256": "a" * 64,
        }
        config = "".join(
            name + "\n" + ", ".join(row) + "\n" for name, row in rows.items()
        ).encode()
        (workspace / "stereo/patch-match.cfg").write_bytes(config)
        prepared_sha = write(
            workspace / "prepared.json",
            {
                **common,
                "status": "PARTIAL_NEIGHBOR_INPUTS_PREPARED",
                "neighbors": rows,
                "config_sha256": digest(config),
                "sparse_sha256": {
                    path.name: digest(path.read_bytes())
                    for path in (workspace / "sparse").iterdir()
                },
            },
        )
        result_sha = write(
            workspace / "result.json",
            {
                **common,
                "status": "PARTIAL_NEIGHBOR_PILOT_COMPLETE",
                "prepared_sha256": prepared_sha,
                "frames": [
                    {"asset": "assets/" + v["name"] + ".wedepth"} for v in views
                ],
            },
        )
        args = SimpleNamespace(
            workspace=workspace,
            output=root / "cache.json",
            manifest=root / "manifest.json",
            manifest_sha256=manifest_sha,
            recipe=root / "recipe.json",
            recipe_sha256=recipe_sha,
            result_sha256=result_sha,
        )
        return root, args

    def test_frozen_private_cache_loads_owned_bytes_and_scores_without_solve(self):
        root, args = self.fixture()
        self.assertEqual(cache.freeze(args, lambda: False), 0)
        descriptor = json.loads(args.output.read_bytes())
        self.assertEqual(descriptor["kind"], cache.KIND)
        views, rows = cache.load_cache(args.workspace, descriptor)
        self.assertEqual(len(views), 9)
        self.assertEqual(len(rows), 9)
        output = root / "scores.json"
        self.assertEqual(
            cache.score(
                SimpleNamespace(
                    workspace=args.workspace,
                    output=output,
                    cache=args.output,
                    cache_sha256=digest(args.output.read_bytes()),
                ),
                lambda: False,
            ),
            0,
        )
        self.assertFalse(json.loads(output.read_bytes())["gpu_recipe_ready"])

    def test_changed_map_image_model_and_parent_link_reject_before_scoring(self):
        for target in (
            "stereo/depth_maps/000001.png.photometric.bin",
            "images/000001.png",
            "sparse/cameras.bin",
            "parent-link",
        ):
            _, args = self.fixture()
            cache.freeze(args, lambda: False)
            descriptor = json.loads(args.output.read_bytes())
            if target == "parent-link":
                folder = args.workspace / "images"
                moved = args.workspace / "moved-images"
                folder.rename(moved)
                folder.symlink_to(moved, target_is_directory=True)
            else:
                path = args.workspace / target
                data = bytearray(path.read_bytes())
                data[-1] ^= 1
                path.write_bytes(data)
            with self.subTest(target=target):
                with self.assertRaises(ValueError):
                    cache.load_cache(args.workspace, descriptor)

    def test_rehashed_metadata_and_private_map_structure_forgery_reject(self):
        for kind in (
            "pose",
            "calibration",
            "recipe",
            "extra-file",
            "trailing-depth",
            "nan-depth",
            "header",
        ):
            _, args = self.fixture()
            cache.freeze(args, lambda: False)
            descriptor = json.loads(args.output.read_bytes())
            if kind in ("pose", "calibration"):
                manifest = json.loads(descriptor["manifest"]["text"])
                frame = manifest["frames"][0]
                if kind == "pose":
                    frame["world_from_camera"]["translation"][2] += 1
                else:
                    frame["intrinsics"][2] += 0.25
                descriptor["manifest"]["text"] = json.dumps(manifest)
                # Also forge downstream manifest references, retaining actual model bytes.
                new_sha = digest(descriptor["manifest"]["text"].encode())
                descriptor["manifest"]["sha256"] = new_sha
                for name in ("prepared.json", "result.json"):
                    doc = json.loads((args.workspace / name).read_bytes())
                    doc["manifest_sha256"] = new_sha
                    if name == "result.json":
                        doc["prepared_sha256"] = descriptor["files"]["prepared.json"]
                    (args.workspace / name).write_text(json.dumps(doc))
                    descriptor["files"][name] = digest(
                        (args.workspace / name).read_bytes()
                    )
            elif kind == "recipe":
                descriptor["recipe"]["sha256"] = "f" * 64
            elif kind == "extra-file":
                descriptor["files"]["unlisted"] = "a" * 64
            else:
                key = "stereo/depth_maps/000001.png.photometric.bin"
                data = (args.workspace / key).read_bytes()
                if kind == "trailing-depth":
                    data += b"\0"
                if kind == "nan-depth":
                    data = data[:-4] + struct.pack("<f", float("nan"))
                if kind == "header":
                    data = b"127" + data[3:]
                (args.workspace / key).write_bytes(data)
                descriptor["files"][key] = digest(data)
            with self.subTest(kind=kind):
                with self.assertRaises((ValueError, KeyError, FileNotFoundError)):
                    cache.load_cache(args.workspace, descriptor)

    def test_output_collision_overlap_cancel_and_total_byte_budget_preserve_inputs(
        self,
    ):
        root, args = self.fixture()
        before = {
            str(p): digest(p.read_bytes())
            for p in args.workspace.rglob("*")
            if p.is_file()
        }
        with self.assertRaises(InterruptedError):
            cache.freeze(args, lambda: True)
        self.assertFalse(args.output.exists())
        args.output = args.workspace / "new.json"
        with self.assertRaises(ValueError):
            cache.freeze(args, lambda: False)
        args.output = root / "cache.json"
        cache.freeze(args, lambda: False)
        with self.assertRaises(ValueError):
            cache.freeze(args, lambda: False)
        descriptor = json.loads(args.output.read_bytes())
        with patch.object(cache, "MAX_CACHE_BYTES", 1):
            with self.assertRaises(ValueError):
                cache.load_cache(args.workspace, descriptor)
        self.assertEqual(
            before,
            {
                str(p): digest(p.read_bytes())
                for p in args.workspace.rglob("*")
                if p.is_file()
            },
        )


if __name__ == "__main__":
    unittest.main()
