#!/usr/bin/env python3
"""Run local video requests sequentially, retaining progress and terminal logs."""

import argparse
import fcntl
import json
import os
import signal
import subprocess
import time
from pathlib import Path


def write_json(path, value):
    pending = path.with_suffix(path.suffix + ".tmp")
    pending.write_text(json.dumps(value, indent=2) + "\n")
    pending.replace(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("requests", nargs="+", type=Path)
    parser.add_argument("--recover-stale-locks", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    artifacts = root / "artifacts"
    requests = [p.resolve(strict=True) for p in args.requests]
    if len(set(p.parent for p in requests)) != len(requests):
        parser.error("each request must have a separate job directory")
    for request in requests:
        if not request.is_file() or not request.is_relative_to(artifacts.resolve()):
            parser.error("requests must be files inside artifacts/")

    # A kernel-owned lock releases on a crash without requiring PID-file removal.
    with (artifacts / ".queue.lock").open("w") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            parser.error("a video queue is already running")

        child = None
        stopped = False

        def stop(_signal, _frame):
            nonlocal stopped
            stopped = True
            if child is not None and child.poll() is None:
                child.send_signal(signal.SIGINT)

        signal.signal(signal.SIGHUP, signal.SIG_IGN)
        signal.signal(signal.SIGINT, stop)
        signal.signal(signal.SIGTERM, stop)
        queue = {"pid": os.getpid(), "status": "RUNNING", "sequential": True,
                 "jobs": [{"request": str(p.relative_to(root)), "status": "PENDING"}
                          for p in requests]}
        write_json(artifacts / "queue.json", queue)
        for request, job in zip(requests, queue["jobs"]):
            if stopped:
                break
            logs = request.parent / "logs"
            logs.mkdir(exist_ok=True)
            argv = [str(root / "build/dev/we_process"), "reconstruct", "--request", str(request)]
            start = time.monotonic()
            result = {}
            job["status"] = "RUNNING"
            write_json(artifacts / "queue.json", queue)
            write_json(logs / "sequential-command.json", {
                "argv": argv, "cwd": str(root), "queue_pid": os.getpid(),
                "started_epoch": time.time(), "sequential": True,
            })
            with (logs / "reconstruction.log").open("a") as log:
                log.write("\n=== Sequential restart: reusing saved stages ===\n")
                log.flush()
                try:
                    if args.recover_stale_locks:
                        for stage in ["inspection", "decode", "poses"]:
                            path = request.parent / stage / "job.lock"
                            if path.exists():
                                pid = int(path.read_text().strip())
                                if pid <= 0:
                                    raise ValueError("invalid worker lock PID")
                                try:
                                    os.kill(pid, 0)
                                except ProcessLookupError:
                                    path.unlink()
                                    log.write(f"Removed stale {stage} lock: PID {pid} is gone.\n")
                                    log.flush()
                                else:
                                    raise RuntimeError(f"worker PID {pid} is still alive")
                    if stopped:
                        raise RuntimeError("queue cancelled before launch")
                    child = subprocess.Popen(argv, cwd=root, stdin=subprocess.DEVNULL,
                                             stdout=log, stderr=subprocess.STDOUT)
                    if stopped:
                        child.send_signal(signal.SIGINT)
                    job["process_pid"] = child.pid
                    write_json(artifacts / "queue.json", queue)
                    result["exit_code"] = child.wait()
                    child = None
                    if result["exit_code"] == 0:
                        with (logs / "validate.log").open("w") as validation:
                            checked = subprocess.run([
                                str(root / "build/dev/we_process"), "validate",
                                str(request.parent / "scene.wexplore"),
                            ], cwd=root, stdin=subprocess.DEVNULL,
                                stdout=validation, stderr=subprocess.STDOUT)
                        result["validation_exit_code"] = checked.returncode
                except (OSError, ValueError, RuntimeError) as error:
                    result.update(exit_code=2, error=str(error))
                finally:
                    if child is not None:
                        if child.poll() is None:
                            child.send_signal(signal.SIGINT)
                        child.wait()
                        child = None
            result.update(elapsed_seconds=time.monotonic() - start, sequential=True,
                          scene_published=(request.parent / "scene.wexplore/manifest.json").exists())
            write_json(logs / "result.json", result)
            job.update(status="COMPLETED" if result["exit_code"] == 0 and
                       result.get("validation_exit_code") == 0 else "FAILED", **result)
            write_json(artifacts / "queue.json", queue)
        queue["status"] = "CANCELLED" if stopped else "FINISHED"
        write_json(artifacts / "queue.json", queue)


if __name__ == "__main__":
    main()
