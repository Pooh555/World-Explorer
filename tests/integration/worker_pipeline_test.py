"""CPU integration tests execute real FFmpeg; no substitute pose/training outputs."""
import importlib.util
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("we_worker", ROOT / "workers/worker.py")
worker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(worker)
pose_spec = importlib.util.spec_from_file_location("pose_export", ROOT / "workers/colmap/export_poses.py")
pose = importlib.util.module_from_spec(pose_spec)
pose_spec.loader.exec_module(pose)
journal_spec = importlib.util.spec_from_file_location("pose_journal", ROOT / "workers/colmap/journal.py")
journal = importlib.util.module_from_spec(journal_spec)
journal_spec.loader.exec_module(journal)


class WorkerBoundaryTests(unittest.TestCase):
    def tearDown(self):
        worker.CANCELLED.clear()

    def test_output_overflow_terminates_child(self):
        with self.assertRaises(worker.WorkerError) as caught:
            worker.run_command([sys.executable, "-c", "print('x'*100000)"], max_capture=128)
        self.assertEqual(caught.exception.code, "RESOURCE_LIMIT")

    def test_cancellation_reaps_running_process(self):
        timer = threading.Timer(0.15, worker.CANCELLED.set)
        timer.start()
        start = time.monotonic()
        try:
            with self.assertRaises(worker.WorkerError) as caught:
                worker.run_command([sys.executable, "-c", "import time; time.sleep(30)"], timeout=60)
            self.assertEqual(caught.exception.code, "CANCELLED")
            self.assertLess(time.monotonic() - start, 3)
        finally:
            timer.join()

    def test_pose_adapter_half_pixel_and_optical_rotation(self):
        self.assertEqual(pose.canonical_intrinsics([[700, 0, 300.5], [0, 510, 200.5], [0, 0, 1]]),
                         [[700, 0, 300], [0, 510, 200], [0, 0, 1]])
        transformed = pose.canonical_pose([2, 3, 4], [0, 0, 0, 1])
        self.assertEqual(transformed, {"translation": [2, -3, -4], "rotation_wxyz": [0, 1, 0, 0]})
        with self.assertRaises(ValueError):
            pose.canonical_pose([float("nan"), 0, 0], [0, 0, 0, 1])

    def test_stage_inventory_rejects_symlink_escape(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "outside").write_text("private")
            (root / "stage").mkdir()
            try:
                (root / "stage/escape").symlink_to(root / "outside")
            except OSError:
                self.skipTest("Creating symlinks is unavailable on this host")
            with self.assertRaises(worker.WorkerError):
                worker.file_inventory(root / "stage", 10000)


@unittest.skipUnless(shutil.which("ffmpeg") and shutil.which("ffprobe"), "FFmpeg is required")
class DecodeIntegrationTests(unittest.TestCase):
    def test_real_decode_resume_corruption_and_failure_preservation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source with spaces.mp4"
            subprocess.run(["ffmpeg", "-v", "error", "-f", "lavfi", "-i",
                            "testsrc2=size=96x64:rate=10:duration=2", "-c:v", "mpeg4",
                            str(source)], check=True)
            request = {"schema_version": 1, "source": str(source),
                       "job_dir": str(root / "job"), "stop_after": "decode", "sample_fps": 2}
            job = worker.Job(request)
            result = job.run()
            output = Path(result["output"])
            decoded = json.loads((output / "result.json").read_text())
            self.assertEqual(decoded["source_frame_count"], 20)
            self.assertEqual([frame["source_index"] for frame in decoded["frames"]], [0, 5, 10, 15])
            self.assertEqual(len(list((output / "images").glob("*.png"))), 4)
            self.assertEqual(len({frame["frame_id"] for frame in decoded["frames"]}), 4)
            committed = (output / "completion.json").read_bytes()
            self.assertEqual(worker.Job(request).run()["output"], result["output"])
            self.assertEqual((output / "completion.json").read_bytes(), committed)
            previous_result = (root / "job/result.json").read_bytes()

            def fail_stage(directory):
                (directory / "partial.txt").write_text("diagnostic partial output")
                raise worker.WorkerError("BACKEND_FAILED", "injected crash")

            with self.assertRaises(worker.WorkerError):
                job.stage_run("injected_failure", {"input": "real decode"}, fail_stage)
            self.assertEqual((root / "job/result.json").read_bytes(), previous_result)

            self.assertFalse((root / "job/injected_failure.json").exists())
            (output / "images/000001.png").write_bytes(b"corrupt")
            with self.assertRaises(worker.WorkerError) as caught:
                worker.Job(request).run()
            self.assertEqual(caught.exception.code, "CORRUPT_ASSET")
            self.assertEqual((root / "job/result.json").read_bytes(), previous_result)


@unittest.skipUnless(shutil.which("ffmpeg"), "FFmpeg is required")
class JournalBoundaryTests(unittest.TestCase):
    def fixture(self, root):
        (root / "input").mkdir()
        frames = []
        for index in range(3):
            pixels = bytes([17 + index, 34, 51, 68, 85, 102])
            data = b"P6\n2 1\n255\n" + pixels
            name = f"{index + 1}.ppm"
            (root / "input" / name).write_bytes(data)
            frames.append({"source_index": index * 10, "pts": index * 10,
                           "time_base": "1/30", "capture_time_ns": index * 10**9 // 3,
                           "frame_id": "b" * 16 + f"-{index * 10}", "calibration_id": "b" * 16 + "-lens-0",
                           "width": 2, "height": 1, "bytes": len(data), "file": name,
                           "pixel_sha256": hashlib.sha256(pixels).hexdigest(),
                           "sha256": hashlib.sha256(data).hexdigest()})
        value = {"schema_version": 1, "profile": "native_rgb_journal_v1", "source_sha256": "b" * 64,
                 "decode_completion_sha256": "a" * 64, "source_frame_count": 21, "frames": frames}
        path = root / "input/journal.json"
        path.write_text(json.dumps(value))
        return {"schema_version": 1, "journal": str(path), "journal_sha256": worker.sha256(path),
                "job_dir": str(root), "pose_python": str(root / "missing-solver")}, value

    def test_admitted_rgb_conversion_and_solver_failure_preserve_decode(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            request, value = self.fixture(root)
            with self.assertRaises(OSError) as caught:
                journal.run(worker, request)
            self.assertEqual(caught.exception.stage, "poses")
            pointer = worker.read_json(root / "decode.json")
            decoded = Path(pointer["output"])
            self.assertTrue(worker.verify_stage(decoded, pointer["key"], worker.LIMITS["max_output_bytes"]))
            metadata = worker.read_json(decoded / "result.json")
            self.assertEqual(metadata["selection"], "native_rgb_journal_v1")
            self.assertEqual(metadata["journal_sha256"], request["journal_sha256"])
            for index, frame in enumerate(metadata["frames"]):
                rgb = subprocess.check_output(["ffmpeg", "-v", "error", "-i", str(decoded / frame["file"]),
                                               "-frames:v", "1", "-f", "rawvideo", "-pix_fmt", "rgb24", "pipe:1"])
                self.assertEqual(hashlib.sha256(rgb).hexdigest(), value["frames"][index]["pixel_sha256"])
            self.assertFalse((root / "poses.json").exists())
            self.assertFalse((root / "result.json").exists())
            self.assertFalse((root / "job.lock").exists())

    def test_corrupt_journal_metadata_assets_and_extra_files_fail_before_solver(self):
        for mutation in ("journal", "asset", "timestamp", "identity", "extra", "unknown"):
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary).resolve()
                request, value = self.fixture(root)
                if mutation == "journal":
                    (root / "input/journal.json").write_text("{}")
                elif mutation == "asset":
                    (root / "input/1.ppm").write_bytes(b"corrupt")
                elif mutation == "extra":
                    (root / "input/extra").write_text("not admitted")
                else:
                    if mutation == "timestamp":
                        value["frames"][1]["capture_time_ns"] += 1
                    elif mutation == "unknown":
                        value["frames"][1]["unknown"] = "invalid schema"
                    else:
                        value["frames"][1]["frame_id"] = "wrong source"
                    (root / "input/journal.json").write_text(json.dumps(value))
                    request["journal_sha256"] = worker.sha256(root / "input/journal.json")
                with self.assertRaises(worker.WorkerError) as caught:
                    journal.run(worker, request)
                self.assertEqual(caught.exception.stage, "journal")
                self.assertFalse((root / "decode.json").exists())
                self.assertFalse((root / "result.json").exists())


if __name__ == "__main__":
    unittest.main()
