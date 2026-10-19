"""Synthetic installed-byte fixtures; no LPIPS evaluation or model downloads."""

import base64
import contextlib
import csv
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import socket
from types import SimpleNamespace
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
# The command's sibling import uses its tools/ directory when run as a script.
with patch.object(sys, "path", [str(ROOT / "tools"), *sys.path]):
    SPEC = importlib.util.spec_from_file_location(
        "metric_audit", ROOT / "tools/audit_metric_environment.py"
    )
    metric = importlib.util.module_from_spec(SPEC)
    SPEC.loader.exec_module(metric)
    PARITY_SPEC = importlib.util.spec_from_file_location(
        "metric_parity", ROOT / "tools/check_metric_parity.py"
    )
    parity = importlib.util.module_from_spec(PARITY_SPEC)
    PARITY_SPEC.loader.exec_module(parity)


class MetricIdentityTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.prefix = self.root / "environment"
        self.site = self.prefix / "site-packages"
        self.site.mkdir(parents=True)
        self.requirements = self.root / "requirements.txt"
        self.requirements.write_text(
            "".join(name + "==1.0\n" for name in metric.DEPENDENCIES)
        )
        self.ssim = self.root / "ssim.py"
        self.ssim.write_text("raise RuntimeError('SSIM source must not execute')\n")
        self.backbone = self.root / "selected-alexnet.pth"
        self.backbone.write_bytes(
            b"synthetic non-pickle backbone; not a qualified model"
        )
        self.infos = {}
        for name in metric.DEPENDENCIES:
            info = self.site / (name.replace("-", "_") + "-1.0.dist-info")
            info.mkdir()
            (info / "METADATA").write_text(
                "Metadata-Version: 2.4\nName: " + name + "\nVersion: 1.0\n\n"
            )
            module = self.site / name.replace("-", "_") / "__init__.py"
            module.parent.mkdir()
            module.write_text("raise RuntimeError('backend must not execute')\n")
            paths = [info / "METADATA", module]
            for relative in metric.IMPLEMENTATION_FILES.get(name, ()):
                file = self.site / relative
                file.parent.mkdir(parents=True, exist_ok=True)
                file.write_bytes(
                    b"synthetic non-executable fixture " + relative.encode()
                )
                paths.append(file)
            self.infos[name] = (info, paths)
            self.inventory(name)

    def inventory(self, name):
        info, paths = self.infos[name]
        site = self.site.resolve(strict=True)
        rows = []
        for file in paths:
            data = file.read_bytes()
            rows.append(
                [
                    file.resolve(strict=True).relative_to(site).as_posix(),
                    "sha256="
                    + base64.urlsafe_b64encode(hashlib.sha256(data).digest())
                    .decode()
                    .rstrip("="),
                    str(len(data)),
                ]
            )
        rows.append(
            [
                (info.resolve(strict=True) / "RECORD").relative_to(site).as_posix(),
                "",
                "",
            ]
        )
        with (info / "RECORD").open("w", newline="") as stream:
            csv.writer(stream).writerows(rows)

    def report(self, backbone=True, sites=None):
        return metric.metric_manifest(
            self.prefix,
            sites or [self.site],
            self.requirements,
            self.ssim,
            self.backbone if backbone else None,
        )

    def test_complete_byte_identity_freezes_policy_but_never_qualifies_metrics(self):
        report = self.report()
        self.assertTrue(report["identity_ready"])
        self.assertEqual(report["status"], "IMPLEMENTED_UNVERIFIED")
        self.assertFalse(report["metric_qualified"])
        self.assertEqual(report["parity"], "NOT_PERFORMED")
        policy = report["identity"]["policy"]
        self.assertEqual(
            policy["lpips"],
            {
                "implementation": "TorchMetrics",
                "net_type": "alex",
                "normalize": True,
                "reduction": "none",
                "resize": None,
            },
        )
        self.assertEqual(policy["ssim"]["window"], 11)
        self.assertEqual(
            report["identity_sha256"],
            hashlib.sha256(metric.canonical_bytes(report["identity"])).hexdigest(),
        )
        self.assertEqual(report, self.report())

    def test_absent_package_and_unselected_backbone_publish_explicit_blockers(self):
        info, paths = self.infos["torchmetrics"]
        for file in [*paths, info / "RECORD"]:
            file.unlink()
        info.rmdir()
        report = self.report(backbone=False)
        self.assertEqual(report["status"], "BLOCKED")
        self.assertFalse(report["identity_ready"])
        self.assertIn("NOT_INSTALLED:torchmetrics", report["identity_blockers"])
        self.assertIn("ALEXNET_BYTES_NOT_SELECTED", report["identity_blockers"])
        self.assertIsNone(report["identity"]["selected_alexnet_bytes"])
        self.assertNotIn(
            "torchmetrics/functional/image/lpips_models/alex.pth",
            report["identity"]["implementation_files"],
        )

    def test_installed_unpinned_and_wrong_version_packages_cannot_be_identity_ready(
        self,
    ):
        original = self.requirements.read_text()
        for replacement, blocker in [
            ("", "NOT_PINNED:torchmetrics"),
            ("torchmetrics==2.0\n", "REQUIREMENTS_MISMATCH"),
        ]:
            with self.subTest(blocker=blocker):
                self.requirements.write_text(
                    original.replace("torchmetrics==1.0\n", replacement)
                )
                report = self.report()
                self.assertFalse(report["identity_ready"])
                self.assertIn(blocker, report["identity_blockers"])

    def test_changed_linear_weights_invalidate_record_and_change_identity(self):
        first = self.report()
        weights = self.site / "torchmetrics/functional/image/lpips_models/alex.pth"
        weights.write_bytes(b"changed linear weights; not a pickle")
        changed = self.report()
        self.assertFalse(changed["identity_ready"])
        self.assertIn("UNVERIFIED_RECORD:torchmetrics", changed["identity_blockers"])
        self.assertNotEqual(first["identity_sha256"], changed["identity_sha256"])

    def test_unlisted_source_cannot_masquerade_as_record_checked_implementation(self):
        info, paths = self.infos["torchmetrics"]
        target = self.site / "torchmetrics/functional/image/lpips.py"
        paths.remove(target)
        self.inventory("torchmetrics")
        report = self.report()
        self.assertFalse(report["identity_ready"])
        self.assertIn(
            "UNCHECKED_IMPLEMENTATION:torchmetrics/functional/image/lpips.py",
            report["identity_blockers"],
        )
        self.assertNotIn(
            "torchmetrics/functional/image/lpips.py",
            report["identity"]["implementation_files"],
        )

    def test_missing_record_is_blocked_and_missing_listed_file_aborts(self):
        info, _ = self.infos["torchmetrics"]
        (info / "RECORD").unlink()
        report = self.report()
        self.assertFalse(report["identity_ready"])
        self.inventory("torchmetrics")
        (self.site / "torchmetrics/functional/image/lpips_models/alex.pth").unlink()
        with self.assertRaises(OSError):
            self.report()

    def test_site_aliases_are_deduplicated_but_distinct_import_candidates_are_rejected(
        self,
    ):
        aliased = self.site / ".." / self.site.name
        self.assertEqual(self.report(sites=[self.site, aliased]), self.report())
        other = self.prefix / "other-site"
        target = other / "torchmetrics/functional/image/lpips.py"
        target.parent.mkdir(parents=True)
        target.write_text("raise RuntimeError('ambiguous shadow module')")
        with self.assertRaisesRegex(ValueError, "Ambiguous"):
            self.report(sites=[self.site, other])

    def test_explicit_weight_bytes_are_not_deserialized_and_change_the_fingerprint(
        self,
    ):
        first = self.report()
        self.assertEqual(
            first["identity"]["selected_alexnet_bytes"]["sha256"],
            hashlib.sha256(self.backbone.read_bytes()).hexdigest(),
        )
        self.backbone.write_bytes(b"different synthetic bytes")
        self.assertNotEqual(first["identity_sha256"], self.report()["identity_sha256"])
        self.backbone.write_bytes(b"")
        with self.assertRaisesRegex(ValueError, "Empty"):
            self.report()
        self.backbone.unlink()
        with self.assertRaises(OSError):
            self.report()

    def test_changed_ssim_source_changes_identity_without_claiming_policy_conformance(
        self,
    ):
        original = self.report()
        self.ssim.write_text("raise RuntimeError('different synthetic SSIM')")
        changed = self.report()
        self.assertNotEqual(original["identity_sha256"], changed["identity_sha256"])
        self.assertFalse(changed["metric_qualified"])
        self.ssim.write_bytes(b"")
        with self.assertRaisesRegex(ValueError, "Empty"):
            self.report()

    def test_requirements_and_selected_implementation_mutation_abort(self):
        self.assert_mutations_abort()

    def test_mutation_observers_follow_aliased_fixture_paths(self):
        # Reproduce equivalent native temp-path spellings without symlink privileges.
        self.site = self.site / ".." / self.site.name
        self.backbone = self.prefix / ".." / self.backbone.name
        self.assert_mutations_abort()

    def assert_mutations_abort(self):
        original_digest = metric.file_digest
        selected = (self.site / "torchmetrics/functional/image/lpips.py").resolve(
            strict=True
        )
        mutations = []

        def corrupt_after_audit(path, *args):
            if path.resolve(strict=True) == selected:
                mutations.append(path)
                path.write_text("changed after installed-byte audit")
            return original_digest(path, *args)

        with patch.object(metric, "file_digest", corrupt_after_audit):
            with self.assertRaisesRegex(ValueError, "changed after"):
                self.report()
        self.assertEqual(len(mutations), 1)
        self.inventory("torchmetrics")
        mutations.clear()
        backbone = self.backbone.resolve(strict=True)

        def corrupt_requirements(path, *args):
            result = original_digest(path, *args)
            if path.resolve(strict=True) == backbone:
                mutations.append(path)
                self.requirements.write_text("torch==wrong\n")
            return result

        with patch.object(metric, "file_digest", corrupt_requirements):
            with self.assertRaisesRegex(ValueError, "Requirements changed"):
                self.report()
        self.assertEqual(len(mutations), 1)

    def write_manifest(self):
        path = self.root / "metric.json"
        path.write_text(json.dumps(self.report()))
        return path

    def verify(self, path):
        return parity.verify_manifest(
            path, self.prefix, [self.site], self.requirements, self.ssim, self.backbone
        )

    def test_parity_preflight_checks_the_manifest_and_current_bytes_without_loading_models(
        self,
    ):
        manifest = self.write_manifest()
        report, digest = self.verify(manifest)
        self.assertTrue(report["identity_ready"])
        self.assertEqual(digest, hashlib.sha256(manifest.read_bytes()).hexdigest())
        self.backbone.write_bytes(b"changed after metric lock")
        with self.assertRaisesRegex(ValueError, "bytes differ"):
            self.verify(manifest)

    def test_parity_preflight_rejects_blocked_malformed_and_tampered_manifests(self):
        path = self.write_manifest()
        original = json.loads(path.read_text())
        for value in [
            [],
            {"schema_version": True},
            {"schema_version": 1, "identity_ready": False},
            {**original, "identity_sha256": "0" * 64},
            {**original, "metric_qualified": True},
        ]:
            with self.subTest(value=value):
                path.write_text(json.dumps(value))
                with self.assertRaises(ValueError):
                    self.verify(path)
        path.write_text('{"schema_version":NaN}')
        with self.assertRaisesRegex(ValueError, "Nonfinite"):
            self.verify(path)
        path.write_text('{"schema_version":1,"schema_version":1}')
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            self.verify(path)

    def test_offline_guard_rejects_both_connection_paths_without_opening_a_native_socket(
        self,
    ):
        original = socket.socket

        class SyntheticSocket:
            family = socket.AF_INET

            def connect(self, address):
                raise AssertionError(
                    "Guard must reject before the underlying connection"
                )

            def connect_ex(self, address):
                raise AssertionError(
                    "Guard must reject before the underlying connection"
                )

        with patch.object(socket, "socket", SyntheticSocket):
            with parity.offline_network():
                client = socket.socket()
                for method in [client.connect, client.connect_ex]:
                    with self.assertRaisesRegex(RuntimeError, "offline"):
                        method(("127.0.0.1", 9))
            self.assertIs(socket.socket, SyntheticSocket)
        self.assertIs(socket.socket, original)

    def test_checkpoint_guard_forces_tensor_only_loading_and_rejects_changed_or_unselected_bytes(
        self,
    ):
        calls = []

        def loader(path, *args, **kwargs):
            calls.append((path, kwargs))
            return "synthetic result"

        fake = SimpleNamespace(load=loader)
        selected = self.backbone.resolve()
        allowed = {selected: hashlib.sha256(selected.read_bytes()).hexdigest()}
        with parity.checked_tensor_loading(fake, allowed):
            self.assertEqual(
                fake.load(selected, weights_only=False), "synthetic result"
            )
            self.assertTrue(calls[-1][1]["weights_only"])
            with self.assertRaisesRegex(ValueError, "Unselected"):
                fake.load(self.ssim)
            selected.write_bytes(b"changed checkpoint")
            with self.assertRaisesRegex(ValueError, "changed"):
                fake.load(selected)
        self.assertIs(fake.load, loader)
        self.assertEqual(len(calls), 1)

    def test_tensor_loader_uses_the_hashed_snapshot_when_the_selected_path_changes(
        self,
    ):
        selected = self.backbone.resolve()
        original = selected.read_bytes()

        def loader(snapshot, *args, **kwargs):
            selected.write_bytes(b"replacement after hashing")
            self.assertTrue(kwargs["weights_only"])
            return snapshot.read()

        fake = SimpleNamespace(load=loader)
        with parity.checked_tensor_loading(
            fake, {selected: hashlib.sha256(original).hexdigest()}
        ):
            self.assertEqual(fake.load(selected), original)

    def cli(self, output, require=False):
        scripts = self.root / "tools"
        scripts.mkdir(exist_ok=True)
        (scripts / "audit_metric_environment.py").write_text(
            "synthetic auditor identity"
        )
        (scripts / "audit_worker_environment.py").write_text(
            "synthetic shared auditor identity"
        )
        argv = [
            "audit_metric_environment.py",
            "--requirements",
            str(self.requirements),
            "--ssim-implementation",
            str(self.ssim),
            "--output",
            str(output),
        ]
        if require:
            argv.append("--require-identities")
        with (
            patch.object(
                metric, "__file__", str(scripts / "audit_metric_environment.py")
            ),
            patch.object(sys, "argv", argv),
            patch.object(sys, "prefix", str(self.prefix)),
            patch.object(metric.sysconfig, "get_path", return_value=str(self.site)),
            contextlib.redirect_stdout(io.StringIO()),
            contextlib.redirect_stderr(io.StringIO()),
        ):
            return metric.main()

    def test_cli_blocked_receipt_exit_code_and_atomic_refusal_preserve_existing_data(
        self,
    ):
        output = self.root / "artifacts" / "blocked.json"
        self.assertEqual(self.cli(output, require=True), 3)
        original = output.read_bytes()
        self.assertFalse(json.loads(original)["identity_ready"])
        self.assertEqual(self.cli(output), 2)
        self.assertEqual(output.read_bytes(), original)
        self.assertEqual(list(output.parent.glob(".worker-audit-*")), [])
        self.assertEqual(self.cli(self.root / "outside.json"), 2)
        self.assertFalse((self.root / "outside.json").exists())

    def test_cli_malformed_record_fails_without_publishing(self):
        info, _ = self.infos["torchmetrics"]
        (info / "RECORD").write_text('"unterminated')
        output = self.root / "artifacts" / "invalid.json"
        self.assertEqual(self.cli(output), 2)
        self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
