#!/usr/bin/env python3
"""Run strict Vulkan smoke in an owned rootful Xwayland display (Linux only)."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import select
import shutil
import subprocess
import time


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def run(smoke, manager, output):
    server_path = shutil.which("Xwayland")
    if server_path is None:
        raise RuntimeError("Xwayland is required for this optional native check")
    smoke = smoke.resolve(strict=True)
    manager = manager.resolve(strict=True)
    output = output.absolute()
    output.mkdir()  # Refuse existing evidence, including a symlink.
    server = window_manager = None
    read_fd, write_fd = os.pipe()
    receipt = {
        "smoke_sha256": sha256(smoke),
        "manager_sha256": sha256(manager),
        "scope": "native X11 synthetic overlap minimize/queued-update/restore",
    }
    try:
        with (output / "xwayland.log").open("wb") as server_log:
            server = subprocess.Popen(
                [
                    server_path,
                    "-displayfd",
                    str(write_fd),
                    "-geometry",
                    "1024x768",
                    "-noreset",
                    "-nolisten",
                    "tcp",
                ],
                pass_fds=(write_fd,),
                stdout=server_log,
                stderr=subprocess.STDOUT,
            )
            os.close(write_fd)
            write_fd = None
            if not select.select([read_fd], [], [], 20)[0]:
                raise RuntimeError("isolated Xwayland did not become ready")
            number = os.read(read_fd, 64).decode("ascii").strip()
            if not number.isdecimal():
                raise RuntimeError(
                    "isolated Xwayland returned an invalid display number"
                )
            display = ":" + number
            env = dict(os.environ, DISPLAY=display, SDL_VIDEODRIVER="x11")
            with (output / "wm.log").open("wb") as manager_log:
                window_manager = subprocess.Popen(
                    [str(manager), display],
                    env=env,
                    stdout=manager_log,
                    stderr=subprocess.STDOUT,
                )
                deadline = time.monotonic() + 5
                while not (output / "wm.log").read_bytes().startswith(b"ready\n"):
                    if (
                        window_manager.poll() is not None
                        or time.monotonic() >= deadline
                    ):
                        raise RuntimeError(
                            "isolated window manager did not become ready"
                        )
                    time.sleep(0.05)
                command = [
                    str(smoke),
                    str(output / "smoke.ppm"),
                    "--timings",
                    "--require-minimize",
                ]
                receipt["command"] = command
                with (output / "smoke.log").open("wb") as log:
                    result = subprocess.run(
                        command,
                        env=env,
                        stdout=log,
                        stderr=subprocess.STDOUT,
                        timeout=180,
                    )
                receipt["smoke_exit"] = result.returncode
                if result.returncode != 0:
                    raise RuntimeError(
                        f"strict GPU smoke exited {result.returncode}; see smoke.log"
                    )
                if (
                    b"minimize/restore passed"
                    not in (output / "smoke.log").read_bytes()
                ):
                    raise RuntimeError("smoke did not report accepted minimize/restore")
                baseline = sha256(output / "minimize-baseline.ppm")
                restored = sha256(output / "minimize-restored.ppm")
                if baseline != restored:
                    raise RuntimeError(
                        "restored pixels differ from the minimize baseline"
                    )
                receipt["readback_sha256"] = baseline
                receipt["passed"] = True
    finally:
        os.close(read_fd)
        if write_fd is not None:
            os.close(write_fd)
        for process in (window_manager, server):
            if process and process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
        (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("smoke", type=Path)
    parser.add_argument("manager", type=Path)
    parser.add_argument(
        "output", type=Path, help="new directory under artifacts/validation"
    )
    args = parser.parse_args()
    try:
        result = run(args.smoke, args.manager, args.output)
    except (OSError, RuntimeError, subprocess.SubprocessError, UnicodeError) as error:
        parser.exit(2, f"Native minimize check failed: {error}\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
