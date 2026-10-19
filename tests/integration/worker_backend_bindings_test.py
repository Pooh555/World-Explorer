"""Synthetic import/map evidence checks; no backend, checkpoint, network or GPU."""

import copy
import hashlib
import json
import os
from pathlib import Path
import socket
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import probe_backend_bindings as probe
import check_backend_bindings as checker


def region(path="/usr/lib/library with spaces.so", inode=123, permissions="r-xp"):
    return f"1000-2000 {permissions} 00000000 08:01 {inode} {path}\n".encode()


class MapsTests(unittest.TestCase):
    def test_spaces_and_executable_regions_are_preserved(self):
        files, _ = probe.parse_maps(region(permissions="r--p") + region())
        self.assertEqual(
            files["/usr/lib/library with spaces.so"],
            {"identity": (8, 1, 123), "executable": True},
        )

    def test_exclusions_are_counted_without_inventing_fingerprints(self):
        files, excluded = probe.parse_maps(
            region("[heap]", 0)
            + region("", 0)
            + region("/usr/lib/deleted.so (deleted)")
            + region(r"/usr/lib/ambiguous\012.so")
        )
        self.assertEqual(files, {})
        self.assertEqual(
            excluded, {"anonymous_or_virtual": 2, "deleted_or_ambiguous": 2}
        )

    def test_malformed_intervals_permissions_and_paths_fail(self):
        for data in [
            b"garbage",
            region().replace(b"1000-2000", b"2000-1000"),
            region(permissions="rwxz"),
            region("relative.so"),
            region("/usr/lib/zero\0.so"),
        ]:
            with self.subTest(data=data), self.assertRaises(ValueError):
                probe.parse_maps(data)

    def test_conflicting_inode_for_same_path_fails(self):
        with self.assertRaisesRegex(ValueError, "Ambiguous"):
            probe.parse_maps(region(inode=123) + region(inode=124))

    def test_byte_region_and_file_budgets_fail_independently(self):
        for bound, data in [
            ("MAX_MAP_BYTES", region()),
            ("MAX_REGIONS", region()),
            ("MAX_FILES", region()),
        ]:
            with (
                self.subTest(bound=bound),
                patch.object(probe, bound, 0),
                self.assertRaises(ValueError),
            ):
                probe.parse_maps(data)


@unittest.skipUnless(sys.platform == "linux", "Linux inode/device mapping diagnostics")
class BackingFileTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.path = self.root / "library.so"
        self.path.write_bytes(b"\x7fELFsynthetic backing bytes")
        self.identity = probe.file_identity(self.path.stat())

    def mapping(self):
        return {str(self.path): {"identity": self.identity, "executable": True}}, {}

    def test_exact_inode_and_bytes_pass(self):
        result = probe.fingerprint_mapping(self.path, self.identity, 100)
        self.assertEqual(
            result["sha256"], hashlib.sha256(self.path.read_bytes()).hexdigest()
        )
        self.assertEqual(result["bytes"], self.path.stat().st_size)

    def test_wrong_inode_rejects_even_identical_content(self):
        with self.assertRaisesRegex(ValueError, "inode/device"):
            probe.fingerprint_mapping(
                self.path, (*self.identity[:2], self.identity[2] + 1), 100
            )

    def test_non_elf_excluded_and_exact_size_limit_checked(self):
        probe.fingerprint_mapping(self.path, self.identity, self.path.stat().st_size)
        with self.assertRaisesRegex(ValueError, "byte budget"):
            probe.fingerprint_mapping(
                self.path, self.identity, self.path.stat().st_size - 1
            )
        self.path.write_bytes(b"plain text")
        self.assertIsNone(probe.fingerprint_mapping(self.path, self.identity, 100))

    def test_replaced_path_during_hash_is_rejected(self):
        original = hashlib.sha256

        def changing_digest():
            digest = original()

            def update(data):
                digest.update(data)
                replacement = self.root / "replacement"
                replacement.write_bytes(data)
                replacement.replace(self.path)

            return SimpleNamespace(update=update, hexdigest=digest.hexdigest)

        with (
            patch.object(probe.hashlib, "sha256", changing_digest),
            self.assertRaisesRegex(ValueError, "changed during"),
        ):
            probe.fingerprint_mapping(self.path, self.identity, 100)

    def test_disappearing_mapping_is_rejected_before_publication(self):
        with (
            patch.object(
                probe, "maps_snapshot", side_effect=[self.mapping(), ({}, {})]
            ),
            self.assertRaisesRegex(ValueError, "disappeared"),
        ):
            probe.collect_mapped_elf(123, self.root)

    def test_in_place_change_after_hash_is_rejected_before_publication(self):
        def later():
            self.path.write_bytes(b"\x7fELFchanged after fingerprint")
            return self.mapping()

        calls = [lambda: self.mapping(), later]
        with (
            patch.object(
                probe, "maps_snapshot", side_effect=lambda pid: calls.pop(0)()
            ),
            self.assertRaisesRegex(ValueError, "before publication"),
        ):
            probe.collect_mapped_elf(123, self.root)

    def test_collect_reports_exact_bounded_files_and_exclusions(self):
        text = self.root / "text"
        text.write_text("text")
        files = self.mapping()[0]
        files[str(text)] = {
            "identity": probe.file_identity(text.stat()),
            "executable": False,
        }
        with patch.object(probe, "maps_snapshot", return_value=(files, {})):
            result = probe.collect_mapped_elf(123, self.root)
        self.assertEqual(list(result["nodes"]), ["repo:library.so"])
        self.assertEqual(result["excluded"]["non_elf_or_device"], 1)
        with (
            patch.object(probe, "MAX_TOTAL_BYTES", 1),
            patch.object(probe, "maps_snapshot", return_value=self.mapping()),
            self.assertRaises(ValueError),
        ):
            probe.collect_mapped_elf(123, self.root)


class GuardTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.work = Path(temporary.name)
        self.hook, self.denied = probe.diagnostic_guard(self.work)

    def test_process_network_pickle_and_checkpoint_actions_fail(self):
        actions = [
            (name, ())
            for name in (
                "subprocess.Popen",
                "os.system",
                "os.exec",
                "os.posix_spawn",
                "pickle.find_class",
                "socket.getaddrinfo",
            )
        ]
        actions += [
            ("socket.connect", (SimpleNamespace(family=family), ("localhost", 1)))
            for family in (socket.AF_INET, socket.AF_INET6)
        ]
        actions += [
            ("open", (str(self.work / name), "rb", 0))
            for name in ("checkpoint.pt", "MODEL.SAFETENSORS", "source.mp4")
        ]
        for event, args in actions:
            with self.subTest(event=event, args=args), self.assertRaises(RuntimeError):
                self.hook(event, args)
        self.assertEqual(len(self.denied), len(actions))

    def test_allowed_reads_and_work_writes_and_blocked_external_writes(self):
        self.hook("open", (str(self.work / "log"), "wb", os.O_CREAT | os.O_WRONLY))
        self.hook("open", (str(self.work.parent / "library.py"), "rb", 0))
        for mode, flags in [("wb", os.O_WRONLY), (None, os.O_RDWR), ("r+", 0)]:
            with self.assertRaisesRegex(RuntimeError, "write_outside"):
                self.hook("open", (str(self.work.parent / "write"), mode, flags))

    def test_write_through_directory_symlink_is_blocked(self):
        link = self.work / "link"
        try:
            link.symlink_to(self.work.parent, target_is_directory=True)
        except OSError:
            self.skipTest("Directory symlink unavailable")
        with self.assertRaisesRegex(RuntimeError, "write_outside"):
            self.hook("open", (str(link / "write"), "wb", os.O_WRONLY))

    def test_torch_load_is_blocked_before_next_fixed_import(self):
        torch = SimpleNamespace(
            __version__="synthetic",
            __file__="fake.py",
            version=SimpleNamespace(cuda=None),
            cuda=SimpleNamespace(
                is_available=lambda: False,
                device_count=lambda: 0,
                is_initialized=lambda: False,
            ),
            load=lambda: self.fail("Checkpoint must never be loaded"),
        )
        observed = []

        def importing(name):
            observed.append(name)
            if name != "torch":
                with self.assertRaisesRegex(RuntimeError, "does not load"):
                    torch.load()
            return (
                torch
                if name == "torch"
                else SimpleNamespace(__version__="synthetic", __file__="fake.py")
            )

        with (
            patch.object(probe.importlib, "import_module", side_effect=importing),
            patch.dict(sys.modules, {"torch": torch}),
        ):
            _, capability = probe.run_imports("training")
        self.assertEqual(observed, probe.PROFILES["training"])
        self.assertIn("gsplat.csrc", observed)
        self.assertNotIn("gsplat.cuda._backend", observed)
        self.assertFalse(capability["cuda_initialized"])


class SummaryTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        for directory in ("tools", "third_party", "artifacts", "build"):
            (self.root / directory).mkdir()
        self.library = self.root / "build/library.so"
        self.library.write_bytes(b"\x7fELFsynthetic")
        (self.root / "tools/probe_backend_bindings.py").write_text("synthetic observer")
        (self.root / "third_party/baseline.json").write_text('{"nodes":{}}')
        self.node = {
            **checker.fingerprint(self.library, 100),
            "executable_mapping": True,
        }
        self.raw = {
            "schema_version": 1,
            "status": "IMPLEMENTED_UNVERIFIED",
            "error": None,
            "guard_denials": [],
            "rights_qualified": False,
            "profile": "poses",
            "capabilities": {},
            "modules": {
                name: {"version": "synthetic", "file": "fake.py"}
                for name in probe.PROFILES["poses"]
            },
            "observation": {
                "nodes": {"repo:build/library.so": self.node},
                "listed_bytes": self.node["bytes"],
                "excluded": {},
            },
            "observer_sha256": checker.fingerprint(
                self.root / "tools/probe_backend_bindings.py", 100
            )["sha256"],
        }
        self.path = self.root / "artifacts/receipt.json"
        self.path.write_text(json.dumps(self.raw))
        info, nodes = checker.project_receipt(self.raw, "imports")
        self.summary = {
            "schema_version": 1,
            "status": "IMPLEMENTED_UNVERIFIED",
            "rights_qualified": False,
            "scope": checker.SCOPE,
            "probe": checker.binding(self.root, "tools/probe_backend_bindings.py"),
            "baseline": checker.binding(self.root, "third_party/baseline.json"),
            "nodes": nodes,
            "listed_bytes": self.node["bytes"],
            "baseline_unseen": sorted(nodes),
            "baseline_changed": [],
            "profiles": {
                "synthetic": {
                    "kind": "imports",
                    "context": "sandbox",
                    "receipt": checker.binding(self.root, "artifacts/receipt.json"),
                    "observation": info,
                }
            },
        }

    def test_complete_local_and_public_summaries_pass(self):
        self.assertEqual(
            checker.check_summary(self.root, self.summary, True)["nodes"], 1
        )
        self.path.unlink()
        self.assertEqual(checker.check_summary(self.root, self.summary)["nodes"], 1)

    def test_current_byte_change_fails_local_check(self):
        self.library.write_bytes(b"\x7fELFchanged")
        with self.assertRaisesRegex(ValueError, "Current mapped"):
            checker.check_summary(self.root, self.summary, True)

    def test_source_and_raw_receipt_changes_fail_bindings(self):
        for path in (self.path, self.root / "tools/probe_backend_bindings.py"):
            original = path.read_bytes()
            path.write_bytes(original + b" ")
            with self.assertRaisesRegex(ValueError, "bytes differ"):
                checker.check_summary(self.root, self.summary, True)
            path.write_bytes(original)

    def test_rehashed_raw_guard_failure_and_observer_change_fail(self):
        for field, value in [
            ("guard_denials", ["subprocess.Popen"]),
            ("observer_sha256", "0" * 64),
        ]:
            raw = {**self.raw, field: value}
            self.path.write_text(json.dumps(raw))
            self.summary["profiles"]["synthetic"]["receipt"] = checker.binding(
                self.root, "artifacts/receipt.json"
            )
            with self.assertRaises(ValueError):
                checker.check_summary(self.root, self.summary, True)

    def test_omitted_rehashed_raw_node_cannot_hide_from_summary(self):
        raw = copy.deepcopy(self.raw)
        raw["observation"]["nodes"] = {}
        self.path.write_text(json.dumps(raw))
        self.summary["profiles"]["synthetic"]["receipt"] = checker.binding(
            self.root, "artifacts/receipt.json"
        )
        with self.assertRaisesRegex(ValueError, "Raw mapping profile"):
            checker.check_summary(self.root, self.summary, True)

    def test_schema_path_count_sum_and_rights_claims_fail(self):
        changes = [
            ("rights_qualified", True),
            ("scope", "all dependencies certified"),
            ("schema_version", 2),
            ("baseline_unseen", []),
            ("listed_bytes", 1),
            ("profiles", {}),
        ]
        for field, value in changes:
            bad = {**self.summary, field: value}
            with self.subTest(field=field), self.assertRaises(ValueError):
                checker.check_summary(self.root, bad)
        for text in (
            "repo:../escape",
            "system:/tmp/library.so",
            "untyped:build/library.so",
        ):
            with self.subTest(text=text), self.assertRaises(ValueError):
                checker.node_path(self.root, text)

    def test_failed_viewer_and_duplicate_json_keys_fail(self):
        with self.assertRaises(ValueError):
            checker.project_receipt({"exit": 2, "mapped": None}, "viewer")
        self.path.write_text('{"nodes":{},"nodes":{}}')
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            checker.read_json(self.path)


if __name__ == "__main__":
    unittest.main()
