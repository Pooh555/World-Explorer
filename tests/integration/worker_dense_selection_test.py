"""Synthetic parallax/closure/resource selection checks; no GPU or scene claims."""

import copy
import importlib.util
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "workers/dense"))
import source_selection as selection


@unittest.skipUnless(
    importlib.util.find_spec("numpy"), "Array selection requires isolated NumPy"
)
class dense_selection_test(unittest.TestCase):
    def fixture(self):
        import numpy as np

        centers = np.zeros((12, 3))
        centers[:9, 0] = np.arange(9) * 0.005
        centers[9:, 0] = [0.4, 0.8, 1.2]
        points = np.array(
            [
                [(x - 5) * 0.05, (y - 5) * 0.05, 4.0]
                for x in range(10)
                for y in range(10)
            ]
        )
        observations = [np.arange(100) for _ in range(12)]
        names = [f"{i:06}.png" for i in range(12)]
        return (
            centers,
            points,
            observations,
            names,
            copy.deepcopy(selection.DEFAULT_POLICY),
        )

    def test_preserves_references_adds_far_sources_and_closes_every_geometric_row(self):
        centers, points, observations, names, policy = self.fixture()
        before = centers.copy(), points.copy(), [v.copy() for v in observations]
        report = selection.choose_sources(centers, points, observations, names, policy)
        self.assertEqual(report["selected_indices"], list(range(11)))
        self.assertEqual(report["added_indices"], [9, 10])
        self.assertEqual(report["dropped_indices"], [])
        self.assertTrue(
            all(
                row["fraction_with_two_sources"] == 1
                for row in report["reference_support"]
            )
        )
        for index in range(9):
            self.assertEqual(report["neighbors"][names[index]], [names[9], names[10]])
        for ref, sources in report["neighbors"].items():
            self.assertNotIn(ref, sources)
            self.assertTrue(2 <= len(sources) <= 6)
            self.assertTrue(set(sources) <= set(report["neighbors"]))
        self.assertTrue((centers == before[0]).all() and (points == before[1]).all())
        self.assertTrue(all((a == b).all() for a, b in zip(observations, before[2])))
        self.assertEqual(
            selection.report_digest(report),
            selection.report_digest(
                selection.choose_sources(centers, points, observations, names, policy)
            ),
        )

    def test_low_parallax_and_no_co_visibility_reject_without_dropping_references(self):
        import numpy as np

        centers, points, observations, names, policy = self.fixture()
        with self.assertRaises(ValueError):
            selection.choose_sources(
                np.zeros_like(centers), points, observations, names, policy
            )
        observations[9:] = [np.array([], dtype=np.int64) for _ in range(3)]
        with self.assertRaises(ValueError):
            selection.choose_sources(centers, points, observations, names, policy)

    def test_arbitrary_scale_and_coordinate_rotation_preserve_selection(self):
        import numpy as np

        centers, points, observations, names, policy = self.fixture()
        baseline = selection.choose_sources(
            centers, points, observations, names, policy
        )
        rotation = np.array([[0.0, 0.0, 1.0], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0]])
        for scale in (1e-140, 1e140):
            actual = selection.choose_sources(
                (centers @ rotation) * scale,
                (points @ rotation) * scale,
                observations,
                names,
                policy,
            )
            self.assertEqual(actual, baseline)

    def test_degenerate_invalid_numeric_ids_policy_and_cancellation_fail(self):
        import numpy as np

        for kind in (
            "nan",
            "infinity",
            "duplicate",
            "escape",
            "duplicate-name",
            "bool-policy",
            "lower-angle",
            "budget",
            "cancel",
        ):
            centers, points, observations, names, policy = self.fixture()
            if kind == "nan":
                points[0, 0] = np.nan
            if kind == "infinity":
                centers[0, 0] = np.inf
            if kind == "duplicate":
                observations[0] = np.array([0, 0])
            if kind == "escape":
                observations[0] = np.array([100])
            if kind == "duplicate-name":
                names[0] = names[1]
            if kind == "bool-policy":
                policy["max_sources"] = True
            if kind == "lower-angle":
                policy["min_angle_degrees"] = 2.9
            if kind == "budget":
                policy["max_views"] = 121
            with self.subTest(kind=kind):
                with self.assertRaises((ValueError, InterruptedError)):
                    selection.choose_sources(
                        centers,
                        points,
                        observations,
                        names,
                        policy,
                        lambda: kind == "cancel",
                    )

    def test_pair_budget_and_insufficient_support_are_explicit_failures(self):
        from unittest.mock import patch

        centers, points, observations, names, policy = self.fixture()
        with patch.object(selection, "MAX_PAIR_TRACKS", 1):
            with self.assertRaises(ValueError):
                selection.choose_sources(centers, points, observations, names, policy)
        policy["max_views"] = 9
        with self.assertRaises(ValueError):
            selection.choose_sources(centers, points, observations, names, policy)


@unittest.skipUnless(
    importlib.util.find_spec("pycolmap"), "Model selection requires isolated pycolmap"
)
class dense_selection_model_test(unittest.TestCase):
    def fixture(self):
        import numpy as np
        from types import SimpleNamespace as Obj

        images, entries, frames = {}, [], []
        k = np.array([[21.0, 0.0, 7.25], [0.0, 21.0, 4.75], [0.0, 0.0, 1.0]])
        camera = Obj(
            model_name="PINHOLE", width=17, height=13, calibration_matrix=lambda: k
        )
        for index, x in enumerate((0.0, 0.4, 0.8)):
            image_id = index + 7
            center = np.array([x, 0.0, 0.0])
            forward = np.column_stack((np.eye(3), -center))
            inverse = np.column_stack((np.eye(3), center))
            pose = Obj(
                matrix=lambda a=forward: a,
                inverse=lambda a=inverse: Obj(matrix=lambda: a),
            )
            points = [
                Obj(
                    point3D_id=7,
                    xy=np.array([7.25 - 21 * x / 4, 4.75]),
                    has_point3D=lambda: True,
                ),
                Obj(
                    point3D_id=7,
                    xy=np.array([7.35 - 21 * x / 4, 4.85]),
                    has_point3D=lambda: True,
                ),
            ]
            image = Obj(
                image_id=image_id,
                camera_id=1,
                name=f"{index:06}.png",
                points2D=points,
                num_points2D=lambda: 2,
                cam_from_world=lambda p=pose: p,
                projection_center=lambda c=center: c,
            )
            images[image_id] = image
            entries += [Obj(image_id=image_id, point2D_idx=j) for j in (0, 1)]
            frames.append(
                {
                    "image": "training/images/" + image.name,
                    "backend_image_id": image_id,
                    "backend_camera_id": 1,
                    "width": 17,
                    "height": 13,
                    "intrinsics": [21, 21, 6.75, 4.25],
                    "world_from_camera": {
                        "rotation_wxyz": [0, 1, 0, 0],
                        "translation": [x, 0, 0],
                        "scale": 1,
                    },
                }
            )
        model = Obj(
            images=images,
            cameras={1: camera},
            points3D={
                7: Obj(xyz=np.array([0.0, 0.0, 4.0]), track=Obj(elements=entries))
            },
            num_cameras=lambda: 1,
            num_points3D=lambda: 1,
        )
        policy = copy.deepcopy(selection.DEFAULT_POLICY)
        policy.update(reference_indices=[0], max_views=3, min_shared_tracks=1)
        return model, {"frames": frames}, policy

    def test_multiple_track_elements_in_one_view_count_as_one_landmark_observation(
        self,
    ):
        model, native, policy = self.fixture()
        report = selection.plan_sources(model, native, policy)
        self.assertEqual(report["collapsed_same_view_observations"], 3)
        self.assertEqual(report["reference_support"][0]["visible_landmarks"], 1)
        self.assertEqual(
            report["reference_support"][0]["landmarks_with_two_sources"], 1
        )
        self.assertEqual(len(model.images[7].points2D), 2)
        self.assertEqual(len(model.points3D[7].track.elements), 6)

    def test_inconsistent_back_references_pose_calibration_and_duplicate_track_entries_reject(
        self,
    ):
        for kind in ("backref", "duplicate", "pose", "calibration", "missing"):
            model, native, policy = self.fixture()
            if kind == "backref":
                model.points3D[7].track.elements[0].image_id = 999
            if kind == "duplicate":
                model.points3D[7].track.elements.append(
                    model.points3D[7].track.elements[0]
                )
            if kind == "pose":
                native["frames"][0]["world_from_camera"]["translation"][1] = 1
            if kind == "calibration":
                native["frames"][0]["intrinsics"][2] += 0.25
            if kind == "missing":
                model.images[7].points2D[0].point3D_id = 9
            with self.subTest(kind=kind):
                with self.assertRaises(ValueError):
                    selection.plan_sources(model, native, policy)


if __name__ == "__main__":
    unittest.main()
