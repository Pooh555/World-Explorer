"""Synthetic installed-owner evidence, with no backend or native package query."""

import copy
import hashlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import audit_binding_owners as audit
from audit_worker_environment import publish_receipt


class Queries:
    def __init__(self, change=None, unowned=False, duplicate=False, no_notice=False):
        self.receipts = []
        self.change = change
        self.unowned = unowned
        self.duplicate = duplicate
        self.no_notice = no_notice

    def run(self, argv, expected=(0,)):
        if argv[1] == "-Qoq":
            output = "fixture\nother\n" if self.duplicate else "fixture\n"
            exit_code = 1 if self.unowned else 0
        elif argv[1] == "-Qi":
            output = "Name : fixture\nVersion : 1.0-1\nURL : https://example.invalid\nLicenses : Fixture-Terms\n"
            exit_code = 0
        elif argv[1] == "-Qlq":
            output = "" if self.no_notice else "/usr/share/licenses/fixture/LICENSE\n"
            exit_code = 0
        else:
            raise AssertionError(
                "Only read-only installed-package queries are permitted"
            )
        if self.change:
            output = self.change(argv, output, len(self.receipts))
        value = {
            "argv": argv,
            "exit": exit_code,
            "output": output,
            "output_sha256": hashlib.sha256(output.encode()).hexdigest(),
        }
        self.receipts.append(value)
        return value


class OwnerTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        for name in ("tools", "artifacts", "build", "env/lib", "env/pkg.dist-info"):
            (self.root / name).mkdir(parents=True, exist_ok=True)
        for name in audit.SOURCES:
            (self.root / name).write_text("synthetic audit source")
        self.worker = self.root / "env/lib/fixture.so"
        self.worker.write_bytes(b"\x7fELFsynthetic worker")
        self.system = self.root / "build/system.so"
        self.system.write_bytes(b"\x7fELFsynthetic system")
        self.system_notice = self.root / "build/LICENSE"
        self.system_notice.write_text("synthetic system terms")
        self.tool = self.root / "build/pacman"
        self.tool.write_bytes(b"synthetic package tool; never executed")
        metadata = self.root / "env/pkg.dist-info/METADATA"
        metadata.write_bytes(b"Name: fixture\nVersion: 1.0\n")
        record = self.root / "env/pkg.dist-info/RECORD"
        record.write_bytes(b"synthetic RECORD identity")
        self.worker_notice = self.root / "env/pkg.dist-info/LICENSE"
        self.worker_notice.write_text("synthetic worker terms")
        files = [
            self.file_row(path.relative_to(self.root / "env").as_posix(), path)
            for path in (self.worker, metadata, record, self.worker_notice)
        ]
        package = {
            "name": "fixture",
            "version": "1.0",
            "integrity": "VERIFIED_RECORD",
            "issues": [],
            "metadata_sha256": self.fingerprint(metadata)["sha256"],
            "record_sha256": self.fingerprint(record)["sha256"],
            "files": files,
            "notices": [{k: files[-1][k] for k in ("path", "sha256", "bytes")}],
        }
        self.installed = {
            "schema_version": 1,
            "requirements_match": True,
            "packages": [package],
        }
        self.worker_receipt = self.root / "artifacts/worker.json"
        self.worker_receipt.write_text(json.dumps(self.installed))
        self.selection = {
            "package_tool": "/usr/bin/pacman",
            "worker_receipts": [{"prefix": "env", "receipt": "artifacts/worker.json"}],
        }
        self.selection_path = self.root / "selection.json"
        self.selection_path.write_text(json.dumps(self.selection))
        self.baseline_raw = {
            "installed_receipts": [
                {
                    "path": "repo:artifacts/worker.json",
                    "sha256": self.fingerprint(self.worker_receipt)["sha256"],
                }
            ]
        }
        self.baseline_raw_path = self.root / "artifacts/baseline.json"
        self.baseline_raw_path.write_text(json.dumps(self.baseline_raw))
        baseline = {
            "selection": {
                "path": "selection.json",
                **self.fingerprint(self.selection_path),
            },
            "receipt": {
                "path": "artifacts/baseline.json",
                **self.fingerprint(self.baseline_raw_path),
            },
        }
        (self.root / "baseline.json").write_text(json.dumps(baseline))
        self.mapping = {
            "baseline": {"path": "baseline.json"},
            "baseline_unseen": [
                "repo:env/lib/fixture.so",
                "system:/usr/lib/fixture.so",
            ],
            "nodes": {
                "repo:env/lib/fixture.so": self.fingerprint(self.worker),
                "system:/usr/lib/fixture.so": self.fingerprint(self.system),
            },
        }
        (self.root / "mapping.json").write_text(json.dumps(self.mapping))
        original_path = audit.mappings.node_path

        def fixture_path(root, key):
            if key.startswith("repo:"):
                return original_path(root, key)
            return {
                "system:/usr/lib/fixture.so": self.system,
                "system:/usr/bin/pacman": self.tool,
                "system:/usr/share/licenses/fixture/LICENSE": self.system_notice,
            }[key]

        # The preceding mapping contract has its own dedicated tests. These
        # fixtures exercise the ownership layer and real temporary file hashes.
        for fixture_patch in (
            patch.object(audit.mappings, "node_path", side_effect=fixture_path),
            patch.object(audit.mappings, "check_summary", return_value={}),
        ):
            fixture_patch.start()
            self.addCleanup(fixture_patch.stop)

    def fingerprint(self, path):
        sha256, size = audit.file_digest(path)
        return {"sha256": sha256, "bytes": size}

    def file_row(self, name, path):
        return {"path": name, **self.fingerprint(path), "record_check": "MATCH"}

    def capture(self, runner=None):
        return audit.capture(
            self.root, "mapping.json", "selection.json", runner or Queries()
        )

    def summary(self):
        raw = self.capture()
        path = self.root / "artifacts/owners.json"
        path.write_text(json.dumps(raw))
        return {
            **audit.public_fields(raw),
            "receipt": audit.reference(self.root, "artifacts/owners.json"),
        }

    def test_complete_current_owner_notice_associations_pass(self):
        summary = self.summary()
        self.assertEqual(
            audit.check_summary(self.root, summary, True, Queries())["notices"], 2
        )
        self.assertEqual(audit.check_summary(self.root, summary)["nodes"], 2)
        self.assertFalse(summary["rights_qualified"])

    def test_capture_uses_only_literal_read_only_package_queries(self):
        runner = Queries()
        self.capture(runner)
        self.assertEqual(
            [row["argv"][1] for row in runner.receipts], ["-Qoq", "-Qi", "-Qlq"] * 2
        )
        self.assertTrue(all(row["argv"][2] == "--" for row in runner.receipts))

    def test_missing_notice_is_retained_as_a_gap(self):
        report = self.capture(Queries(no_notice=True))
        self.assertIn(
            "SEPARATE_INSTALLED_NOTICE_ABSENT",
            report["groups"]["system:fixture"]["gaps"],
        )

    def test_changed_elf_metadata_record_and_notices_fail(self):
        for path in (
            self.worker,
            self.system,
            self.root / "env/pkg.dist-info/METADATA",
            self.root / "env/pkg.dist-info/RECORD",
            self.worker_notice,
        ):
            original = path.read_bytes()
            path.write_bytes(original + b" changed")
            with self.subTest(path=path), self.assertRaises(ValueError):
                self.capture()
            path.write_bytes(original)

    def test_file_change_after_fingerprinting_fails_before_publication(self):
        def change(argv, output, count):
            if count == 0:
                self.worker.write_bytes(b"changed after hash")
            return output

        with self.assertRaisesRegex(ValueError, "before publication"):
            self.capture(Queries(change=change))

    def test_package_database_change_is_rejected(self):
        def change(argv, output, count):
            return output.replace("1.0-1", "2.0-1") if count >= 3 else output

        with self.assertRaisesRegex(ValueError, "association changed"):
            self.capture(Queries(change=change))

    def test_unowned_or_ambiguous_system_file_fails(self):
        for runner in (Queries(unowned=True), Queries(duplicate=True)):
            with self.assertRaises(ValueError):
                self.capture(runner)

    def test_installed_receipt_cannot_be_rebound_silently(self):
        self.worker_receipt.write_text(
            json.dumps({**self.installed, "requirements_match": False})
        )
        with self.assertRaisesRegex(ValueError, "original binding"):
            self.capture()

    def test_missing_record_match_is_rejected_even_after_rebinding_fixture(self):
        package = self.installed["packages"][0]
        package["files"][0]["record_check"] = "NO_HASH"
        self.worker_receipt.write_text(json.dumps(self.installed))
        self.baseline_raw["installed_receipts"][0]["sha256"] = self.fingerprint(
            self.worker_receipt
        )["sha256"]
        self.baseline_raw_path.write_text(json.dumps(self.baseline_raw))
        baseline = json.loads((self.root / "baseline.json").read_text())
        baseline["receipt"].update(self.fingerprint(self.baseline_raw_path))
        (self.root / "baseline.json").write_text(json.dumps(baseline))
        with self.assertRaisesRegex(ValueError, "RECORD inventory"):
            self.capture()

    def test_count_and_byte_budgets_fail(self):
        for name, value in [
            ("MAX_NODES", 1),
            ("MAX_NOTICES", 1),
            ("MAX_NOTICE_BYTES", 1),
        ]:
            with (
                self.subTest(bound=name),
                patch.object(audit, name, value),
                self.assertRaises(ValueError),
            ):
                self.capture()

    def test_missing_owner_or_notice_and_forged_rights_fail(self):
        original = self.summary()
        for mutate in (
            lambda s: s["nodes"].pop("system:/usr/lib/fixture.so"),
            lambda s: s["groups"].pop("system:fixture"),
            lambda s: s.update(rights_qualified=True),
            lambda s: s["groups"]["system:fixture"]["gaps"].clear(),
        ):
            summary = copy.deepcopy(original)
            mutate(summary)
            with self.assertRaises(ValueError):
                audit.check_summary(self.root, summary)
        # Rehashing both raw and public copies cannot hide an omitted installed notice.
        summary = copy.deepcopy(original)
        summary["groups"]["system:fixture"]["notices"].clear()
        summary["groups"]["system:fixture"]["gaps"].append(
            "SEPARATE_INSTALLED_NOTICE_ABSENT"
        )
        path = self.root / "artifacts/owners.json"
        path.write_text(json.dumps(audit.public_fields(summary)))
        summary["receipt"] = audit.reference(self.root, "artifacts/owners.json")
        with self.assertRaisesRegex(ValueError, "Current installed"):
            audit.check_summary(self.root, summary, True, Queries())

    def test_raw_receipt_and_source_mutations_are_independent_failures(self):
        summary = self.summary()
        for path in (self.root / "artifacts/owners.json", self.root / audit.SOURCES[0]):
            original = path.read_bytes()
            path.write_bytes(original + b" ")
            with self.assertRaisesRegex(ValueError, "binding changed"):
                audit.check_summary(self.root, summary, True, Queries())
            path.write_bytes(original)

    def test_public_check_does_not_need_private_receipts(self):
        summary = self.summary()
        self.worker_receipt.unlink()
        (self.root / "artifacts/owners.json").unlink()
        self.assertEqual(audit.check_summary(self.root, summary)["groups"], 2)

    def test_invalid_notice_namespace_and_metadata_escape_are_rejected(self):
        summary = self.summary()
        for group, field, value in [
            (
                "system:fixture",
                "notices",
                [
                    {
                        "path": "system:/usr/share/licenses/../escape",
                        "bytes": 1,
                        "sha256": "0" * 64,
                    }
                ],
            ),
            (
                "worker:env:fixture",
                "metadata",
                {"path": "repo:../escape", "bytes": 1, "sha256": "0" * 64},
            ),
        ]:
            changed = copy.deepcopy(summary)
            changed["groups"][group][field] = value
            with self.assertRaises(ValueError):
                audit.check_summary(self.root, changed)

    def test_immutable_publication_preserves_existing_receipt(self):
        path = self.root / "artifacts/published.json"
        publish_receipt(path, {"scope": "synthetic"})
        original = path.read_bytes()
        with self.assertRaises(FileExistsError):
            publish_receipt(path, {"scope": "replacement"})
        self.assertEqual(path.read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
