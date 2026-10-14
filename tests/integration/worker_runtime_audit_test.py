"""Synthetic loader receipts; no backend, GPU or rights qualification."""

import copy
import importlib.util
import io
import json
from pathlib import Path, PurePosixPath, PureWindowsPath
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
SPEC = importlib.util.spec_from_file_location("runtime_audit", ROOT / "tools/audit_runtime_dependencies.py")
audit = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(audit)
import check_dependency_manifest as checker

HEADER = "Class: ELF64\nData: 2's complement, little endian\nMachine: Advanced Micro Devices X86-64\n"
FIXTURE_ROOT = PurePosixPath("/synthetic-runtime")
SELECTED_PATH = audit.selected_path


def fixture_selected_path(text, root, directory=False):
    """Translate synthetic Linux paths to host files; never run a host loader.

    The fake loader speaks POSIX paths even on Windows. Validate its directory
    spelling before translation so a host drive colon cannot become a Linux
    library-list separator. Real capture keeps its original Linux validator.
    """
    if not isinstance(text, str) or not text or len(text) > 4096 or any(c in text for c in "\n\r\0"):
        raise ValueError("Invalid selected runtime path")
    logical = PurePosixPath(text)
    if logical.is_relative_to(FIXTURE_ROOT):
        text = logical.relative_to(FIXTURE_ROOT).as_posix()
    if not directory:
        return SELECTED_PATH(text, root)
    # Only directories in the synthetic repository namespace are translated.
    if logical.is_absolute() and not logical.is_relative_to(FIXTURE_ROOT):
        raise ValueError("Directory is outside synthetic runtime namespace")
    if ":" in text or "\\" in text:
        raise ValueError("Library path contains a separator or host path spelling")
    path = (root / text).resolve(strict=True)
    if not path.is_relative_to(root) or not path.is_dir():
        raise ValueError("Library path is not a contained fixture directory")
    return path


class FixtureCommands:
    def __init__(self, root, change=None, missing=False):
        self.root = root
        self.receipts = []
        self.change = change
        self.missing = missing

    def run(self, argv, expected=(0,)):
        if "--file-header" in argv:
            text = HEADER
            if argv[-1].endswith("program"):
                text += "0 (NEEDED) Shared library: [libbase.so]\n"
            else:
                text += "0 (SONAME) Library soname: [libbase.so]\n"
        elif "--list" in argv:
            text = f"libbase.so => {FIXTURE_ROOT / 'build/libbase.so'} (0x1234)\n"
            if self.missing:
                text = "libbase.so => not found\n"
            if self.change:
                self.change()
        else:
            raise AssertionError("Unexpected command, including any backend or application launch")
        receipt = {"argv": argv, "exit": 0, "output": text}
        self.receipts.append(receipt)
        return receipt


class RuntimeAuditTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name).resolve()
        (self.root / "build").mkdir()
        for name in ["program", "libbase.so", "loader", "readelf", "pacman"]:
            (self.root / "build" / name).write_bytes(b"\x7fELFsynthetic-not-executable")
        self.selection = {"schema_version": 1, "loader": "build/loader", "readelf": "build/readelf",
                          "package_tool": "build/pacman", "roots": [{"id": "synthetic", "path": "build/program",
                          "sha256": audit.file_digest(self.root / "build/program")[0],
                          "scope": "synthetic-loader", "library_path": ["build"]}]}

    def capture(self, runner=None):
        with patch("sys.stdout", new_callable=io.StringIO), patch.object(audit, "selected_path", fixture_selected_path):
            return audit.capture(self.root, self.selection, runner or FixtureCommands(self.root))

    def test_synthetic_loader_paths_are_linux_paths_on_windows_hosts(self):
        runner = FixtureCommands(PureWindowsPath("D:/fixture root"))
        result = runner.run(["synthetic-loader", "--list"])
        parsed = audit.parse_loader(result["output"])
        self.assertEqual(parsed["resolved"]["libbase.so"], str(FIXTURE_ROOT / "build/libbase.so"))
        self.assertEqual(fixture_selected_path(parsed["resolved"]["libbase.so"], self.root), self.root / "build/libbase.so")
        self.assertEqual(fixture_selected_path(str(FIXTURE_ROOT / "build"), self.root, directory=True), self.root / "build")

    def test_fixture_directory_translation_rejects_invalid_and_escaping_paths(self):
        for text in ("build:second", "bad\npath", "bad\0path", "../", str(FIXTURE_ROOT / "../"),
                     "build/program", "D:/fixture/build", "/foreign/build"):
            with self.subTest(text=text), self.assertRaises((ValueError, OSError)):
                fixture_selected_path(text, self.root, directory=True)
        # The actual Linux validator still rejects a literal list separator.
        if sys.platform == "linux":
            directory = self.root / "build:second"
            directory.mkdir()
            with self.assertRaisesRegex(ValueError, "separator"):
                SELECTED_PATH(str(directory), self.root, directory=True)

    def test_elf_parser_preserves_needed_rpath_runpath_and_interpreter(self):
        text = HEADER + "0 (NEEDED) Shared library: [libbase.so]\n0 (RPATH) Library rpath: [$ORIGIN/lib]\n"
        text += "0 (RUNPATH) Library runpath: [$ORIGIN/new]\n[Requesting program interpreter: /lib64/loader]\n"
        result = audit.parse_elf(text)
        self.assertEqual(result["needed"], ["libbase.so"])
        self.assertEqual(result["rpath"], ["$ORIGIN/lib"])
        self.assertEqual(result["runpath"], ["$ORIGIN/new"])
        self.assertEqual(result["interpreter"], "/lib64/loader")
        for altered in (text.replace("ELF64", "ELF32"), text.replace("little endian", "big endian"),
                        text + "0 (RUNPATH) Library runpath: [second]\n", text + "0 (NEEDED) Shared library: []\n"):
            with self.assertRaises(ValueError):
                audit.parse_elf(altered)

    def test_loader_parser_preserves_absolute_aliases_and_virtual_mapping(self):
        result = audit.parse_loader("linux-vdso.so.1 (0x12)\n/lib64/loader => /usr/lib/loader (0x23)\n"
                                    "libbase.so => /usr/lib/libbase.so (0x34)\nmissing.so => not found\n")
        self.assertEqual(result["resolved"]["/lib64/loader"], "/usr/lib/loader")
        self.assertEqual(result["virtual"], ["linux-vdso.so.1"])
        self.assertEqual(result["missing"], ["missing.so"])
        for text in ("unexpected diagnostic", "libbase.so => /usr/lib/a (0x1)\nlibbase.so => /usr/lib/b (0x2)"):
            with self.assertRaises(ValueError):
                audit.parse_loader(text)

    def test_loaded_soname_repairs_loader_alias_without_inventing_direct_path_resolution(self):
        nodes = {"root": {"elf": {"needed": ["loader", "/wrong/loader"], "soname": []}},
                 "loader-node": {"elf": {"needed": [], "soname": ["loader"]}}}
        aliases, edges, missing = audit.resolve_edges("root", {"/lib64/loader": "loader-node"}, nodes)
        self.assertEqual(aliases["loader"], "loader-node")
        self.assertEqual(edges[0]["to"], "loader-node")
        self.assertIsNone(edges[1]["to"])
        self.assertEqual(missing, ["root:/wrong/loader"])
        nodes["root"]["elf"]["soname"] = ["loader"]
        with self.assertRaisesRegex(ValueError, "Ambiguous"):
            audit.resolve_edges("root", {"loader": "loader-node"}, nodes)

    def test_capture_has_checked_bytes_and_explicit_context_without_application_execution(self):
        report = self.capture()
        self.assertEqual(len(report["nodes"]), 2)
        self.assertFalse(report["rights_qualified"])
        self.assertEqual(report["roots"][0]["unresolved"], [])
        self.assertEqual(report["roots"][0]["edges"][0]["to"], "repo:build/libbase.so")
        loader = next(c for c in report["commands"] if "--list" in c["argv"])
        self.assertEqual(loader["argv"][:4], [str(self.root / "build/loader"), "--list", "--library-path", str(self.root / "build")])

    def test_missing_dependency_is_retained_as_unresolved_not_fabricated_success(self):
        report = self.capture(FixtureCommands(self.root, missing=True))
        self.assertIn("libbase.so", report["roots"][0]["unresolved"])
        self.assertIsNone(report["roots"][0]["edges"][0]["to"])
        self.assertFalse(report["rights_qualified"])

    def test_mutable_roots_and_libraries_abort_before_receipt_publication(self):
        for name in ("program", "libbase.so"):
            with self.subTest(name=name):
                original = (self.root / "build" / name).read_bytes()
                if name == "program":
                    runner = FixtureCommands(self.root, change=lambda: (self.root / "build/program").write_bytes(b"changed"))
                else:
                    runner = FixtureCommands(self.root)
                    run = runner.run
                    def changing(argv, expected=(0,)):
                        result = run(argv, expected)
                        if "--file-header" in argv and argv[-1].endswith("libbase.so"):
                            (self.root / "build/libbase.so").write_bytes(b"changed")
                        return result
                    runner.run = changing
                with self.assertRaisesRegex(ValueError, "changed"):
                    self.capture(runner)
                (self.root / "build" / name).write_bytes(original)

    def test_root_node_byte_and_path_limits_are_checked(self):
        with patch.object(audit, "MAX_NODES", 1), self.assertRaises(ValueError):
            self.capture()
        with patch.object(audit, "MAX_TOTAL_BYTES", 1), self.assertRaises(ValueError):
            self.capture()
        for roots in ([], self.selection["roots"] * 2):
            selected = copy.deepcopy(self.selection)
            selected["roots"] = roots
            with self.assertRaises(ValueError):
                audit.capture(self.root, selected, FixtureCommands(self.root))
        for name in ("bad\npath", "bad\0path"):
            with self.assertRaises(ValueError):
                audit.selected_path(name, self.root)

    def test_replacement_after_hash_cannot_pair_old_bytes_with_new_metadata(self):
        original = audit.file_digest
        def replacing(path, *args, **kwargs):
            result = original(path, *args, **kwargs)
            if path.name == "libbase.so":
                path.write_bytes(b"\x7fELFreplacement after hashing")
            return result
        with patch.object(audit, "file_digest", side_effect=replacing), self.assertRaisesRegex(ValueError, "changed"):
            self.capture()

    def test_installed_receipt_corruption_fails_before_loader_inspection(self):
        prefix = self.root / "environment"
        prefix.mkdir()
        receipt = self.root / "receipt.json"
        receipt.write_text(json.dumps({"schema_version": 1, "requirements_match": False}))
        with patch.object(audit, "selected_path", fixture_selected_path), self.assertRaises(ValueError):
            audit.worker_owners(self.root, [{"prefix": "environment", "receipt": "receipt.json"}])

    def test_interpreter_metadata_must_match_selected_binary_directory(self):
        cfg = self.root / "interpreter.cfg"
        cfg.write_bytes(f"home = {FIXTURE_ROOT / 'build'}\nimplementation = CPython\nversion_info = 3.10.22\n".encode())
        self.selection["roots"][0]["interpreter_metadata"] = {"pyvenv_cfg": "interpreter.cfg"}
        self.assertEqual(self.capture()["interpreter_metadata"][0]["files"]["declared_version"], "3.10.22")
        cfg.write_bytes(b"home = wrong\nimplementation = Other\nversion_info = 0\n")
        with self.assertRaises(ValueError):
            self.capture()

    def summary(self):
        report = self.capture()
        selection = self.root / "selection.json"
        selection.write_text(json.dumps(self.selection))
        tool = self.root / "tools/audit_runtime_dependencies.py"
        tool.parent.mkdir()
        tool.write_bytes(b"synthetic auditor identity")
        report["auditor_sha256"] = audit.file_digest(tool)[0]
        raw = self.root / "receipt.json"
        raw.write_text(json.dumps(report))
        summary = {field: report[field] for field in ["schema_version", "rights_qualified", "nodes", "roots",
                   "system_packages", "interpreter_metadata", "remaining", "auditor_sha256"]}
        summary["selection"] = {"path": "selection.json", "sha256": audit.file_digest(selection)[0]}
        summary["receipt"] = {"path": "receipt.json", "sha256": audit.file_digest(raw)[0]}
        return summary

    def test_runtime_summary_rejects_missing_roots_edges_aliases_and_wrong_fingerprints(self):
        original = self.summary()
        self.assertEqual(checker.check_runtime_summary(self.root, original, False)["nodes"], 2)
        self.assertEqual(checker.check_runtime_summary(self.root, original, True)["unresolved"], 0)
        for mutate in (lambda d: d["roots"].clear(),
                       lambda d: d["roots"][0]["edges"].clear(),
                       lambda d: d["roots"][0]["aliases"].clear(),
                       lambda d: d["nodes"]["repo:build/program"].update(sha256="0" * 64)):
            d = copy.deepcopy(original)
            mutate(d)
            with self.assertRaises(ValueError):
                checker.check_runtime_summary(self.root, d, False)

    def test_runtime_summary_current_bytes_and_raw_receipt_are_independently_checked(self):
        summary = self.summary()
        (self.root / "build/libbase.so").write_bytes(b"changed caller bytes")
        with self.assertRaisesRegex(ValueError, "payload differs"):
            checker.check_runtime_summary(self.root, summary, True)
        (self.root / "receipt.json").write_bytes(b"changed raw receipt")
        with self.assertRaisesRegex(ValueError, "receipt changed"):
            checker.check_runtime_summary(self.root, summary, True)

    @unittest.skipUnless(sys.platform == "linux", "bounded pipe runner is Linux-only")
    def test_command_runner_bounds_output_exit_status_and_retains_literal_arguments(self):
        runner = audit.commands()
        value = "$() `literal`"
        result = runner.run([sys.executable, "-c", "import sys; print(sys.argv[1])", value])
        self.assertEqual(result["output"].strip(), value)
        with patch.object(audit, "MAX_OUTPUT", 4), self.assertRaises(ValueError):
            runner.run([sys.executable, "-c", "print('oversized')"])
        with self.assertRaises(ValueError):
            runner.run([sys.executable, "-c", "raise SystemExit(2)"])
        with patch.object(audit, "MAX_COMMANDS", 0), self.assertRaises(ValueError):
            runner.run([sys.executable, "-c", "print('unused')"])


if __name__ == "__main__":
    unittest.main()
