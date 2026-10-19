"""Frame-coverage regressions; these fixtures are synthetic, not scene evidence."""

import importlib.util
from pathlib import Path
import unittest

path = Path(__file__).resolve().parents[2] / "workers/colmap/model_selection.py"
spec = importlib.util.spec_from_file_location("model_selection", path)
selection = importlib.util.module_from_spec(spec)
spec.loader.exec_module(selection)


class ModelSelectionTests(unittest.TestCase):
    def test_complete_later_model_supersedes_redundant_attempt(self):
        frames = set(range(63))
        self.assertEqual(
            selection.select_candidate([{23, 59, 60, 61}, frames], frames), 1
        )
        self.assertEqual(
            selection.select_candidate([frames, {23, 59, 60, 61}], frames), 0
        )

    def test_does_not_accept_largest_model_with_separate_coverage(self):
        self.assertIsNone(
            selection.select_candidate([{1, 2, 3, 4}, {5, 6}], set(range(1, 7)))
        )

    def test_overlap_without_containment_requires_recovery(self):
        self.assertIsNone(selection.select_candidate([{1, 2, 3}, {3, 4}], {1, 2, 3, 4}))

    def test_equal_coverage_is_ambiguous(self):
        self.assertIsNone(selection.select_candidate([{1, 2}, {1, 2}], {1, 2, 3}))

    def test_single_model_leaves_existing_registration_threshold_to_caller(self):
        self.assertEqual(selection.select_candidate([{1, 2}], {1, 2, 3}), 0)

    def test_unknown_and_empty_model_images_are_rejected(self):
        for models in [[], [set()], [{1, 99}]]:
            with self.subTest(models=models), self.assertRaises(ValueError):
                selection.select_candidate(models, {1, 2, 3})


if __name__ == "__main__":
    unittest.main()
