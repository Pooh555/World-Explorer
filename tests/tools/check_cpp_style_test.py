"""Regression tests for the repository C++ style gate."""
import importlib.util
from pathlib import Path
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("check_cpp_style", ROOT / "tools/check_cpp_style.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class CheckCppStyleTests(unittest.TestCase):
    def test_tidy_targets_exclude_headers_without_compile_entries(self):
        database = ROOT / "build/dev/compile_commands.json"
        files = [
            ROOT / "include/world_explorer/capture/frame_source.hpp",
            ROOT / "src/core/hash.cpp",
        ]
        self.assertEqual(MODULE.select_tidy_files(files, database), [ROOT / "src/core/hash.cpp"])

    def test_tidy_targets_exclude_build_tree_and_vendor_code(self):
        files = [
            ROOT / "src/application/scenes.cpp",
            ROOT / "build/dev/_deps/catch2-src/src/catch2/benchmark/detail/catch_analyse.cpp",
        ]
        database = [
            {"file": str(ROOT / "src/application/scenes.cpp")},
            {"file": str(ROOT / "build/dev/_deps/catch2-src/src/catch2/benchmark/detail/catch_analyse.cpp")},
        ]
        self.assertEqual(MODULE.select_tidy_files(files, None), files)
        self.assertEqual(
            MODULE.select_tidy_files(files, ROOT / "build/dev/compile_commands.json"),
            [ROOT / "src/application/scenes.cpp"],
        )

    def test_repo_quality_checks_run_docs_and_boundaries(self):
        with mock.patch.object(MODULE, "run_command") as run_command:
            run_command.return_value = 0
            self.assertEqual(MODULE.run_repo_quality_checks(), 0)

        self.assertEqual(
            run_command.call_args_list,
            [
                mock.call(["python3", str(ROOT / "tools/check_docs.py")]),
                mock.call(["python3", str(ROOT / "tools/check_boundaries.py")]),
            ],
        )


if __name__ == "__main__":
    unittest.main()
