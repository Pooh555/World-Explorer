#!/usr/bin/env python3
"""Run local video requests sequentially, retaining progress and terminal logs."""

import argparse
import fcntl
import json
import os
import signal
import subprocess
import time
import sys
from pathlib import Path


__all__ = [
    "main",
    "owned_job_directories",
    "write_json",
]


def write_json(path, value):
    pending = path.with_suffix(path.suffix + ".tmp")
    pending.write_text(json.dumps(value, indent=2) + "\n")
    pending.replace(path)


def owned_job_directories(request, artifacts):
    """Only mutable worker outputs selected by this reconstruction request."""
    with request.open("rb") as stream:
        payload = stream.read(65537)
    if len(payload) > 65536:
        raise ValueError("request exceeds 64 KiB")
    data = json.loads(payload)
    if not isinstance(data, dict):
        raise ValueError("request must be an object")
    paths = []
    if data.get("pose_action") == "solve":
        paths.append(data["pose_job"])
    for stage, action in [("inspection", "inspect"), ("decode", "decode")]:
        config = data.get(stage, {})
        if not isinstance(config, dict):
            raise ValueError("invalid worker configuration")
        if config.get("action") == action:
            paths.append(config["job_directory"])
    directories = []
    for value in paths:
        if not isinstance(value, str) or not value:
            raise ValueError("invalid worker output path")
        directory = Path(value).resolve()
        if not directory.is_relative_to(artifacts.resolve()):
            raise ValueError("worker output is outside artifacts/")
        directories.append(directory)
    return directories


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("requests", nargs="+", type=Path)
    parser.add_argument("--recover-stale-locks", action="store_true")
    parser.add_argument(
        "--service",
        action="store_true",
        help="Use a transient Linux user service (2 CPU cores, 7 GiB memory cap)",
    )
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    artifacts = root / "artifacts"
    requests = [p.resolve(strict=True) for p in args.requests]
    if len(set(p.parent for p in requests)) != len(requests):
        parser.error("each request must have a separate job directory")
    for request in requests:
        if not request.is_file() or not request.is_relative_to(artifacts.resolve()):
            parser.error("requests must be files inside artifacts/")

    if args.service:
        unit = "world-explorer-video-jobs"
        command = [
            "systemd-run",
            "--user",
            "--collect",
            "--service-type=exec",
            f"--unit={unit}",
            f"--working-directory={root}",
            "--property=CPUQuota=200%",
            "--property=MemoryHigh=6G",
            "--property=MemoryMax=7G",
            "--property=MemorySwapMax=1G",
            "--property=OOMPolicy=stop",
            "--property=KillSignal=SIGINT",
            "--property=TimeoutStopSec=90s",
            f"--property=StandardOutput=append:{artifacts / 'queue.log'}",
            f"--property=StandardError=append:{artifacts / 'queue.log'}",
            sys.executable,
            str(Path(__file__).resolve()),
        ]
        if args.recover_stale_locks:
            command.append("--recover-stale-locks")
        command.extend(str(p) for p in requests)
        subprocess.run(command, check=True)
        write_json(
            artifacts / "queue-service.json",
            {
                "unit": unit,
                "argv": command,
                "started_epoch": time.time(),
            },
        )
        return

    # A kernel-owned lock releases on a crash without requiring PID-file removal.
    with (artifacts / ".queue.lock").open("w") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            parser.error("a video queue is already running")

        child = None
        stopped = False
        blocked = False

        def stop(_signal, _frame):
            nonlocal stopped
            stopped = True
            queue["stop_signal"] = signal.Signals(_signal).name
            queue["stop_epoch"] = time.time()
            if child is not None and child.poll() is None:
                child.send_signal(signal.SIGINT)

        signal.signal(signal.SIGHUP, signal.SIG_IGN)
        signal.signal(signal.SIGINT, stop)
        signal.signal(signal.SIGTERM, stop)
        queue = {
            "pid": os.getpid(),
            "status": "RUNNING",
            "sequential": True,
            "jobs": [
                {"request": str(p.relative_to(root)), "status": "PENDING"}
                for p in requests
            ],
        }
        write_json(artifacts / "queue.json", queue)
        for request, job in zip(requests, queue["jobs"]):
            if stopped:
                break
            logs = request.parent / "logs"
            logs.mkdir(exist_ok=True)
            previous = logs / "result.json"
            if previous.exists():
                history = logs / "history"
                history.mkdir(exist_ok=True)
                attempt = 1
                while (history / f"attempt-{attempt:03d}").exists():
                    attempt += 1
                destination = history / f"attempt-{attempt:03d}"
                destination.mkdir()
                previous.rename(destination / "result.json")
            argv = [
                str(root / "build/dev/we_process"),
                "reconstruct",
                "--request",
                str(request),
            ]
            start = time.monotonic()
            result = {}
            job["status"] = "RUNNING"
            write_json(artifacts / "queue.json", queue)
            write_json(
                logs / "sequential-command.json",
                {
                    "argv": argv,
                    "cwd": str(root),
                    "queue_pid": os.getpid(),
                    "started_epoch": time.time(),
                    "sequential": True,
                },
            )
            with (logs / "reconstruction.log").open("a") as log:
                log.write("\n=== Sequential restart: reusing saved stages ===\n")
                log.flush()
                try:
                    if args.recover_stale_locks:
                        for directory in owned_job_directories(request, artifacts):
                            path = directory / "job.lock"
                            if path.is_symlink():
                                raise ValueError(
                                    "worker lock may not be a symbolic link"
                                )
                            if path.exists():
                                pid = int(path.read_text().strip())
                                if pid <= 0:
                                    raise ValueError("invalid worker lock PID")
                                try:
                                    os.kill(pid, 0)
                                except ProcessLookupError:
                                    path.unlink()
                                    log.write(
                                        f"Removed stale {directory.name} lock: PID {pid} is gone.\n"
                                    )
                                    log.flush()
                                except PermissionError as error:
                                    blocked = True
                                    raise RuntimeError(
                                        f"cannot verify worker PID {pid}"
                                    ) from error
                                else:
                                    blocked = True
                                    raise RuntimeError(
                                        f"worker PID {pid} is still alive"
                                    )
                    if stopped:
                        raise RuntimeError("queue cancelled before launch")
                    child = subprocess.Popen(
                        argv,
                        cwd=root,
                        stdin=subprocess.DEVNULL,
                        stdout=log,
                        stderr=subprocess.STDOUT,
                    )
                    if stopped:
                        child.send_signal(signal.SIGINT)
                    job["process_pid"] = child.pid
                    write_json(artifacts / "queue.json", queue)
                    result["exit_code"] = child.wait()
                    child = None
                    if result["exit_code"] == 0:
                        with (logs / "validate.log").open("w") as validation:
                            checked = subprocess.run(
                                [
                                    str(root / "build/dev/we_process"),
                                    "validate",
                                    str(request.parent / "scene.wexplore"),
                                ],
                                cwd=root,
                                stdin=subprocess.DEVNULL,
                                stdout=validation,
                                stderr=subprocess.STDOUT,
                            )
                        result["validation_exit_code"] = checked.returncode
                except (
                    OSError,
                    ValueError,
                    RuntimeError,
                    KeyError,
                    TypeError,
                ) as error:
                    result.update(exit_code=2, error=str(error))
                finally:
                    if child is not None:
                        if child.poll() is None:
                            child.send_signal(signal.SIGINT)
                        child.wait()
                        child = None
            result.update(
                elapsed_seconds=time.monotonic() - start,
                sequential=True,
                scene_published=(
                    request.parent / "scene.wexplore/manifest.json"
                ).exists(),
            )
            write_json(logs / "result.json", result)
            status = (
                "COMPLETED"
                if result["exit_code"] == 0 and result.get("validation_exit_code") == 0
                else "FAILED"
            )
            if stopped or result["exit_code"] == 130:
                status = "CANCELLED"
            job.update(status=status, **result)
            write_json(artifacts / "queue.json", queue)
            if blocked:
                break
        queue["status"] = (
            "BLOCKED" if blocked else "CANCELLED" if stopped else "FINISHED"
        )
        write_json(artifacts / "queue.json", queue)


if __name__ == "__main__":
    main()
