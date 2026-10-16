"""Synthetic graph/admission tests; no pose or geometry success is inferred."""
from contextlib import closing
import hashlib
import importlib.util
import json
from pathlib import Path
import sqlite3
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "workers"))
from colmap.recovery_analysis import analyze_matches, read_matches, PAIR_BASE, MAX_PAIRS

spec = importlib.util.spec_from_file_location("inspect_pose_recovery",
                                              ROOT / "tools/inspect_pose_recovery.py")
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


class RecoveryAnalysisTests(unittest.TestCase):
    def setUp(self):
        self.selected = [f"{i:06}.png" for i in range(1, 7)]
        self.images = list(enumerate(self.selected, 1))
        self.pairs = [(PAIR_BASE + 2, 100, 2), (2 * PAIR_BASE + 3, 84, 6),
                      (3 * PAIR_BASE + 4, 30, 2), (4 * PAIR_BASE + 5, 29, 6)]
        self.models = [{"model": "0", "images": self.selected[:2]},
                       {"model": "1", "images": self.selected[:4]}]

    def analyze(self, **overrides):
        return analyze_matches(**({"images": self.images, "pairs": self.pairs,
            "selected": self.selected, "models": self.models,
            "configurations": {1: "DEGENERATE", 2: "CALIBRATED", 6: "PANORAMIC"},
            "thresholds": {"registration": 30, "seed": 100}} | overrides))

    def test_subset_selection_preserves_deficit_and_missing_edges(self):
        result = self.analyze()
        self.assertEqual(result["selected_model"], "1")
        self.assertEqual(result["required_registered_frames"], 6)
        candidate = result["candidates"][1]
        self.assertEqual(candidate["registration_deficit"], 2)
        self.assertEqual(candidate["missing"], [
            {"image": "000005.png", "positive_inlier_neighbors": 1,
             "registered_neighbors": [{"image": "000004.png", "inliers": 29,
                                        "configuration": "PANORAMIC"}]},
            {"image": "000006.png", "positive_inlier_neighbors": 0,
             "registered_neighbors": []}])
        self.assertEqual([len(c) for c in result["graphs"]["positive_inliers"]["components"]], [5, 1])
        self.assertEqual([len(c) for c in result["graphs"]["registration"]["components"]], [4, 1, 1])
        self.assertEqual([len(c) for c in result["graphs"]["seed"]["components"]], [2, 1, 1, 1, 1])
        self.assertIn("not 2D-to-3D", result["interpretation"])

    def test_exact_ninety_percent_uses_integer_ceiling(self):
        names = [f"{i:06}.png" for i in range(1, 91)]
        result = self.analyze(selected=names, images=list(enumerate(names, 1)), pairs=[],
                              models=[{"model": "0", "images": names[:79]}])
        self.assertEqual(result["required_registered_frames"], 81)
        self.assertEqual(result["candidates"][0]["registration_deficit"], 2)

    def test_incomparable_and_equal_models_never_sum_coverage(self):
        for groups in [(self.selected[:4], self.selected[3:]),
                       (self.selected[:4], self.selected[:4])]:
            with self.subTest(groups=groups):
                result = self.analyze(models=[{"model": str(i), "images": g}
                                              for i, g in enumerate(groups)])
                self.assertIsNone(result["selected_model"])
                self.assertGreater(result["candidates"][0]["registration_deficit"], 0)

    def test_zero_inlier_pairs_are_counted_without_connecting_images(self):
        result = self.analyze(pairs=[(PAIR_BASE + 2, 0, 1)])
        self.assertEqual(result["stored_pairs"], 1)
        self.assertEqual(result["positive_inlier_pairs"], 0)
        self.assertEqual(len(result["graphs"]["positive_inliers"]["components"]), 6)

    def test_database_order_does_not_change_observations(self):
        self.assertEqual(self.analyze(), self.analyze(images=self.images[::-1],
                         pairs=self.pairs[::-1], selected=self.selected[::-1]))

    def test_invalid_selected_names_and_bounds(self):
        for names in [self.selected[:2], self.selected * 2, ["../a.png"] * 6,
                      [None] * 6, [f"{i:06}.png" for i in range(121)]]:
            with self.subTest(names=names), self.assertRaises(ValueError):
                self.analyze(selected=names)

    def test_invalid_database_image_identities(self):
        for rows in [self.images[:-1], self.images + [self.images[0]],
                     [(True, self.selected[0])] + self.images[1:],
                     [(PAIR_BASE, self.selected[0])] + self.images[1:],
                     [(1, "unknown.png")] + self.images[1:],
                     [(1, self.selected[1])] + self.images[1:]]:
            with self.subTest(rows=rows), self.assertRaises(ValueError):
                self.analyze(images=rows)

    def test_invalid_pair_identity_count_and_configuration(self):
        for pair in [(0, 10, 2), (PAIR_BASE + 1, 10, 2), (2 * PAIR_BASE + 1, 10, 2),
                     (PAIR_BASE + 99, 10, 2), (PAIR_BASE + 2, -1, 2),
                     (PAIR_BASE + 2, True, 2), (PAIR_BASE + 2, 1000001, 2),
                     (PAIR_BASE + 2, 100, 99), (PAIR_BASE + 2, 100, True)]:
            with self.subTest(pair=pair), self.assertRaises(ValueError):
                self.analyze(pairs=[pair])
        with self.assertRaises(ValueError):
            self.analyze(pairs=[self.pairs[0]] * 2)
        with self.assertRaises(ValueError):
            self.analyze(pairs=[self.pairs[0]] * (MAX_PAIRS + 1))

    def test_invalid_models_and_thresholds(self):
        for models in [[], [{"model": "0", "images": []}],
                       [{"model": "0", "images": ["999999.png"]}],
                       [{"model": "0", "images": self.selected * 2}],
                       [self.models[0]] * 2]:
            with self.subTest(models=models), self.assertRaises(ValueError):
                self.analyze(models=models)
        for minimum in [0, -1, True, 1.5, 1000001]:
            with self.subTest(minimum=minimum), self.assertRaises(ValueError):
                self.analyze(thresholds={"seed": minimum})

    def database(self, root):
        path = root / "matches #saved.db"
        with closing(sqlite3.connect(path)) as connection, connection:
            connection.execute("PRAGMA journal_mode=WAL")
            connection.execute("CREATE TABLE images(image_id INTEGER,name TEXT)")
            connection.executemany("INSERT INTO images VALUES(?,?)", self.images)
            connection.execute("CREATE TABLE two_view_geometries(pair_id INTEGER,rows INTEGER,config INTEGER)")
            connection.executemany("INSERT INTO two_view_geometries VALUES(?,?,?)", self.pairs)
        return path

    def test_sqlite_read_only_preserves_bytes_and_closes_handle(self):
        with tempfile.TemporaryDirectory() as directory:
            path = self.database(Path(directory))
            before = path.read_bytes()
            connections = []
            connect = sqlite3.connect
            def track(*args, **kwargs):
                connection = connect(*args, **kwargs)
                connections.append(connection)
                return connection
            with patch("colmap.recovery_analysis.sqlite3.connect", track):
                images, pairs = read_matches(path)
            self.assertEqual((images, pairs), (self.images, self.pairs))
            self.assertEqual(path.read_bytes(), before)
            for connection in connections:
                with self.assertRaises(sqlite3.ProgrammingError):
                    connection.execute("SELECT 1")
            self.assertEqual({p.name for p in Path(directory).iterdir()}, {path.name})

    def test_sqlite_sidecars_and_size_budget_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = self.database(Path(directory))
            for suffix in ("-wal", "-shm", "-journal"):
                sidecar = Path(str(path) + suffix)
                sidecar.write_bytes(b"retained sidecar")
                with self.assertRaisesRegex(ValueError, "sidecar"):
                    read_matches(path)
                sidecar.unlink()
            with patch("colmap.recovery_analysis.MAX_DATABASE_BYTES", path.stat().st_size - 1):
                with self.assertRaisesRegex(ValueError, "bounded regular"):
                    read_matches(path)

    def test_sqlite_frame_budget_is_checked_before_graph_loading(self):
        with tempfile.TemporaryDirectory() as directory:
            path = self.database(Path(directory))
            with closing(sqlite3.connect(path)) as connection, connection:
                connection.executemany("INSERT INTO images VALUES(?,?)",
                                       [(i, f"{i:06}.png") for i in range(7, 122)])
            with self.assertRaisesRegex(ValueError, "budget"):
                read_matches(path)

    def test_report_publication_is_exclusive_and_atomic_on_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "report.json"
            tool.publish(path, self.analyze())
            before = hashlib.sha256(path.read_bytes()).digest()
            with self.assertRaises(FileExistsError):
                tool.publish(path, {"changed": True})
            self.assertEqual(hashlib.sha256(path.read_bytes()).digest(), before)
            self.assertEqual({p.name for p in Path(directory).iterdir()}, {"report.json"})
            with patch.object(tool.os, "link", side_effect=OSError("injected failure")):
                with self.assertRaises(OSError):
                    tool.publish(Path(directory) / "new.json", self.analyze())
            self.assertEqual({p.name for p in Path(directory).iterdir()}, {"report.json"})

    def recovery_fixture(self):
        (ROOT / "artifacts").mkdir(exist_ok=True)
        directory = tempfile.TemporaryDirectory(prefix="recovery-test-", dir=ROOT / "artifacts")
        self.addCleanup(directory.cleanup)
        job = Path(directory.name)
        key = "2" * 64
        decoded = job / ("stages/decode-" + key)
        decoded.mkdir(parents=True)
        frames = {"source_sha256": "0" * 64, "journal_sha256": "1" * 64,
                  "frames": [{"file": "images/" + name} for name in self.selected]}
        (decoded / "result.json").write_text(json.dumps(frames))
        completion = {"status": "complete", "key": key,
                      "files": tool.worker.file_inventory(decoded, 1024**2)}
        (decoded / "completion.json").write_text(json.dumps(completion))
        (job / "decode.json").write_text(json.dumps({"key": key, "output": str(decoded)}))
        request = job / "batch-request.json"
        request.write_text(json.dumps({"schema_version": 1, "job_dir": str(job),
                                       "journal_sha256": "1" * 64}))
        failed = job / "stages/.poses-synthetic"
        model = failed / "sparse/0"
        model.mkdir(parents=True)
        (model / "cameras.bin").write_bytes(b"synthetic backend fixture")
        (failed / "commands.jsonl").write_text(json.dumps({"argv": ["python",
            str(ROOT / "workers/colmap/solve.py"), "--images", str(decoded / "images"),
            "--output", str(failed)]}) + "\n")
        self.database(failed).rename(failed / "database.db")
        backend = SimpleNamespace(__version__="4.2.1",
            Reconstruction=lambda _: SimpleNamespace(images={i: SimpleNamespace(name=name, has_pose=True)
                for i, name in enumerate(self.selected[:4])}),
            IncrementalMapperOptions=lambda: SimpleNamespace(abs_pose_min_num_inliers=30,
                                                              init_min_num_inliers=100),
            TwoViewGeometryConfiguration=SimpleNamespace(__members__={
                "DEGENERATE": 1, "CALIBRATED": 2, "PANORAMIC": 6}))
        return job, request, failed, decoded, backend

    def inventory(self, root):
        return {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in root.rglob("*") if p.is_file()}

    def test_inspect_binds_decode_commands_models_and_preserves_entire_job(self):
        job, request, failed, _, backend = self.recovery_fixture()
        before = self.inventory(job)
        with patch.dict(sys.modules, pycolmap=backend):
            report = tool.inspect(request, failed)
        self.assertEqual(report["selected_model"], "0")
        self.assertEqual(report["candidates"][0]["registration_deficit"], 2)
        self.assertEqual(report["source_sha256"], "0" * 64)
        self.assertEqual(report["input_sha256"][str(failed / "database.db")],
                         tool.sha(failed / "database.db"))
        self.assertEqual(self.inventory(job), before)

    def test_unbound_decode_and_changed_inputs_never_publish(self):
        job, request, failed, decoded, backend = self.recovery_fixture()
        before = self.inventory(job)
        with patch.dict(sys.modules, pycolmap=backend):
            command = failed / "commands.jsonl"
            original = command.read_bytes()
            command.write_text(json.dumps({"argv": ["python", "other-solve"]}))
            with self.assertRaisesRegex(ValueError, "not bound"):
                tool.inspect(request, failed)
            command.write_bytes(original)
            original_loader = backend.Reconstruction
            def changed(path):
                model = original_loader(path)
                (decoded / "result.json").write_text("{}")
                return model
            backend.Reconstruction = changed
            with self.assertRaisesRegex(ValueError, "inputs changed"):
                tool.inspect(request, failed)
        # The only changed input is the mutation deliberately performed by this test.
        after = self.inventory(job)
        changed_paths = [p for p in before if before[p] != after[p]]
        self.assertEqual(changed_paths, [str((decoded / "result.json").relative_to(job))])
        self.assertEqual(set(before), set(after))

    def test_cli_refuses_existing_and_in_job_outputs(self):
        job, request, failed, _, _ = self.recovery_fixture()
        before = self.inventory(job)
        for output in (request, job / "new.json"):
            argv = ["inspect_pose_recovery.py", "--request", str(request),
                    "--failed-stage", str(failed), "--output", str(output)]
            with (patch.object(sys, "argv", argv),
                  patch.object(tool, "inspect", return_value={"failed_stage": str(failed)}) as inspect):
                with self.assertRaises(SystemExit) as error:
                    tool.main()
                self.assertEqual(error.exception.code, 2)
                self.assertEqual(inspect.call_count, 0 if output == request else 1)
        self.assertEqual(self.inventory(job), before)

    def test_invalid_request_and_rehashed_frame_metadata_are_rejected(self):
        job, request, failed, decoded, backend = self.recovery_fixture()
        original = request.read_bytes()
        with patch.dict(sys.modules, pycolmap=backend):
            for value in [True, 0, "1"]:
                record = json.loads(original)
                record["schema_version"] = value
                request.write_text(json.dumps(record))
                with self.assertRaisesRegex(ValueError, "batch request"):
                    tool.inspect(request, failed)
            request.write_bytes(original)
            record = json.loads((decoded / "result.json").read_text())
            record["frames"][0]["file"] = "../000001.png"
            (decoded / "result.json").write_text(json.dumps(record))
            completion = {"status": "complete", "key": "2" * 64,
                          "files": tool.worker.file_inventory(decoded, 1024**2)}
            (decoded / "completion.json").write_text(json.dumps(completion))
            with self.assertRaisesRegex(ValueError, "frame metadata"):
                tool.inspect(request, failed)
        self.assertFalse((job / "report.json").exists())


if __name__ == "__main__":
    unittest.main()
