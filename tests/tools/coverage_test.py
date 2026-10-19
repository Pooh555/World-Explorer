"""Check denominator integrity and fixed E1 coverage thresholds."""

import copy
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "coverage_report", ROOT / "tools/measure_coverage.py"
)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class CoverageTests(unittest.TestCase):
    def setUp(self):
        self.files = {"src/scene_store/scene.cpp": None, "src/core/hash.cpp": None}
        self.scope = {
            "roots": ["src"],
            "package_update_modules": ["scene_store"],
            "package_update_files": [],
        }
        self.export = {
            "type": "llvm.coverage.json.export",
            "data": [
                {
                    "files": [
                        {
                            "filename": str(ROOT / name),
                            "summary": {
                                "lines": {"count": 100, "covered": 80},
                                "branches": {"count": 100, "covered": branches},
                            },
                        }
                        for name, branches in zip(self.files, (90, 50))
                    ]
                }
            ],
        }

    def summary(self, export=None):
        return MODULE.summarize(export or self.export, self.files, self.scope, ROOT)

    def test_fixed_thresholds_and_separate_validation_denominator(self):
        self.assertTrue(self.summary()["coverage_goals_met"])
        self.export["data"][0]["files"][0]["summary"]["branches"]["covered"] = 89
        self.assertFalse(self.summary()["goals"]["package_update_branches_90"])
        self.assertFalse(self.summary()["goals"]["core_branches_70"])

    def test_unexecuted_file_remains_in_denominator(self):
        for metric in ("lines", "branches"):
            self.export["data"][0]["files"][1]["summary"][metric]["covered"] = 0
        self.assertEqual(
            self.summary()["groups"]["non_gpu_core"]["lines"]["percent"], 40
        )

    def test_missing_translation_unit_aborts(self):
        self.export["data"][0]["files"].pop()
        with self.assertRaisesRegex(ValueError, "missing"):
            self.summary()

    def test_vendor_and_test_counters_cannot_inflate_coverage(self):
        row = copy.deepcopy(self.export["data"][0]["files"][0])
        row["filename"] = str(ROOT / "tests/unit/contracts_tests.cpp")
        self.export["data"][0]["files"].append(row)
        self.assertEqual(
            self.summary()["groups"]["non_gpu_core"]["lines"]["count"], 200
        )

    def test_duplicate_invalid_and_empty_counters_abort(self):
        duplicate = copy.deepcopy(self.export)
        duplicate["data"][0]["files"].append(duplicate["data"][0]["files"][0])
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            self.summary(duplicate)
        self.export["data"][0]["files"][0]["summary"]["lines"]["covered"] = 101
        with self.assertRaisesRegex(ValueError, "Invalid"):
            self.summary()
        empty = copy.deepcopy(self.export)
        for row in empty["data"][0]["files"]:
            for metric in ("lines", "branches"):
                row["summary"][metric] = {"count": 0, "covered": 0}
        with self.assertRaisesRegex(ValueError, "Empty"):
            self.summary(empty)


if __name__ == "__main__":
    unittest.main()
