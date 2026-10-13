"""Installed metadata fixtures are synthetic; no reconstruction is certified."""

import base64
import csv
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("worker_audit", ROOT / "tools/audit_worker_environment.py")
audit = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(audit)


class WorkerEnvironmentAuditTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.prefix = self.root / "environment"
        self.site = self.prefix / "site-packages"
        self.info = self.site / "example-1.0.dist-info"
        self.info.mkdir(parents=True)
        self.requirements = self.root / "requirements.txt"
        self.requirements.write_text("example==1.0\n")
        (self.info / "METADATA").write_text(
            "Metadata-Version: 2.4\nName: example\nVersion: 1.0\nLicense: Synthetic café notice\n\n")
        (self.info / "LICENSE").write_text("Synthetic fixture notice, no upstream code")
        (self.site / "example.py").write_text("raise RuntimeError('backend must never be imported')")
        self.inventory()

    def inventory(self, algorithm="sha256"):
        paths = [self.info / "METADATA", self.info / "LICENSE", self.site / "example.py"]
        rows = []
        for path in paths:
            data = path.read_bytes()
            digest = hashlib.new(algorithm, data).digest()
            rows.append([path.relative_to(self.site).as_posix(),
                         algorithm + "=" + base64.urlsafe_b64encode(digest).decode().rstrip("="),
                         str(len(data))])
        rows.append(["example-1.0.dist-info/RECORD", "", ""])
        with (self.info / "RECORD").open("w", newline="") as stream:
            csv.writer(stream).writerows(rows)

    def run_audit(self):
        return audit.audit_environment(self.prefix, [self.site], self.requirements)

    def test_installed_identity_and_notices_without_backend_import_or_metric_invention(self):
        first = self.run_audit()
        second = self.run_audit()
        self.assertEqual(first, second)
        self.assertTrue(first["requirements_match"])
        self.assertFalse(first["metric_qualified"])
        self.assertIsNone(first["backbone_sha256"])
        self.assertEqual(first["metrics"], {"torchmetrics": None, "lpips": None})
        package = first["packages"][0]
        self.assertEqual(package["integrity"], "VERIFIED_RECORD")
        self.assertIsNone(package["wheel_archive_sha256"])
        self.assertEqual(package["notices"][0]["sha256"],
                         hashlib.sha256((self.info / "LICENSE").read_bytes()).hexdigest())

    def test_changed_installed_file_is_unverified_with_a_different_receipt_identity(self):
        original = self.run_audit()["packages"][0]
        (self.site / "example.py").write_text("changed bytes")
        changed = self.run_audit()["packages"][0]
        self.assertEqual(changed["integrity"], "UNVERIFIED_RECORD")
        self.assertTrue(changed["issues"])
        self.assertNotEqual(changed["installed_files_sha256"], original["installed_files_sha256"])

    def test_missing_record_and_unpinned_metadata_do_not_claim_complete_integrity(self):
        (self.info / "RECORD").unlink()
        self.requirements.write_text("missing==2.0\n")
        (self.site / "legacy.egg-info").mkdir()
        report = self.run_audit()
        self.assertFalse(report["requirements_match"])
        self.assertEqual(report["packages"][0]["integrity"], "UNVERIFIED_NO_RECORD")
        self.assertEqual(report["unlisted_distributions"], ["example"])
        self.assertEqual(report["uninspected_legacy_metadata"], ["site-packages/legacy.egg-info"])

    def test_escape_duplicates_missing_files_and_file_budgets_fail_without_receipt(self):
        record = self.info / "RECORD"
        original = record.read_text()
        for row in ("../../outside.py,,\n", "missing.py,,\n", "example.py,,\n"):
            with self.subTest(row=row):
                record.write_text(original + row)
                with self.assertRaises((ValueError, OSError)):
                    self.run_audit()
        record.write_text(original)
        with patch.object(audit, "MAX_FILE_BYTES", 1), self.assertRaises(ValueError):
            self.run_audit()
        with patch.object(audit, "MAX_FILES", 1), self.assertRaises(ValueError):
            self.run_audit()

    def test_symlink_to_another_environment_is_rejected(self):
        module = self.site / "example.py"
        outside = self.root / "outside.py"
        outside.write_bytes(module.read_bytes())
        module.unlink()
        try:
            module.symlink_to(outside)
        except OSError:
            self.skipTest("symlink creation unavailable on this profile")
        with self.assertRaises(ValueError):
            self.run_audit()

    def test_unhashed_bytecache_is_allowed_but_unhashed_source_is_not(self):
        record = self.info / "RECORD"
        (self.site / "example.pyc").write_bytes(b"synthetic bytecache")
        with record.open("a") as stream:
            stream.write("example.pyc,,\n")
        self.assertEqual(self.run_audit()["packages"][0]["integrity"], "VERIFIED_RECORD")
        text = record.read_text().splitlines()
        text = ["example.py,," if line.startswith("example.py,") else line for line in text]
        record.write_text("\n".join(text) + "\n")
        self.assertEqual(self.run_audit()["packages"][0]["integrity"], "UNVERIFIED_RECORD")

    def test_sha512_record_and_direct_wheel_version_are_checked(self):
        self.inventory("sha512")
        report = self.run_audit()
        self.assertEqual(report["packages"][0]["integrity"], "VERIFIED_RECORD")
        self.requirements.write_text(
            "example @ https://example.invalid/example-1.0-cp310-cp310-linux_x86_64.whl\n")
        self.assertTrue(self.run_audit()["requirements_match"])
        self.requirements.write_text("example==2.0\n")
        self.assertFalse(self.run_audit()["requirements_match"])
        self.requirements.write_text("example>=1.0\n")
        with self.assertRaises(ValueError):
            self.run_audit()

    def test_atomic_publication_never_replaces_existing_output(self):
        path = self.root / "receipt.json"
        audit.publish_receipt(path, self.run_audit())
        before = path.read_bytes()
        with self.assertRaises(FileExistsError):
            audit.publish_receipt(path, {"replacement": True})
        self.assertEqual(path.read_bytes(), before)
        self.assertEqual(list(self.root.glob(".worker-audit-*")), [])
        self.assertEqual(json.loads(before)["packages"][0]["integrity"], "VERIFIED_RECORD")

    def test_explicit_backbone_bytes_are_hashed_without_loading_or_qualifying_metrics(self):
        report = self.run_audit()
        audit.record_backbone(report, None)
        self.assertEqual(report["backbone_status"], "NOT_SELECTED")
        weights = self.root / "alexnet.pth"
        weights.write_bytes(b"synthetic bytes; deliberately not a pickle")
        audit.record_backbone(report, weights)
        self.assertEqual(report["backbone_sha256"], hashlib.sha256(weights.read_bytes()).hexdigest())
        self.assertEqual(report["backbone_status"], "LOCAL_BYTES_UNREVIEWED")
        self.assertFalse(report["metric_qualified"])
        with self.assertRaises(FileNotFoundError):
            audit.record_backbone(report, self.root / "missing.pth")

    def test_inputs_changing_during_hashing_cannot_publish_a_checked_identity(self):
        original_digest = audit.file_digests
        for target in [self.requirements, self.info / "RECORD", self.info / "METADATA"]:
            original_bytes = target.read_bytes()

            def change_input(path, *args, **kwargs):
                result = original_digest(path, *args, **kwargs)
                if path.name == "example.py":
                    target.write_bytes(original_bytes + b"\n")
                return result

            with self.subTest(target=target.name), patch.object(audit, "file_digests", change_input):
                with self.assertRaises(ValueError):
                    self.run_audit()
            target.write_bytes(original_bytes)

    def test_record_and_inventory_digests_use_the_same_file_read(self):
        self.inventory("sha512")
        self.assert_shared_file_read()

    def test_read_observer_matches_resolved_files_through_an_aliased_site(self):
        # Exercise equivalent spellings on every OS without requiring symlink
        # privileges: the auditor resolves paths before opening installed files.
        self.site = self.site / ".." / self.site.name
        self.info = self.site / self.info.name
        self.inventory("sha512")
        self.assert_shared_file_read()

    def assert_shared_file_read(self):
        module = (self.site / "example.py").resolve(strict=True)
        original = module.read_bytes()
        original_open = Path.open
        reads = []

        def read_once(path, *args, **kwargs):
            if path == module:
                reads.append(path)
                # A second read would observe different installed bytes and
                # must never form a mixed SHA-256/SHA-512 receipt.
                if len(reads) > 1:
                    with original_open(path, "wb") as stream:
                        stream.write(b"x" * len(original))
            return original_open(path, *args, **kwargs)

        with patch.object(Path, "open", read_once):
            package = self.run_audit()["packages"][0]
        self.assertEqual(package["integrity"], "VERIFIED_RECORD")
        self.assertEqual(len(reads), 1)
        row = next(row for row in package["files"] if row["path"].endswith("example.py"))
        self.assertEqual(row["sha256"], hashlib.sha256(original).hexdigest())


if __name__ == "__main__":
    unittest.main()
