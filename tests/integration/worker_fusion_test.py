"""Synthetic fusion invariants; analytic surfaces are not Q5 ground truth."""

import copy
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "workers/fusion"))
from contract import selected_recipe, load_request, digest
from geometry import (
    robust_extent,
    consistent_masks,
    integrate,
    observed_arrays,
    world_points,
)
from sequence import StagedSequence


class FusionContractTest(unittest.TestCase):
    def fixture(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        root = Path(temp.name)
        recipe = selected_recipe()
        (root / "recipe.json").write_text(json.dumps(recipe))
        request = {
            "schema_version": 1,
            "recipe": "recipe.json",
            "recipe_sha256": digest((root / "recipe.json").read_bytes()),
            "cache": "cache",
            "receipt_sha256": "a" * 64,
        }
        (root / "request.json").write_text(json.dumps(request))
        return root, recipe, request

    def test_fixed_recipe_and_changed_code_identity_rejected(self):
        root, recipe, request = self.fixture()
        path = root / "request.json"
        self.assertEqual(load_request(path, digest(path.read_bytes()))[1], recipe)
        recipe["implementation_sha256"] = "b" * 64
        (root / "recipe.json").write_text(json.dumps(recipe))
        request["recipe_sha256"] = digest((root / "recipe.json").read_bytes())
        path.write_text(json.dumps(request))
        with self.assertRaisesRegex(ValueError, "fixed pilot"):
            load_request(path, digest(path.read_bytes()))

    def test_rehashed_threshold_change_boolean_schema_and_unknown_key_reject(self):
        root, recipe, request = self.fixture()
        path = root / "request.json"
        recipe["voxel_divisor"] = 256
        (root / "recipe.json").write_text(json.dumps(recipe))
        request["recipe_sha256"] = digest((root / "recipe.json").read_bytes())
        path.write_text(json.dumps(request))
        with self.assertRaises(ValueError):
            load_request(path, digest(path.read_bytes()))
        request["schema_version"] = True
        path.write_text(json.dumps(request))
        with self.assertRaises(ValueError):
            load_request(path, digest(path.read_bytes()))
        request["schema_version"] = 1
        request["extra"] = 1
        path.write_text(json.dumps(request))
        with self.assertRaises(ValueError):
            load_request(path, digest(path.read_bytes()))

    def test_sequence_policy_is_explicit_and_keeps_fusion_limits(self):
        root, pilot, request = self.fixture()
        recipe = selected_recipe(sequence=True)
        self.assertEqual(recipe["kind"], "OBSERVED_TSDF_CPU_SEQUENCE_V1")
        for key in (
            "max_blocks",
            "max_rss_bytes",
            "max_seconds",
            "max_output_bytes",
            "voxel_divisor",
        ):
            self.assertEqual(recipe[key], pilot[key])
        self.assertEqual(recipe["max_scratch_bytes"], 4 * 1024**3)
        (root / "recipe.json").write_text(json.dumps(recipe))
        request["recipe_sha256"] = digest((root / "recipe.json").read_bytes())
        path = root / "request.json"
        path.write_text(json.dumps(request))
        self.assertEqual(load_request(path, digest(path.read_bytes()))[1], recipe)
        recipe["max_scratch_bytes"] += 1
        (root / "recipe.json").write_text(json.dumps(recipe))
        request["recipe_sha256"] = digest((root / "recipe.json").read_bytes())
        path.write_text(json.dumps(request))
        with self.assertRaises(ValueError):
            load_request(path, digest(path.read_bytes()))


@unittest.skipUnless(importlib.util.find_spec("numpy"), "Requires isolated NumPy")
class FusionGeometryTest(unittest.TestCase):
    def fixture(self):
        import numpy as np

        # Identical optical views of an asymmetric bounded plane. Translation and
        # a 90 degree rotation make world X=7 the analytic surface, not world Z.
        transform = np.array(
            [[0.0, 0.0, -1.0, 3.0], [0.0, 1.0, 0.0, -2.0], [1.0, 0.0, 0.0, -5.0]]
        )
        view = {
            "name": "synthetic",
            "intrinsics": [45.0, 55.0, 29.25, 20.75],
            "cam_from_world": transform,
            "depth": np.full((48, 64), 2.0, dtype=np.float32),
            "valid": np.ones((48, 64), dtype=np.uint8),
            "confidence": np.full((48, 64), 0.25, dtype=np.float32),
            "support": np.full((48, 64), 3, dtype=np.uint32),
            "sigma": None,
        }
        view["valid"][:, 28:37] = 0
        view["depth"][:, 28:37] = 0
        view["confidence"][:, 28:37] = 0
        view["support"][:, 28:37] = 0
        return [copy.deepcopy(view) for _ in range(3)]

    def test_analytic_world_transform_extent_unknowns_and_weight_denominators(self):
        import numpy as np

        views = self.fixture()
        points = world_points(
            views[0], np.array([4, 40]), np.array([5, 30]), np.array([2.0, 2.0])
        )
        self.assertTrue(np.allclose(points[:, 0], 7))
        self.assertAlmostEqual(points[0, 1], 2 + (5 - 20.75) * 2 / 55)
        self.assertAlmostEqual(points[0, 2], 3 - (4 - 29.25) * 2 / 45)
        views[0]["confidence"][3, 3] = 0
        masks, summary = consistent_masks(views, selected_recipe())
        self.assertFalse(masks[0][3, 3])
        for mask, row in zip(masks, summary):
            self.assertFalse(mask[:, 28:37].any())
            self.assertEqual(row["valid"] + row["unknown"], row["pixels"])
            self.assertEqual(
                row["accepted"]
                + row["rejected_positive_weight"]
                + row["zero_confidence_valid"],
                row["valid"],
            )
        extent, _, count = robust_extent(views)
        self.assertGreater(extent, 2)
        self.assertEqual(count, sum(int((v["confidence"] > 0).sum()) for v in views))

    def test_unknown_source_and_known_occluder_never_certify_support(self):
        views = self.fixture()
        views[1]["depth"][:] = 1
        masks, summary = consistent_masks(views, selected_recipe())
        self.assertFalse(masks[0].any())
        self.assertGreater(summary[0]["known_occlusion"], 0)
        views = self.fixture()
        views[2]["valid"][:] = 0
        masks, _ = consistent_masks(views, selected_recipe())
        self.assertFalse(masks[0].any())

    def test_cancellation_and_empty_extent_reject(self):
        views = self.fixture()
        with self.assertRaises(InterruptedError):
            consistent_masks(views, selected_recipe(), lambda: True)
        for view in views:
            view["confidence"][:] = 0
        with self.assertRaises(ValueError):
            robust_extent(views)

    def staged(self, views, *, recipe=None):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        sequence = StagedSequence(
            Path(temp.name) / "scratch", recipe or selected_recipe(sequence=True)
        )
        # Every view is a separate cache: support must cross all boundaries.
        sequence.stage(([view], {}) for view in views)
        return sequence

    def test_sequence_exact_pooled_percentiles_not_batch_percentiles(self):
        import numpy as np

        views = self.fixture()
        views[0]["confidence"][:] = 0
        views[0]["valid"][:] = 0
        views[0]["depth"][:] = 0
        views[1]["confidence"][1:40, :] = 0
        views[2]["cam_from_world"][0, 3] += 20
        expected = robust_extent(views)
        sequence = self.staged(views)
        actual = sequence.extent()
        self.assertEqual(actual[0], expected[0])
        self.assertEqual(actual[2], expected[2])
        np.testing.assert_array_equal(actual[1], expected[1])
        averaged = np.mean([robust_extent([view])[1] for view in views[1:]], axis=0)
        self.assertFalse(np.allclose(actual[1], averaged))

    def test_sequence_matches_all_source_masks_and_late_occlusion(self):
        import numpy as np

        for occluder in (False, True):
            with self.subTest(occluder=occluder):
                views = self.fixture() + self.fixture()[:1]
                views[1]["sigma"] = np.full(
                    views[1]["depth"].shape, 0.2, dtype=np.float32
                )
                views[0]["confidence"][3, 3] = 0
                # Earlier sources already provide the two required agreements;
                # a last-cache occluder must still veto the reference sample.
                if occluder:
                    views[-1]["depth"][views[-1]["valid"] == 1] = 1
                recipe = dict(selected_recipe(sequence=True), chunk_voxels=79)
                expected, summary = consistent_masks(views, recipe)
                sequence = self.staged(views, recipe=recipe)
                self.assertEqual(sequence.filter(), summary)
                for (_, mask), original in zip(sequence.pairs(), expected):
                    np.testing.assert_array_equal(mask, original)
                if occluder:
                    self.assertGreater(summary[0]["known_occlusion"], 0)
                    self.assertFalse(expected[0].any())
                else:
                    self.assertTrue(expected[0].any())

    def test_sequence_closes_maps_preserves_inputs_and_rejects_scratch_overflow(self):
        import numpy as np

        views = self.fixture()
        original = views[0]["depth"].copy()
        sequence = self.staged(views)
        with sequence.open_view(0) as view:
            mapped = view["depth"]
            self.assertFalse(mapped.flags.writeable)
            self.assertFalse(mapped._mmap.closed)
        self.assertTrue(mapped._mmap.closed)
        sequence.extent()
        sequence.filter()
        np.testing.assert_array_equal(views[0]["depth"], original)
        with self.assertRaisesRegex(ValueError, "scratch budget"):
            self.staged(
                views, recipe=dict(selected_recipe(sequence=True), max_scratch_bytes=1)
            )

    def test_sequence_late_failure_cancellation_and_occupied_scratch_preserve(self):
        views = self.fixture()
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        sequence = StagedSequence(
            Path(temp.name) / "scratch", selected_recipe(sequence=True)
        )

        def failed_batches():
            yield views[:1], {}
            raise ValueError("late child corruption")

        with self.assertRaisesRegex(ValueError, "late child"):
            sequence.stage(failed_batches())
        retained = {p.name: p.read_bytes() for p in sequence.directory.iterdir()}
        with self.assertRaises(FileExistsError):
            sequence.stage(iter([]))
        sequence.cancelled = lambda: True
        for operation in (sequence.extent, sequence.filter):
            with self.assertRaises(InterruptedError):
                operation()
        self.assertEqual(
            retained,
            {
                p.name: p.read_bytes()
                for p in sequence.directory.iterdir()
                if not p.name.startswith("extent-")
            },
        )

    @unittest.skipUnless(sys.platform == "linux", "Linux retained job owner")
    @unittest.skipUnless(importlib.util.find_spec("PIL"), "Requires isolated Pillow")
    def test_owned_sequence_rejects_late_child_corruption_before_ownership(self):
        import worker_admitted_visibility_test

        fixture = worker_admitted_visibility_test.AdmittedVisibilityTest()
        self.addCleanup(fixture.doCleanups)
        cache, receipt = fixture.sequence_fixture()
        bad = cache / "batch-1" / receipt["frames"][-1]["depth_asset"]
        bad.write_bytes(b"retained corrupt caller bytes")
        root = cache.parent
        recipe = root / "fusion-recipe.json"
        recipe.write_text(json.dumps(selected_recipe(sequence=True)))
        request = root / "fusion-request.json"
        request.write_text(
            json.dumps(
                {
                    "schema_version": 1,
                    "cache": str(cache),
                    "receipt_sha256": digest((cache / "manifest.json").read_bytes()),
                    "recipe": str(recipe),
                    "recipe_sha256": digest(recipe.read_bytes()),
                }
            )
        )
        spec = importlib.util.spec_from_file_location(
            "fusion_geometry_owner_test",
            Path(__file__).resolve().parents[2] / "tools/geometry_job.py",
        )
        coordinator = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(coordinator)
        job = root / "owned-job"
        args = SimpleNamespace(
            request=str(request),
            request_sha256=digest(request.read_bytes()),
            pose=str(cache / "batch-0/poses.weposes"),
            pose_sha256=receipt["pose_asset_sha256"],
            job=str(job),
            parent_supervised=False,
        )
        with self.assertRaises(ValueError):
            coordinator.execute(args)
        self.assertFalse(job.exists())
        self.assertEqual(bad.read_bytes(), b"retained corrupt caller bytes")

    @unittest.skipUnless(
        importlib.util.find_spec("open3d"), "Requires isolated CPU Open3D"
    )
    def test_sequence_integrates_one_grid_with_support_sigma_and_unknowns(self):
        import numpy as np

        views = self.fixture()
        views[1]["sigma"] = np.full(views[1]["depth"].shape, 0.1, dtype=np.float32)
        recipe = selected_recipe(sequence=True)
        masks, _ = consistent_masks(views, recipe)
        grid, indices, blocks = integrate(views, masks, 0.04, recipe)
        expected = observed_arrays(grid, indices, 0.04, recipe)
        sequence = self.staged(views)
        sequence.filter()
        grid, indices, actual_blocks = integrate(
            None, None, 0.04, recipe, pairs=sequence.pairs
        )
        actual = observed_arrays(grid, indices, 0.04, recipe)
        self.assertEqual(actual_blocks, blocks)
        for key in expected:
            np.testing.assert_array_equal(actual[key], expected[key])
        self.assertTrue((actual["support_sum"] == 9).all())
        self.assertTrue(np.allclose(actual["sigma_weight"], 0.25))
        self.assertLess(len(actual["weight"]), blocks * 8**3)

    @unittest.skipUnless(
        importlib.util.find_spec("open3d"), "Requires isolated CPU Open3D"
    )
    def test_large_world_offset_rejects_before_sparse_narrowing(self):
        views = self.fixture()
        masks = [view["valid"].astype(bool) for view in views]
        for view in views:
            view["cam_from_world"][2, 3] = -1e9
        with self.assertRaisesRegex(ValueError, "coordinate range/precision"):
            integrate(views, masks, 0.04, selected_recipe())

    @unittest.skipUnless(
        importlib.util.find_spec("open3d"), "Requires isolated CPU Open3D"
    )
    def test_weighted_sparse_tsdf_surface_unknown_gap_sigma_and_block_bound(self):
        import numpy as np

        views = self.fixture()
        views[1]["sigma"] = np.full((48, 64), 0.1, dtype=np.float32)
        recipe = selected_recipe()
        masks, _ = consistent_masks(views, recipe)
        with self.assertRaisesRegex(ValueError, "block budget"):
            integrate(views, masks, 0.04, dict(recipe, max_blocks=1))
        grid, indices, blocks = integrate(views, masks, 0.04, recipe)
        arrays = observed_arrays(grid, indices, 0.04, recipe)
        self.assertGreater(len(arrays["weight"]), 0)
        self.assertTrue(np.allclose(arrays["weight"], 0.75))
        self.assertTrue((arrays["observation_count"] == 3).all())
        self.assertTrue((arrays["support_sum"] == 9).all())
        self.assertTrue(np.allclose(arrays["sigma_weight"], 0.25))
        self.assertTrue(
            np.allclose(arrays["sigma2_sum"] / arrays["sigma_weight"], 0.01)
        )
        self.assertLess(len(arrays["weight"]), blocks * 8**3)
        self.assertTrue((arrays["front_count"] > 0).any())
        self.assertTrue((arrays["front_count"] == 0).any())
        mesh = grid.extract_triangle_mesh(weight_threshold=1e-6)
        positions = mesh.vertex.positions.numpy()
        self.assertGreater(len(positions), 0)
        self.assertLess(float(np.max(np.abs(positions[:, 0] - 7))), 1e-4)
        # The missing central stripe must stay open; marching cubes may not bridge it.
        camera = (
            positions @ views[0]["cam_from_world"][:, :3].T
            + views[0]["cam_from_world"][:, 3]
        )
        u = camera[:, 0] * 45 / camera[:, 2] + 29.25
        self.assertFalse(((u > 29) & (u < 35)).any())
        for triangle in mesh.triangle.indices.numpy():
            self.assertFalse(u[triangle].min() < 29 and u[triangle].max() > 35)

    @unittest.skipUnless(
        importlib.util.find_spec("open3d"), "Requires isolated CPU Open3D"
    )
    def test_confidence_changes_zero_crossing_and_missing_sigma_stays_unavailable(self):
        import numpy as np

        # Independent two-plane weighted zero: (.25*2+.75*2.04)/1 = 2.03.
        # Test integration itself with explicit masks, not the cross-view selector.
        views = self.fixture()[:2]
        views[1]["depth"][views[1]["valid"] == 1] = 2.04
        views[1]["confidence"][views[1]["valid"] == 1] = 0.75
        masks = [view["valid"].astype(bool) for view in views]
        recipe = selected_recipe()
        grid, indices, _ = integrate(views, masks, 0.02, recipe)
        arrays = observed_arrays(grid, indices, 0.02, recipe)
        self.assertFalse(arrays["sigma_weight"].any())
        self.assertFalse(arrays["sigma2_sum"].any())
        shared = arrays["observation_count"] == 2
        world_x = arrays["lattice"][shared, 0] * 0.02
        expected = (7.03 - world_x) / (0.02 * 4)
        self.assertTrue(np.allclose(arrays["tsdf"][shared], expected, atol=2e-5))


if __name__ == "__main__":
    unittest.main()
