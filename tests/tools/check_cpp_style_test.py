"""Regression tests for the repository C++ style gate."""

import importlib.util
import json
from pathlib import Path
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "check_cpp_style", ROOT / "tools/check_cpp_style.py"
)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class CheckCppStyleTests(unittest.TestCase):
    def test_tidy_targets_exclude_headers_without_compile_entries(self):
        database = ROOT / "build/dev/compile_commands.json"
        files = [
            ROOT / "include/world_explorer/capture/frame_source.hpp",
            ROOT / "src/core/hash.cpp",
        ]
        entries = [{"file": str(ROOT / "src/core/hash.cpp")}]
        with mock.patch.object(MODULE.Path, "exists", return_value=True):
            with mock.patch.object(
                MODULE.Path, "read_text", return_value=json.dumps(entries)
            ):
                self.assertEqual(
                    MODULE.select_tidy_files(files, database),
                    [ROOT / "src/core/hash.cpp"],
                )

    def test_tidy_targets_exclude_build_tree_and_vendor_code(self):
        files = [
            ROOT / "src/application/scenes.cpp",
            ROOT
            / "build/dev/_deps/catch2-src/src/catch2/benchmark/detail/catch_analyse.cpp",
        ]
        database = [
            {"file": str(ROOT / "src/application/scenes.cpp")},
            {
                "file": str(
                    ROOT
                    / "build/dev/_deps/catch2-src/src/catch2/benchmark/detail/catch_analyse.cpp"
                )
            },
        ]
        self.assertEqual(MODULE.select_tidy_files(files, None), files)
        with mock.patch.object(MODULE.Path, "exists", return_value=True):
            with mock.patch.object(
                MODULE.Path, "read_text", return_value=json.dumps(database)
            ):
                self.assertEqual(
                    MODULE.select_tidy_files(
                        files, ROOT / "build/dev/compile_commands.json"
                    ),
                    [ROOT / "src/application/scenes.cpp"],
                )

    def test_brace_space_normalization_preserves_clang_format_comparison(self):
        self.assertEqual(
            MODULE.normalize_brace_spacing(
                "int count { 123 };\nif (ready) {\n    run();\n}\n"
            ),
            "int count {123};\nif (ready) {\n    run();\n}\n",
        )

    def test_template_initialization_rejects_size_t_equals_and_temporary_literals(self):
        source = (
            "for (std::size_t index = 0; index < 4; ++index) {}\n"
            "auto count = std::size_t{4};\n"
        )
        with mock.patch.object(MODULE.Path, "read_text", return_value=source):
            with mock.patch("builtins.print") as print_output:
                self.assertEqual(
                    MODULE.check_template_initialization([ROOT / "sample.cpp"]), 1
                )
        self.assertTrue(print_output.called)

    def test_template_initialization_rejects_missing_brace_spaces_and_size_suffixes(
        self,
    ):
        source = (
            "std::size_t count { 256uz * 1024 * 1024 };\n"
            "int value {123};\n"
            "value = { 1,2 };\n"
        )
        with mock.patch.object(MODULE.Path, "read_text", return_value=source):
            with mock.patch("builtins.print") as print_output:
                self.assertEqual(
                    MODULE.check_template_initialization([ROOT / "sample.cpp"]), 1
                )
        self.assertTrue(print_output.called)

    def test_template_initialization_ignores_numbers_and_braces_in_strings(self):
        source = (
            "constexpr std::size_t K_PPM_BYTES { "
            'std::string_view("P6\\n2 1\\n255\\n").size() + 6uz };\n'
            'const auto K_TEXT = "std::size_t count { 12 }; for (int i = 0;";\n'
            "constexpr std::size_t K_CHARACTER { sizeof('1') };\n"
        )
        with mock.patch.object(MODULE.Path, "read_text", return_value=source):
            self.assertEqual(
                MODULE.check_template_initialization([ROOT / "sample.cpp"]), 0
            )

    def test_template_initialization_still_checks_literals_outside_strings(self):
        source = (
            "constexpr std::size_t K_PPM_BYTES { "
            'std::string_view("P6\\n2 1\\n255\\n").size() + 6 };\n'
        )
        with mock.patch.object(MODULE.Path, "read_text", return_value=source):
            with mock.patch("builtins.print") as print_output:
                self.assertEqual(
                    MODULE.check_template_initialization([ROOT / "sample.cpp"]), 1
                )
        self.assertIn("use uz suffixes", str(print_output.call_args_list))

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
