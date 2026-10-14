"""Synthetic dependency evidence; no rights or reconstruction qualification."""

import copy
import importlib.util
import io
import json
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("dependency_manifest", ROOT / "tools/check_dependency_manifest.py")
audit = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(audit)


class DependencyManifestTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        scope = patch.object(audit, "REQUIRED_PROFILES", frozenset({"training"}))
        scope.start()
        self.addCleanup(scope.stop)

    def binding(self, path, data, kind="tracked"):
        target = self.root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        return {"path": path, "kind": kind, "sha256": audit.digest(data), "bytes": len(data)}

    def fixture(self):
        inventory = [{"path": "site/example.py", "bytes": 1, "sha256": "0" * 64, "record_check": "MATCH"}]
        notices = [{"path": "site/LICENSE", "bytes": 1, "sha256": "1" * 64}]
        package = {"name": "example", "version": "1.0", "metadata_sha256": "2" * 64,
                   "record_sha256": "3" * 64, "files": inventory, "notices": notices,
                   "installed_files_sha256": audit.digest(audit.canonical(inventory)),
                   "integrity": "VERIFIED_RECORD", "issues": []}
        identity = {key: package[key] for key in ("metadata_sha256", "record_sha256", "installed_files_sha256")}
        selected = {key: package[key] for key in ("name", "version", *identity)}
        requirement = self.binding("requirements.txt", b"example==1.0\n")
        selection = {"environments": {"training": {"packages": [selected]}}}
        reference = self.binding("selection.json", json.dumps(selection).encode())
        receipt = {"schema_version": 1, "requirements_match": True, "deviations": [],
                   "unlisted_distributions": [], "uninspected_legacy_metadata": [],
                   "requirements_sha256": requirement["sha256"], "packages": [package]}
        raw = self.binding("artifacts/receipt.json", json.dumps(receipt).encode(), "local")
        row = {"name": "example", "version": "1.0", "source": {"status": "METADATA_ONLY"},
               "archive": {"status": "UNAVAILABLE"}, "notice": {"files": 1,
               "inventory_sha256": audit.digest(audit.canonical(notices))},
               "installed": identity, "rights": "NOT_REVIEWED", "gaps": ["TRANSITIVE_RIGHTS_UNREVIEWED"]}
        return {"schema_version": 1, "status": "IN_PROGRESS", "rights_qualified": False,
                "inputs": [requirement, reference, raw], "profiles": [{"id": "training", "kind": "worker",
                "selection": "selection.json", "requirements": "requirements.txt",
                "receipt": "artifacts/receipt.json", "components": [row]}]}

    def archive(self, entries):
        path = self.root / "fixture.tar.gz"
        with tarfile.open(path, "w:gz") as output:
            for name, data, linked in entries:
                entry = tarfile.TarInfo(name)
                entry.size = len(data)
                if linked:
                    entry.type = tarfile.SYMTYPE
                    entry.linkname = "../../outside"
                output.addfile(entry, io.BytesIO(data))
        return path

    def test_inventory_is_valid_with_gaps_without_rights_or_backend_execution(self):
        manifest = self.fixture()
        result = audit.check_manifest(self.root, manifest, True)
        self.assertEqual(result["components"], 1)
        self.assertEqual(result["open_gaps"], 1)
        self.assertFalse(result["rights_qualified"])
        (self.root / "artifacts/receipt.json").unlink()
        self.assertFalse(audit.check_manifest(self.root, manifest)["local_evidence_checked"])
        with self.assertRaises(OSError):
            audit.check_manifest(self.root, manifest, True)

    def test_changed_selection_requirements_notices_and_receipts_fail(self):
        for name in ("selection.json", "requirements.txt", "artifacts/receipt.json"):
            with self.subTest(name=name):
                manifest = self.fixture()
                (self.root / name).write_bytes(b"changed")
                with self.assertRaisesRegex(ValueError, "Changed evidence"):
                    audit.check_manifest(self.root, manifest, True)
        manifest = self.fixture()
        manifest["profiles"][0]["components"][0]["notice"]["files"] = 0
        with self.assertRaisesRegex(ValueError, "notice inventory"):
            audit.check_manifest(self.root, manifest, True)

    def test_omitted_components_wrong_versions_and_unbound_selections_fail(self):
        for mutation in (lambda m: m["profiles"][0]["components"].clear(),
                         lambda m: m["profiles"][0]["components"][0].update(version="2.0"),
                         lambda m: m["inputs"].pop(1),
                         lambda m: m["profiles"].append(copy.deepcopy(m["profiles"][0]))):
            manifest = self.fixture()
            mutation(manifest)
            with self.assertRaises(ValueError):
                audit.check_manifest(self.root, manifest, True)

    def test_bad_raw_inventory_and_package_deviations_are_rejected(self):
        manifest = self.fixture()
        original = audit.load_json(self.root / "artifacts/receipt.json")
        for mutate in (lambda r: r["packages"][0]["files"][0].update(bytes=2),
                       lambda r: r["packages"].append(copy.deepcopy(r["packages"][0])),
                       lambda r: r["packages"][0].update(integrity="UNVERIFIED_RECORD"),
                       lambda r: r.update(unlisted_distributions=["extra"]),
                       lambda r: r.update(requirements_match=False)):
            receipt = copy.deepcopy(original)
            mutate(receipt)
            with self.assertRaises(ValueError):
                audit.receipt_packages(receipt)

    def test_unsafe_paths_symlink_escape_duplicate_keys_and_limits_fail(self):
        self.binding("ordinary.json", b"{}")
        for name in ("../ordinary.json", "/ordinary.json", "x/../ordinary.json", "x\\ordinary.json", "./ordinary.json"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                audit.contained(self.root, name)
        manifest = self.fixture()
        manifest["inputs"][-1]["path"] = "../absent-local-receipt"
        with self.assertRaisesRegex(ValueError, "relative evidence"):
            audit.check_manifest(self.root, manifest)
        outside = self.root.parent / (self.root.name + "-outside")
        outside.write_bytes(b"outside")
        self.addCleanup(outside.unlink)
        try:
            (self.root / "link").symlink_to(outside)
        except OSError:
            pass  # Windows may deny symlinks; the path cases still execute.
        else:
            with self.assertRaises(ValueError):
                audit.contained(self.root, "link")
        for data in (b'{"x":1,"x":2}', b'{"x":NaN}'):
            self.binding("bad.json", data)
            with self.assertRaises(ValueError):
                audit.load_json(self.root / "bad.json")
        with self.assertRaises(ValueError):
            audit.read_file(self.root / "ordinary.json", 1)

    def test_notice_scan_never_extracts_paths_and_rejects_ambiguous_or_linked_notices(self):
        path = self.archive([("../../escape", b"unused", False), ("pkg/LICENSE", b"synthetic notice", False)])
        self.assertEqual(audit.archive_notice(path, "pkg/LICENSE"), b"synthetic notice")
        self.assertEqual(set(p.name for p in self.root.iterdir()), {"fixture.tar.gz"})
        for entries in ([('pkg/LICENSE', b'a', False), ('pkg/LICENSE', b'b', False)],
                        [('pkg/LICENSE', b'', True)], [('pkg/OTHER', b'a', False)]):
            with self.assertRaises(ValueError):
                audit.archive_notice(self.archive(entries), "pkg/LICENSE")
        with patch.object(audit, "MAX_NOTICE", 1), self.assertRaises(ValueError):
            audit.archive_notice(self.archive([('pkg/LICENSE', b'too large', False)]), "pkg/LICENSE")
        with patch.object(audit, "MAX_EXPANDED", 1), self.assertRaises(ValueError):
            audit.archive_notice(self.archive([('pkg/LICENSE', b'too large', False)]), "pkg/LICENSE")

    def test_only_declared_single_notice_whitespace_change_is_permitted(self):
        audit.compare_notice(b"Title \nTerms\n", b"Title\nTerms\n", "REMOVE_ONE_FIRST_LINE_TRAILING_SPACE")
        for original, retained, policy in ((b"Title\nTerms\n", b"Title\nTerms\n", "REMOVE_ONE_FIRST_LINE_TRAILING_SPACE"),
                                           (b"Title \nTerms\n", b"Title\nOther\n", "REMOVE_ONE_FIRST_LINE_TRAILING_SPACE"),
                                           (b"Title \nTerms\n", b"Title\nTerms\n", "EXACT"),
                                           (b"Title\n", b"Title\n", "TRIM_ALL")):
            with self.assertRaises(ValueError):
                audit.compare_notice(original, retained, policy)

    def test_source_trace_comparisons_require_both_bound_identical_files(self):
        manifest = self.fixture()
        manifest["comparisons"] = [{"left": "requirements.txt", "right": "selection.json"}]
        with self.assertRaisesRegex(ValueError, "comparison"):
            audit.check_manifest(self.root, manifest)
        manifest["comparisons"][0]["right"] = "missing"
        with self.assertRaisesRegex(ValueError, "comparison"):
            audit.check_manifest(self.root, manifest)

    def test_native_archive_pins_versions_and_rehashed_notice_changes_are_checked(self):
        with patch.object(audit, "REQUIRED_PROFILES", frozenset({"native"})):
            self.native_archive_case()

    def native_archive_case(self):
        original = self.archive([("pkg/LICENSE", b"Synthetic license terms\n", False)]).read_bytes()
        archive = self.binding(".deps/example.tar.gz", original, "local")
        archive["archive"] = True
        notice = self.binding("third_party/example-LICENSE", b"Synthetic license terms\n")
        cmake = self.binding("cmake/dependencies.cmake",
                             f"we_archive(example example.tar.gz https://source.example/v1.0 {archive['sha256']})\n".encode())
        manifest = {"schema_version": 1, "status": "IN_PROGRESS", "rights_qualified": False,
                    "inputs": [archive, notice, cmake], "profiles": [{"id": "native", "kind": "native-archives",
                    "components": [{"name": "example", "version": "1.0", "rights": "NOT_REVIEWED",
                    "source": {"url": "https://source.example/v1.0"}, "archive": {"sha256": archive["sha256"]},
                    "notice": {"path": notice["path"], "member": "pkg/LICENSE", "archive_sha256": notice["sha256"],
                               "sha256": notice["sha256"], "comparison": "EXACT"}, "gaps": ["TRANSITIVE_RIGHTS_UNREVIEWED"]}]}]}
        self.assertEqual(audit.check_manifest(self.root, manifest, True)["components"], 1)
        row = manifest["profiles"][0]["components"][0]
        row["version"] = "2.0"
        with self.assertRaisesRegex(ValueError, "declaration differs"):
            audit.check_manifest(self.root, manifest, True)
        row["version"] = "1.0"
        manifest["inputs"][1] = self.binding(notice["path"], b"Different license terms\n")
        row["notice"]["sha256"] = manifest["inputs"][1]["sha256"]
        with self.assertRaisesRegex(ValueError, "notice differs"):
            audit.check_manifest(self.root, manifest, True)
        with self.assertRaisesRegex(ValueError, "changed before"):
            audit.archive_notice(self.root / archive["path"], "pkg/LICENSE", "0" * 64)

    def test_omitting_an_entire_selected_profile_fails(self):
        manifest = self.fixture()
        manifest["profiles"].clear()
        with self.assertRaisesRegex(ValueError, "profile inventory"):
            audit.check_manifest(self.root, manifest)

    def test_cli_distinguishes_valid_inventory_open_closure_and_invalid_evidence(self):
        result = {"open_gaps": 1, "rights_qualified": False}
        with patch.object(audit, "load_json", return_value=self.fixture()), \
                patch.object(audit, "check_manifest", return_value=result), \
                patch("sys.stdout", new_callable=io.StringIO):
            with patch("sys.argv", ["check"]):
                self.assertEqual(audit.main(), 0)
            with patch("sys.argv", ["check", "--require-closure"]):
                self.assertEqual(audit.main(), 3)
        with patch.object(audit, "load_json", side_effect=ValueError("changed")), \
                patch("sys.argv", ["check"]), patch("sys.stderr", new_callable=io.StringIO):
            self.assertEqual(audit.main(), 2)


if __name__ == "__main__":
    unittest.main()
