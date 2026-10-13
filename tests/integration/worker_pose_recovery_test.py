"""Synthetic recovery admission tests; no reconstruction quality claims."""
import hashlib
import importlib.util
from pathlib import Path
import sqlite3
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

path = Path(__file__).resolve().parents[2] / "tools/remap_video_poses.py"
spec = importlib.util.spec_from_file_location("remap_video_poses", path)
remap = importlib.util.module_from_spec(spec)
spec.loader.exec_module(remap)


class PoseRecoveryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.images = self.root / "images"
        self.images.mkdir()
        for name in ("000001.png", "000002.png"):
            (self.images / name).write_bytes(b"synthetic admission fixture")
        self.database = self.root / "database #saved.db"
        with sqlite3.connect(self.database) as database:
            database.execute("CREATE TABLE images(name TEXT,image_id INTEGER)")
            database.executemany("INSERT INTO images VALUES(?,?)",
                                 [("000001.png", 7), ("000002.png", 2)])
            database.execute("CREATE TABLE two_view_geometries(pair_id INTEGER,rows INTEGER)")
            database.execute("INSERT INTO two_view_geometries VALUES(?,?)",
                             (2 * 2147483647 + 7, 100))
        self.backend = SimpleNamespace(__version__="4.2.1",
            IncrementalMapperOptions=lambda: SimpleNamespace(init_min_num_inliers=100))
        self.calls = []
        self.backend.incremental_mapping = self.map

    def map(self, *args, **kwargs):
        self.calls.append((args, kwargs))
        return {0: object()}

    def run_helper(self, *initialization):
        argv = [str(path), "--database", str(self.database), "--images", str(self.images),
                "--output", str(self.root / "sparse"), *initialization]
        with patch.object(sys, "argv", argv), patch.dict(sys.modules, pycolmap=self.backend):
            remap.main()

    def test_verified_seed_preserves_database_and_mapper_thresholds(self):
        before = hashlib.sha256(self.database.read_bytes()).digest()
        self.run_helper("--seed-images", "000001.png", "000002.png")
        self.assertEqual(hashlib.sha256(self.database.read_bytes()).digest(), before)
        self.assertEqual(self.calls[0][1], {"options": {
            "num_threads": 2, "random_seed": 42, "init_image_id1": 7, "init_image_id2": 2},
            "input_path": ""})

    def test_insufficient_verified_matches_never_launch_mapper(self):
        with sqlite3.connect(self.database) as database:
            database.execute("UPDATE two_view_geometries SET rows=99")
        with self.assertRaisesRegex(ValueError, "verified-inlier minimum"):
            self.run_helper("--seed-images", "000001.png", "000002.png")
        self.assertFalse(self.calls)

    def test_unknown_or_repeated_seed_is_rejected(self):
        for names, error in [(('000001.png', '000003.png'), KeyError),
                             (('000001.png', '000001.png'), ValueError)]:
            with self.subTest(names=names), self.assertRaises(error):
                self.run_helper("--seed-images", *names)
        self.assertFalse(self.calls)

    def test_missing_decoded_image_is_rejected_before_mapping(self):
        (self.images / "000002.png").unlink()
        with self.assertRaisesRegex(ValueError, "verified decoded images"):
            self.run_helper("--seed-images", "000001.png", "000002.png")
        self.assertFalse(self.calls)

    def test_retained_model_continuation_does_not_force_a_seed(self):
        model = self.root / "initial-model"
        self.run_helper("--initial-model", str(model))
        self.assertEqual(self.calls[0][1], {"options": {"num_threads": 2, "random_seed": 42},
                                         "input_path": model})

    def test_empty_mapping_is_failure(self):
        self.backend.incremental_mapping = lambda *args, **kwargs: {}
        with self.assertRaisesRegex(ValueError, "initialization geometry"):
            self.run_helper("--seed-images", "000001.png", "000002.png")


if __name__ == "__main__":
    unittest.main()
