"""Synthetic dense sampling, complete denominators and train/evaluation isolation."""

import copy
import importlib.util
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "workers/dense"))
import reliability
import visibility


@unittest.skipUnless(importlib.util.find_spec("numpy"), "Requires isolated NumPy")
class DenseReliabilityTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        import numpy as np

        cls.views = []
        x, y = np.meshgrid(np.arange(320), np.arange(240))
        for index in range(9):
            center = (index - 4) * 0.27
            world_x, world_y = (x - 156.25) * 4 / 125 + center, (y - 113.75) * 4 / 143
            gray = np.where(
                world_x < -0.8,
                0.5,
                0.5 + 0.17 * np.sin(world_x * 7) + 0.16 * np.cos(world_y * 11),
            ).astype(np.float32)
            depth = np.full((240, 320), 4, dtype=np.float32)
            depth[:48] = -1
            gray.flags.writeable = False
            depth.flags.writeable = False
            cls.views.append(
                {
                    "name": f"{index+1:06}.png",
                    "intrinsics": [125.0, 143.0, 156.25, 113.75],
                    "cam_from_world": np.column_stack((np.eye(3), [-center, 0, 0])),
                    "gray": gray,
                    "depth": depth,
                }
            )
        names = [v["name"] for v in cls.views]
        cls.rows = {
            name: names[max(0, i - 3) : i] + names[i + 1 : i + 4]
            for i, name in enumerate(names)
        }
        cls.plan = reliability.train(cls.views, cls.rows, "a" * 64)

    def test_grid_has_fixed_disjoint_interior_samples_and_rejects_insufficient_resolution(
        self,
    ):
        for shape in ((204, 268), (240, 320), (1102, 2048), (4096, 4096)):
            train = set(zip(*reliability.grid(shape)))
            evaluation = set(zip(*reliability.grid(shape, True)))
            self.assertEqual(len(train), 12_288)
            self.assertEqual(len(evaluation), 12_288)
            self.assertFalse(train & evaluation)
            self.assertTrue(
                all(
                    2 <= x < shape[1] - 2 and 2 <= y < shape[0] - 2
                    for x, y in train | evaluation
                )
            )
        for shape in ((203, 268), (204, 267)):
            with self.assertRaises(ValueError):
                reliability.grid(shape)

    def test_unknown_flat_and_unsupported_samples_remain_in_every_denominator(self):
        report = self.plan["training"]
        for ref in report["references"]:
            for label in ("cached_baseline", "temporal", "candidate"):
                result = ref[label]
                self.assertEqual(result["all"]["samples"], 12_288)
                self.assertLess(result["all"]["reference_known"], 12_288)
                self.assertGreater(result["all"]["two_supported"], 0)
                self.assertGreater(result["all"]["unsupported"], 0)
                self.assertEqual(
                    result["all"]["two_supported"] + result["all"]["unsupported"],
                    12_288,
                )
                self.assertGreater(result["texture"]["FLAT"]["samples"], 0)
                self.assertEqual(result["texture"]["FLAT"]["two_supported"], 0)
                self.assertEqual(
                    sum(group["samples"] for group in result["texture"].values()),
                    12_288,
                )
                self.assertEqual(
                    sum(tile["samples"] for tile in result["tiles"]), 12_288
                )
                self.assertTrue(all(tile["samples"] == 256 for tile in result["tiles"]))
                self.assertEqual(
                    sum(group["samples"] for group in result["tile_texture"]), 12_288
                )
                self.assertEqual(
                    sum(group["two_supported"] for group in result["tile_texture"]),
                    result["all"]["two_supported"],
                )

    def test_first_failure_partition_and_conditional_depth_tail_population_are_explicit(
        self,
    ):
        for summary in self.plan["training"]["pairs"].values():
            self.assertEqual(
                sum(summary["all"]["first_failure_counts"].values()), 12_288
            )
            self.assertGreater(
                summary["all"]["first_failure_counts"]["reference_unknown"], 0
            )
            self.assertEqual(
                sum(group["samples"] for group in summary["texture"].values()), 12_288
            )
            for group in [summary["all"], *summary["texture"].values()]:
                self.assertEqual(
                    sum(group["first_failure_counts"].values()), group["samples"]
                )
                self.assertLessEqual(
                    group["known_positive_back_samples"], group["samples"]
                )
                if group["known_positive_back_samples"]:
                    self.assertLess(group["relative_depth_p90"], 1e-10)

    def test_frozen_training_is_deterministic_closed_and_never_samples_evaluation_grid(
        self,
    ):
        original = reliability.grid
        phases = []

        def tracked_grid(shape, evaluation=False):
            phases.append(evaluation)
            return original(shape, evaluation)

        with patch.object(reliability, "grid", tracked_grid):
            repeat = reliability.train(self.views, self.rows, "a" * 64)
        self.assertEqual(repeat, self.plan)
        self.assertTrue(phases)
        self.assertFalse(any(phases))
        self.assertFalse(repeat["gpu_recipe_ready"])
        self.assertEqual(repeat["reference_indices"], list(range(9)))
        rows = repeat["training"]["neighbors"]
        self.assertEqual(set(rows), set(self.rows))
        for ref, sources in rows.items():
            self.assertEqual(len(sources), 6)
            self.assertEqual(len(set(sources)), 6)
            self.assertNotIn(ref, sources)
            self.assertTrue(set(sources) <= set(rows))

    def test_evaluation_keeps_frozen_rows_and_rehashed_forgery_fails_before_evaluation(
        self,
    ):
        result = reliability.evaluate(self.views, self.rows, self.plan, "a" * 64)
        self.assertEqual(
            result["evaluation"]["neighbors"], self.plan["training"]["neighbors"]
        )
        self.assertFalse(result["gpu_recipe_ready"])
        for mutation in ("row", "statistic"):
            forged = copy.deepcopy(self.plan)
            if mutation == "row":
                row = forged["training"]["neighbors"][self.views[0]["name"]]
                row[0], row[1] = row[1], row[0]
            else:
                forged["training"]["references"][0]["candidate"]["all"][
                    "two_supported"
                ] += 1
            phases = []
            original = reliability.grid

            def tracked_grid(shape, evaluation=False):
                phases.append(evaluation)
                return original(shape, evaluation)

            with patch.object(reliability, "grid", tracked_grid):
                with self.assertRaises(ValueError):
                    reliability.evaluate(self.views, self.rows, forged, "a" * 64)
            self.assertFalse(any(phases))

    def test_wrong_cache_policy_code_and_cancel_fail_without_touching_inputs(self):
        for key in ("cache_sha256", "implementation_sha256", "policy"):
            forged = copy.deepcopy(self.plan)
            forged[key] = "f" * 64 if key != "policy" else {}
            with self.assertRaises(ValueError):
                reliability.evaluate(self.views, self.rows, forged, "a" * 64)
        with self.assertRaises(InterruptedError):
            reliability.train(self.views, self.rows, "a" * 64, lambda: True)
        with self.assertRaises(InterruptedError):
            reliability.evaluate(
                self.views, self.rows, self.plan, "a" * 64, lambda: True
            )
        self.assertTrue(
            all(
                not v["gray"].flags.writeable and not v["depth"].flags.writeable
                for v in self.views
            )
        )

    def test_explicit_pixel_interface_rejects_unbounded_arrays_and_preserves_unsigned_centers(
        self,
    ):
        import numpy as np

        ref, source = self.views[4:6]
        x, y = reliability.grid(ref["depth"].shape)
        actual = visibility.pixel_evidence(ref, source, x, y)
        unsigned = visibility.pixel_evidence(
            ref, source, x.astype(np.uint64), y.astype(np.uint64)
        )
        self.assertTrue(np.array_equal(actual[0], unsigned[0]))
        for px, py in (
            (x.astype(float), y),
            (x[:0], y[:0]),
            (x, y[:1]),
            (np.zeros_like(x), y),
            (np.tile(x, 2), np.tile(y, 2)),
        ):
            with self.assertRaises(ValueError):
                visibility.pixel_evidence(ref, source, px, py)


if __name__ == "__main__":
    unittest.main()
