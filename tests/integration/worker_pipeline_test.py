"""CPU integration tests execute real FFmpeg; no substitute pose/training outputs."""
import importlib.util
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


if __name__ == "__main__":
    unittest.main()
