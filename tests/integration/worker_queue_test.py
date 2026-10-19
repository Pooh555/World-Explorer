"""Synthetic queue/lock regressions; no reconstruction success is simulated."""

import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
if sys.platform == "linux":
    spec = importlib.util.spec_from_file_location(
        "video_queue", ROOT / "tools/run_video_jobs.py"
    )
    queue = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(queue)


@unittest.skipUnless(sys.platform == "linux", "local queue uses Linux/POSIX facilities")
class QueueBoundaryTests(unittest.TestCase):
    def test_lock_recovery_uses_requested_outputs_and_ignores_retained_inputs(self):
        with tempfile.TemporaryDirectory() as temporary:
            artifacts = Path(temporary) / "artifacts"
            artifacts.mkdir()
            request = artifacts / "request.json"
            custom = artifacts / "video/pose-recovery"
            request.write_text(
                json.dumps(
                    {
                        "pose_action": "solve",
                        "pose_job": str(custom),
                        "inspection": {
                            "action": "adopt_completed",
                            "stage": "/retained/input",
                        },
                        "decode": {
                            "action": "adopt_completed",
                            "stage": "/retained/input",
                        },
                    }
                )
            )
            self.assertEqual(queue.owned_job_directories(request, artifacts), [custom])
            data = json.loads(request.read_text())
            data["pose_job"] = str(Path(temporary) / "outside")
            request.write_text(json.dumps(data))
            with self.assertRaises(ValueError):
                queue.owned_job_directories(request, artifacts)

    def run_fixture(self, alive):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "tools").mkdir()
            (root / "build/dev").mkdir(parents=True)
            shutil.copy(
                ROOT / "tools/run_video_jobs.py", root / "tools/run_video_jobs.py"
            )
            requests = []
            for name in ["one", "two"]:
                job = root / "artifacts" / name
                poses = job / "custom-poses"
                poses.mkdir(parents=True)
                request = job / "request.json"
                request.write_text(
                    json.dumps({"pose_action": "solve", "pose_job": str(poses)})
                )
                requests.append(str(request))
            first = root / "artifacts/one"
            (first / "custom-poses/job.lock").write_text(
                str(os.getpid() if alive else 2147483647)
            )
            (first / "logs").mkdir()
            (first / "logs/result.json").write_text('{"previous": true}')
            executable = root / "build/dev/we_process"
            executable.write_text("""#!/usr/bin/env python3
import pathlib,sys,time
root=pathlib.Path.cwd()
if sys.argv[1]=='validate':raise SystemExit(2)
marker=root/'active';marker.mkdir()
name=pathlib.Path(sys.argv[-1]).parent.name
with (root/'order').open('a') as log:log.write(name+' start\\n')
time.sleep(0.05)
with (root/'order').open('a') as log:log.write(name+' end\\n')
marker.rmdir()
raise SystemExit(2)
""")
            executable.chmod(0o700)
            result = subprocess.run(
                [
                    sys.executable,
                    str(root / "tools/run_video_jobs.py"),
                    "--recover-stale-locks",
                    *requests,
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            status = json.loads((root / "artifacts/queue.json").read_text())
            if alive:
                self.assertEqual(status["status"], "BLOCKED")
                self.assertEqual(status["jobs"][1]["status"], "PENDING")
                self.assertFalse((root / "order").exists())
                self.assertTrue((first / "custom-poses/job.lock").exists())
            else:
                self.assertEqual(
                    (root / "order").read_text(),
                    "one start\none end\ntwo start\ntwo end\n",
                )
                self.assertEqual(status["status"], "FINISHED")
                self.assertFalse((first / "custom-poses/job.lock").exists())
            archived = first / "logs/history/attempt-001/result.json"
            self.assertEqual(json.loads(archived.read_text()), {"previous": True})

    def test_serial_failure_continuation_custom_stale_lock_and_result_history(self):
        self.run_fixture(False)

    def test_live_worker_blocks_queue_without_removal_or_second_launch(self):
        self.run_fixture(True)


if __name__ == "__main__":
    unittest.main()
