#!/usr/bin/env python3
"""Local, transactional offline worker. See docs/protocols/worker.md."""

from __future__ import annotations

import argparse
import contextlib
import hashlib
import importlib.metadata
import importlib.util
import json
import math
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time
from fractions import Fraction
from types import SimpleNamespace

__all__ = [
    "CANCELLED",
    "Job",
    "LIMITS",
    "MAX_LOG_BYTES",
    "MAX_MESSAGE_BYTES",
    "PROTOCOL_VERSION",
    "ROOT",
    "WIRE",
    "WorkerError",
    "atomic_json",
    "canonical_hash",
    "capabilities",
    "emit",
    "executable",
    "file_inventory",
    "main",
    "read_json",
    "run_command",
    "sha256",
    "terminate",
    "tool_identity",
    "validate_request",
    "verify_stage",
]

PROTOCOL_VERSION = 1
MAX_MESSAGE_BYTES = 1024 * 1024
MAX_LOG_BYTES = 8 * 1024 * 1024
ROOT = Path(__file__).resolve().parent
CANCELLED = threading.Event()
WIRE = None
LIMITS = {
    "max_source_bytes": 16 * 1024**3,
    "max_output_bytes": 32 * 1024**3,
    "max_frames": 120,
    "max_source_frames": 36000,
    "max_image_pixels": 4096 * 4096,
    "max_duration_seconds": 1200,
    "stage_timeout_seconds": 3600,
    "cpu_threads": 4,
    "max_splats": 2000000,
}


class WorkerError(Exception):
    def __init__(self, code: str, message: str, stage: str | None = None):
        super().__init__(message)
        self.code = code
        self.stage = stage


def emit(event: str, **values: object) -> None:
    message = {"schema_version": PROTOCOL_VERSION, "event": event, **values}
    encoded = json.dumps(message, allow_nan=False, separators=(",", ":"))
    if len(encoded.encode()) > MAX_MESSAGE_BYTES:
        raise WorkerError("RESOURCE_LIMIT", "Worker event exceeds message limit")
    if WIRE and WIRE.WRITER:
        if event == "job_completed":
            result = values["result"]
            WIRE.completed(result["stage"], Path(result["output"]) / "result.json")
        elif event == "job_failed":
            WIRE.failure(
                values["code"], values["stage"], values["message"], values["recovery"]
            )
        else:
            WIRE.progress(
                values.get("stage", event), encoded, values.get("elapsed_seconds", 0)
            )
        return
    print(encoded, flush=True)


def read_json(path: Path, limit: int = MAX_MESSAGE_BYTES) -> dict:
    if path.stat().st_size > limit:
        raise WorkerError("BAD_INPUT", f"JSON exceeds {limit} byte limit")
    with path.open(encoding="utf-8") as source:
        value = json.load(
            source,
            parse_constant=lambda value: (_ for _ in ()).throw(
                WorkerError("BAD_INPUT", "Non-finite JSON value")
            ),
        )
    if not isinstance(value, dict):
        raise WorkerError("BAD_INPUT", "Expected a JSON object")
    return value


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while block := source.read(1024 * 1024):
            if CANCELLED.is_set():
                raise WorkerError(
                    "CANCELLED", "Job cancelled; committed stages retained"
                )
            digest.update(block)
    return digest.hexdigest()


def canonical_hash(value: object) -> str:
    return hashlib.sha256(
        json.dumps(
            value, sort_keys=True, allow_nan=False, separators=(",", ":")
        ).encode()
    ).hexdigest()


def atomic_json(path: Path, value: object) -> None:
    temporary = path.with_name(path.name + ".tmp")
    with temporary.open("w", encoding="utf-8") as output:
        json.dump(value, output, sort_keys=True, indent=2, allow_nan=False)
        output.write("\n")
        output.flush()
        os.fsync(output.fileno())
    os.replace(temporary, path)
    if os.name == "posix":
        descriptor = os.open(path.parent, os.O_RDONLY)
        try:
            os.fsync(descriptor)
        finally:
            os.close(descriptor)


def executable(name: str) -> str:
    found = shutil.which(name)
    if not found:
        raise WorkerError(
            "UNSUPPORTED", f"Install/configure local {name}; no remote fallback"
        )
    return str(Path(found).resolve())


def terminate(process: subprocess.Popen, isolated_group: bool = True) -> None:
    if process.poll() is not None:
        return
    if os.name == "posix" and isolated_group:
        with contextlib.suppress(ProcessLookupError):
            os.killpg(process.pid, signal.SIGTERM)
    else:
        process.terminate()
    try:
        process.wait(timeout=1)
    except subprocess.TimeoutExpired:
        if os.name == "posix" and isolated_group:
            with contextlib.suppress(ProcessLookupError):
                os.killpg(process.pid, signal.SIGKILL)
        else:
            process.kill()
        process.wait()


def run_command(
    arguments: list[str],
    *,
    timeout: int = 60,
    log: Path | None = None,
    max_capture: int = MAX_MESSAGE_BYTES,
    threads: int = 4,
    output_directory: Path | None = None,
    output_budget: int = 32 * 1024**3,
) -> bytes:
    """No shell; continuously drain both pipes with bounded memory and disk logs."""
    if CANCELLED.is_set():
        raise WorkerError("CANCELLED", "Job cancelled")
    environment = os.environ.copy()
    environment.update(
        {
            "OMP_NUM_THREADS": str(threads),
            "OPENBLAS_NUM_THREADS": str(threads),
            "MKL_NUM_THREADS": str(threads),
            "QT_QPA_PLATFORM": "offscreen",
            "HF_HUB_OFFLINE": "1",
            "TRANSFORMERS_OFFLINE": "1",
        }
    )
    start = time.monotonic()
    isolated_group = os.name == "posix" and not (WIRE and WIRE.WRITER)
    process = subprocess.Popen(
        arguments,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=environment,
        start_new_session=isolated_group,
    )
    output = bytearray()
    overflow = threading.Event()
    log_stream = log.open("ab") if log else None
    log_lock = threading.Lock()

    def drain(stream, capture: bool) -> None:
        try:
            while block := stream.read(8192):
                if capture:
                    available = max_capture - len(output)
                    output.extend(block[: max(0, available)])
                    if len(block) > available:
                        overflow.set()
                if log_stream:
                    with log_lock:
                        remaining = MAX_LOG_BYTES - log_stream.tell()
                        if remaining > 0:
                            log_stream.write(block[:remaining])
        finally:
            stream.close()

    readers = [
        threading.Thread(target=drain, args=(process.stdout, True), daemon=True),
        threading.Thread(target=drain, args=(process.stderr, False), daemon=True),
    ]
    for reader in readers:
        reader.start()
    failure = None
    last_disk_check = 0.0
    try:
        while process.poll() is None:
            elapsed = time.monotonic() - start
            if CANCELLED.is_set():
                failure = WorkerError(
                    "CANCELLED", "Job cancelled; committed stages retained"
                )
            elif elapsed > timeout:
                failure = WorkerError(
                    "TIMEOUT", "Worker stage exceeded configured deadline"
                )
            elif overflow.is_set():
                failure = WorkerError(
                    "RESOURCE_LIMIT", "Backend stdout exceeds capture limit"
                )
            elif output_directory and elapsed - last_disk_check > 1:
                last_disk_check = elapsed
                size = sum(
                    p.stat().st_size for p in output_directory.rglob("*") if p.is_file()
                )
                if size > output_budget:
                    failure = WorkerError(
                        "RESOURCE_LIMIT", "Stage exceeded output byte budget"
                    )
            if failure:
                break
            time.sleep(0.05)
    finally:
        if process.poll() is None:
            terminate(process, isolated_group)
        for reader in readers:
            reader.join(timeout=5)
        if log_stream:
            log_stream.close()
    if failure:
        raise failure
    if overflow.is_set():
        raise WorkerError("RESOURCE_LIMIT", "Backend stdout exceeds capture limit")
    if process.returncode:
        raise WorkerError(
            "BACKEND_FAILED",
            f"{Path(arguments[0]).name} exited {process.returncode}; inspect stage log",
        )
    return bytes(output)


def tool_identity(name: str) -> dict:
    try:
        path = executable(name)
        arguments = [path, "-h"] if name == "colmap" else [path, "-version"]
        version = run_command(arguments).decode(errors="replace").splitlines()[0]
        return {
            "available": True,
            "path": path,
            "version": version,
            "sha256": sha256(Path(path)),
        }
    except (WorkerError, OSError, IndexError) as error:
        return {"available": False, "reason": str(error)}


def capabilities() -> dict:
    packages = {}
    for name in ("torch", "gsplat", "pycolmap"):
        try:
            packages[name] = importlib.metadata.version(name)
        except importlib.metadata.PackageNotFoundError:
            packages[name] = None
    tools = {name: tool_identity(name) for name in ("ffmpeg", "ffprobe", "colmap")}
    return {
        "api_version": 1,
        "backend": "world_explorer_local",
        "backend_version": "0.1.0",
        "wire_format": "flatbuffers-commands/events-v2; jsonl diagnostic CLI available",
        "sensor_types": ["video", "image_sequence"],
        "batch": True,
        "streaming_tracker": False,
        "remote": False,
        "checkpoint_semantics": "completed_stages; appearance restarts (upstream checkpoints are eval-only)",
        "checkpoint_rights": "local user data; backend package licenses apply",
        "device_options": ["cpu_poses", "cuda_appearance_requires_probe"],
        "cuda_appearance_verified": False,
        "limits": LIMITS,
        "tools": tools,
        "packages": packages,
        "python": sys.version.split()[0],
    }


def _frozen_view_helper():
    spec = importlib.util.spec_from_file_location(
        "we_frozen_views", ROOT / "frozen_views.py"
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def validate_request(request: dict) -> dict:
    allowed = {
        "schema_version",
        "source",
        "job_dir",
        "preset",
        "stop_after",
        "limits",
        "sample_fps",
        "gsplat_source",
        "alexnet_weights_sha256",
        "pose_python",
        "appearance_python",
        "pose_recipe",
        "frame_selection",
        "frozen_views",
    }
    if set(request) - allowed or request.get("schema_version") != 1:
        raise WorkerError("BAD_INPUT", "Unknown request fields or schema version")
    for name in ("source", "job_dir"):
        if (
            not isinstance(request.get(name), str)
            or not request[name]
            or "\0" in request[name]
        ):
            raise WorkerError("BAD_INPUT", f"{name} must be a local filesystem path")
    source = Path(request["source"]).expanduser().resolve(strict=True)
    if not source.is_file():
        raise WorkerError("BAD_INPUT", "Source must be a regular local file")
    job_dir = Path(request["job_dir"]).expanduser().resolve()
    if source == job_dir or job_dir in source.parents:
        raise WorkerError(
            "BAD_INPUT", "Source cannot be inside the job output directory"
        )
    preset = request.get("preset", "standard")
    if request.get("pose_recipe", "sequential") not in (
        "sequential",
        "exhaustive_guided",
    ):
        raise WorkerError("BAD_INPUT", "Unknown pose recovery recipe")
    stop_after = request.get("stop_after", "appearance")
    if preset not in ("standard", "quality") or stop_after not in (
        "inspect",
        "decode",
        "poses",
        "appearance",
    ):
        raise WorkerError("BAD_INPUT", "Unsupported preset or stage")
    limits = LIMITS | request.get("limits", {})
    if set(limits) != set(LIMITS):
        raise WorkerError("BAD_INPUT", "Unknown resource limit")
    for key, value in limits.items():
        if type(value) is not int or value <= 0 or value > LIMITS[key]:
            raise WorkerError(
                "BAD_INPUT",
                f"{key} must be a positive integer no greater than {LIMITS[key]}",
            )
    sample_fps = request.get("sample_fps", 3)
    if (
        type(sample_fps) not in (int, float)
        or not math.isfinite(sample_fps)
        or not 0.1 <= sample_fps <= 4
    ):
        raise WorkerError("BAD_INPUT", "sample_fps must be in [0.1, 4]")
    selection = request.get("frame_selection", "timestamp_sampling_v1")
    if selection not in (
        "timestamp_sampling_v1",
        "quality_keyframes_candidate_v1",
        "quality_keyframes_candidate_v2",
        "quality_keyframes_candidate_v3",
    ):
        raise WorkerError("BAD_INPUT", "Unknown frame selection profile")
    if selection != "timestamp_sampling_v1" and sample_fps < 2:
        raise WorkerError("BAD_INPUT", "Quality candidates require 2..4 sample_fps")
    if "frozen_views" in request:
        if stop_after != "decode" or selection != "timestamp_sampling_v1":
            raise WorkerError(
                "BAD_INPUT", "Frozen views require decode-only preparation"
            )
        try:
            request = request | {
                "frozen_views": _frozen_view_helper().validate_configuration(
                    request["frozen_views"], source, job_dir
                )
            }
        except (ValueError, OSError, KeyError, TypeError) as error:
            raise WorkerError("BAD_INPUT", str(error)) from error
    if source.stat().st_size > limits["max_source_bytes"]:
        raise WorkerError("RESOURCE_LIMIT", "Source exceeds configured byte limit")
    return request | {
        "source": str(source),
        "job_dir": str(job_dir),
        "preset": preset,
        "stop_after": stop_after,
        "limits": limits,
        "sample_fps": sample_fps,
    }


def file_inventory(directory: Path, budget: int) -> list[dict]:
    files = []
    total = 0
    for path in sorted(directory.rglob("*")):
        if path.is_symlink():
            raise WorkerError(
                "CORRUPT_ASSET", "Stage outputs may not contain symbolic links"
            )
        if not path.is_file() or path.name == "completion.json":
            continue
        total += path.stat().st_size
        if total > budget:
            raise WorkerError("RESOURCE_LIMIT", "Stage exceeds output byte budget")
        files.append(
            {
                "path": path.relative_to(directory).as_posix(),
                "bytes": path.stat().st_size,
                "sha256": sha256(path),
            }
        )
    return files


def verify_stage(directory: Path, key: str, budget: int) -> bool:
    try:
        manifest = read_json(directory / "completion.json", 16 * MAX_MESSAGE_BYTES)
        return (
            manifest.get("key") == key
            and manifest.get("status") == "complete"
            and manifest.get("files") == file_inventory(directory, budget)
        )
    except (OSError, ValueError, WorkerError):
        return False


class Job:
    def __init__(self, request: dict, *, journal_identity: dict | None = None):
        # The journal boundary validates its image inventory before constructing
        # this stage coordinator; ordinary video requests use the video validator.
        self.request = (
            validate_request(request) if journal_identity is None else request
        )
        self.directory = Path(self.request["job_dir"])
        self.source = Path(
            self.request["source"]
            if journal_identity is None
            else self.request["journal"]
        )
        self.limits = self.request["limits"]
        self.directory.mkdir(parents=True, exist_ok=True)
        self.source_hash = (
            sha256(self.source)
            if journal_identity is None
            else journal_identity["sha256"]
        )
        self.backend = capabilities()
        self.stage = "initialization"
        self.source_identity = (
            {"sha256": self.source_hash, "bytes": self.source.stat().st_size}
            if journal_identity is None
            else journal_identity
        )
        self.recipe_identity = canonical_hash(
            {
                "request": {
                    k: v
                    for k, v in self.request.items()
                    if k not in ("job_dir", "stop_after", "source", "journal")
                },
                "source": self.source_identity,
                "backend": self.backend,
                "worker_hash": sha256(Path(__file__)),
                "adapter_hashes": {
                    str(path.relative_to(ROOT)): sha256(path)
                    for path in sorted(ROOT.rglob("*.py"))
                },
            }
        )

    @contextlib.contextmanager
    def lock(self):
        path = self.directory / "job.lock"
        try:
            descriptor = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
        except FileExistsError as error:
            raise WorkerError(
                "WOULD_BLOCK",
                "Job is already locked; after a crash verify its PID is gone, then remove job.lock",
            ) from error
        try:
            os.write(descriptor, str(os.getpid()).encode())
            os.close(descriptor)
            yield
        finally:
            path.unlink(missing_ok=True)

    def stage_run(self, name: str, previous: dict, operation) -> tuple[Path, dict]:
        self.stage = name
        key = canonical_hash(
            {"recipe": self.recipe_identity, "stage": name, "input": previous}
        )
        destination = self.directory / "stages" / f"{name}-{key}"
        destination.parent.mkdir(exist_ok=True)
        if verify_stage(destination, key, self.limits["max_output_bytes"]):
            emit("stage_cached", stage=name, key=key, output=str(destination))
            return destination, read_json(
                destination / "result.json", 16 * MAX_MESSAGE_BYTES
            ) | {"input_artifact_sha256": sha256(destination / "completion.json")}
        if destination.exists():
            raise WorkerError(
                "CORRUPT_ASSET",
                "Cached stage hash validation failed; retain it for diagnosis and use a fresh job directory",
            )
        temporary = Path(tempfile.mkdtemp(prefix=f".{name}-", dir=destination.parent))
        start = time.monotonic()
        emit("stage_started", stage=name, key=key)
        try:
            result = operation(temporary)
            if CANCELLED.is_set():
                raise WorkerError("CANCELLED", "Job cancelled")
            atomic_json(temporary / "result.json", result)
            inventory = file_inventory(temporary, self.limits["max_output_bytes"])
            for entry in inventory:
                # Windows fsync requires write access; update mode preserves asset bytes.
                with (temporary / entry["path"]).open("r+b") as output:
                    os.fsync(output.fileno())
            atomic_json(
                temporary / "completion.json",
                {
                    "schema_version": 1,
                    "status": "complete",
                    "key": key,
                    "files": inventory,
                    "elapsed_seconds": time.monotonic() - start,
                },
            )
            os.replace(temporary, destination)
            atomic_json(
                self.directory / f"{name}.json",
                {"key": key, "output": str(destination)},
            )
            emit(
                "stage_completed",
                stage=name,
                key=key,
                output=str(destination),
                elapsed_seconds=time.monotonic() - start,
            )
            return destination, result | {
                "input_artifact_sha256": sha256(destination / "completion.json")
            }
        except Exception:
            # Preserve bounded diagnostics, never publish a partial stage as complete.
            emit(
                "stage_preserved",
                stage=name,
                output=str(temporary),
                status="incomplete",
            )
            raise

    def command(
        self, args: list[str], directory: Path, *, capture: int = MAX_LOG_BYTES
    ) -> bytes:
        with (directory / "commands.jsonl").open("a", encoding="utf-8") as log:
            log.write(json.dumps({"argv": args}) + "\n")
        return run_command(
            args,
            timeout=self.limits["stage_timeout_seconds"],
            log=directory / "backend.log",
            max_capture=capture,
            threads=self.limits["cpu_threads"],
            output_directory=directory,
            output_budget=self.limits["max_output_bytes"],
        )

    def inspect(self, directory: Path) -> dict:
        probe = executable("ffprobe")
        data = json.loads(
            self.command(
                [
                    probe,
                    "-v",
                    "error",
                    "-protocol_whitelist",
                    "file,pipe",
                    "-select_streams",
                    "v:0",
                    "-show_streams",
                    "-show_format",
                    "-of",
                    "json",
                    str(self.source),
                ],
                directory,
            )
        )
        streams = data.get("streams", [])
        if len(streams) != 1:
            raise WorkerError(
                "BAD_INPUT", "Source has no decodable primary video stream"
            )
        stream = streams[0]
        width, height = stream.get("width", 0), stream.get("height", 0)
        if (
            width <= 0
            or height <= 0
            or width * height > self.limits["max_image_pixels"]
        ):
            raise WorkerError("RESOURCE_LIMIT", "Video dimensions exceed image budget")
        duration = float(
            stream.get("duration", data.get("format", {}).get("duration", "nan"))
        )
        if (
            not math.isfinite(duration)
            or duration <= 0
            or duration > self.limits["max_duration_seconds"]
        ):
            raise WorkerError(
                "RESOURCE_LIMIT", "Missing, invalid, or over-budget video duration"
            )
        if stream.get("color_transfer") in ("smpte2084", "arib-std-b67"):
            raise WorkerError(
                "UNSUPPORTED",
                "HDR requires an explicit calibrated conversion; SDR recipe cannot process it",
            )
        try:
            time_base = Fraction(stream["time_base"])
            if time_base <= 0:
                raise ValueError()
        except (KeyError, ValueError, ZeroDivisionError) as error:
            raise WorkerError(
                "BAD_INPUT", "Missing/invalid original stream time base"
            ) from error
        rotation = 0
        for side_data in stream.get("side_data_list", []):
            rotation = side_data.get("rotation", rotation)
        if rotation not in (0, 90, -90, 180, -180, 270, -270):
            raise WorkerError(
                "UNSUPPORTED", "Only orthogonal display rotation is currently supported"
            )
        projected_bytes = width * height * 4 * self.limits["max_frames"]
        if (
            projected_bytes > self.limits["max_output_bytes"]
            or shutil.disk_usage(self.directory).free < projected_bytes
        ):
            raise WorkerError(
                "RESOURCE_LIMIT",
                "Insufficient output budget/free disk for decoded keyframes",
            )
        return {
            "schema_version": 1,
            "source": self.source_identity,
            "stream": stream,
            "duration_seconds": duration,
            "rotation_degrees": rotation,
            "audio": "ignored",
            "scale_status": "ARBITRARY",
            "color_domain": "srgb",
            "color_conversion": "ffmpeg SDR RGB24; no exposure adjustment",
            "gravity": "unknown; display world orientation is conventional",
        }

    def decode(self, directory: Path, inspection: dict) -> dict:
        if "frozen_views" in self.request:
            try:
                return _frozen_view_helper().decode(
                    self,
                    directory,
                    inspection,
                    SimpleNamespace(
                        CANCELLED=CANCELLED, WorkerError=WorkerError, sha256=sha256
                    ),
                )
            except (ValueError, OSError, KeyError, TypeError) as error:
                raise WorkerError("BAD_INPUT", str(error), stage="decode") from error
        quality_profile = (
            self.request.get("frame_selection", "timestamp_sampling_v1")
            != "timestamp_sampling_v1"
        )
        probe = executable("ffprobe")
        data = json.loads(
            self.command(
                [
                    probe,
                    "-v",
                    "error",
                    "-protocol_whitelist",
                    "file,pipe",
                    "-select_streams",
                    "v:0",
                    "-show_frames",
                    "-show_entries",
                    "frame=pts,width,height",
                    "-of",
                    "json",
                    str(self.source),
                ],
                directory,
                capture=min(
                    self.limits["max_source_frames"] * 200, 16 * MAX_MESSAGE_BYTES
                ),
            )
        )
        frames = data.get("frames", [])
        if len(frames) > self.limits["max_source_frames"]:
            raise WorkerError("RESOURCE_LIMIT", "Source frame count exceeds limit")
        time_base = Fraction(inspection["stream"]["time_base"])
        interval = Fraction(1, 1) / Fraction(str(self.request["sample_fps"]))
        selected = []
        previous_pts = None
        next_time = None
        for index, frame in enumerate(frames):
            pts = frame.get("pts")
            if type(pts) is not int or (
                previous_pts is not None and pts <= previous_pts
            ):
                raise WorkerError(
                    "BAD_INPUT",
                    "Missing or non-monotonic original PTS; timestamps were not synthesized",
                )
            if (frame.get("width"), frame.get("height")) != (
                inspection["stream"]["width"],
                inspection["stream"]["height"],
            ):
                raise WorkerError(
                    "BAD_INPUT",
                    "Lens/image dimensions change; split capture into calibrated segments",
                )
            previous_pts = pts
            instant = pts * time_base
            if next_time is None or instant >= next_time:
                candidate_limit = (
                    min(480, self.limits["max_frames"] * 4)
                    if quality_profile
                    else self.limits["max_frames"]
                )
                if len(selected) == candidate_limit:
                    raise WorkerError(
                        "RESOURCE_LIMIT",
                        "Frame/candidate budget exceeded; trim video or lower sample_fps",
                    )
                selected.append(
                    {
                        "frame_id": f"{self.source_hash[:16]}-{index}",
                        "source_index": index,
                        "pts": pts,
                        "time_base": str(time_base),
                        "capture_time_ns": int(instant * 10**9),
                        "calibration_id": f"{self.source_hash[:16]}-lens-0",
                        "file": f"images/{len(selected) + 1:06d}.png",
                    }
                )
                next_time = instant + interval
        if len(selected) < 3:
            raise WorkerError(
                "BAD_INPUT",
                "At least three distinct timestamped keyframes are required",
            )
        ledger = None
        if quality_profile:
            selected, ledger = self.select_quality(directory, inspection, selected)
        (directory / "images").mkdir()
        expression = "+".join(f"eq(n\\,{frame['source_index']})" for frame in selected)
        ffmpeg = executable("ffmpeg")
        self.command(
            [
                ffmpeg,
                "-nostdin",
                "-v",
                "error",
                "-protocol_whitelist",
                "file,pipe",
                "-threads",
                str(self.limits["cpu_threads"]),
                "-i",
                str(self.source),
                "-map",
                "0:v:0",
                "-an",
                "-sn",
                "-dn",
                "-vf",
                f"select={expression}",
                "-fps_mode",
                "passthrough",
                "-pix_fmt",
                "rgb24",
                "-threads",
                "1",
                str(directory / "images" / "%06d.png"),
            ],
            directory,
        )
        if len(list((directory / "images").glob("*.png"))) != len(selected):
            raise WorkerError(
                "DECODE_FAILED",
                "Decoded image count differs from selected original frames",
            )
        if sha256(self.source) != self.source_hash:
            raise WorkerError(
                "BAD_INPUT", "Source changed during decode; no stage was committed"
            )
        hashes = set()
        duplicates = 0
        for frame in selected:
            frame["sha256"] = sha256(directory / frame["file"])
            duplicates += frame["sha256"] in hashes
            hashes.add(frame["sha256"])
        if len(hashes) < 3:
            raise WorkerError(
                "INSUFFICIENT_PARALLAX", "Fewer than three distinct decoded images"
            )
        result = {
            "frames": selected,
            "source_frame_count": len(frames),
            "duplicate_images": duplicates,
            "selection": "timestamp_sampling_v1",
            "rotation_degrees": inspection["rotation_degrees"],
            "resize": "none",
            "crop": "none",
            "lens_change_detection": "dimension_metadata_only",
            "diagnostics_pending": [
                "blur/exposure/motion/cut selection requires M3 qualification"
            ],
        }
        if ledger is not None:
            selected_iter = iter(selected)
            for decision in ledger["decisions"]:
                if decision["selected"]:
                    decision["frame"] = next(selected_iter)
            atomic_json(directory / "quality.json", ledger)
            result |= {
                "selection": self.request["frame_selection"],
                "source_sha256": self.source_hash,
                "quality_ledger": "quality.json",
                "quality_ledger_sha256": sha256(directory / "quality.json"),
                "diagnostics_pending": [
                    "candidate thresholds unqualified; visual lens changes and 3D coverage unresolved"
                ],
            }
            if self.request["frame_selection"] == "quality_keyframes_candidate_v3":
                result["lens_change_detection"] = "ambiguous_image_scale_preflight"
                result["diagnostics_pending"] = [
                    "candidate thresholds unqualified; no fixed-intrinsics or 3D coverage guarantee"
                ]
        return result

    def select_quality(
        self, directory: Path, inspection: dict, candidates: list[dict]
    ) -> tuple[list[dict], dict]:
        # Load only the repository-owned helper, including when imported by a host
        # with a different sys.path. Its bytes also enter the job recipe identity.
        calibration = (
            self.request["frame_selection"] == "quality_keyframes_candidate_v3"
        )
        helper = ROOT / (
            "frame_calibration.py"
            if calibration
            else (
                "frame_tracking.py"
                if self.request["frame_selection"] == "quality_keyframes_candidate_v2"
                else "frame_quality.py"
            )
        )
        implementation_files = (
            {
                name: sha256(ROOT / name)
                for name in ("frame_calibration.py", "frame_tracking.py")
            }
            if calibration
            else None
        )
        specification = importlib.util.spec_from_file_location(
            "we_frame_quality", helper
        )
        implementation_sha256 = (
            canonical_hash(implementation_files) if calibration else sha256(helper)
        )
        quality = importlib.util.module_from_spec(specification)
        specification.loader.exec_module(quality)

        deadline = time.monotonic() + self.limits["stage_timeout_seconds"]

        def check_cancelled() -> None:
            if CANCELLED.is_set():
                raise WorkerError("CANCELLED", "Quality selection cancelled")
            if time.monotonic() > deadline:
                raise WorkerError(
                    "RESOURCE_LIMIT", "Quality analysis deadline exceeded"
                )

        expression = "+".join(
            f"eq(n\\,{frame['source_index']})" for frame in candidates
        )
        thumbnail_path = directory / "candidates.gray"
        thumbnail_bytes = len(candidates) * 96 * 64
        if thumbnail_bytes > self.limits["max_output_bytes"]:
            raise WorkerError("RESOURCE_LIMIT", "Quality thumbnail budget exceeded")
        self.command(
            [
                executable("ffmpeg"),
                "-nostdin",
                "-v",
                "error",
                "-protocol_whitelist",
                "file,pipe",
                "-threads",
                str(self.limits["cpu_threads"]),
                "-i",
                str(self.source),
                "-map",
                "0:v:0",
                "-an",
                "-sn",
                "-dn",
                "-vf",
                f"select={expression},scale=96:64:flags=area,format=gray",
                "-fps_mode",
                "passthrough",
                "-threads",
                "1",
                "-f",
                "rawvideo",
                str(thumbnail_path),
            ],
            directory,
        )
        if thumbnail_path.stat().st_size != thumbnail_bytes:
            raise WorkerError("DECODE_FAILED", "Quality thumbnail count mismatch")
        if sha256(self.source) != self.source_hash:
            raise WorkerError(
                "BAD_INPUT", "Source changed during quality thumbnail decode"
            )
        data = thumbnail_path.read_bytes()
        decisions = quality.analyze(
            [data[offset : offset + 6144] for offset in range(0, len(data), 6144)],
            check_cancelled,
        )
        current_files = (
            {name: sha256(ROOT / name) for name in implementation_files}
            if calibration
            else None
        )
        if (
            canonical_hash(current_files) if calibration else sha256(helper)
        ) != implementation_sha256:
            raise WorkerError(
                "BAD_INPUT", "Quality implementation changed during analysis"
            )
        for frame, decision in zip(candidates, decisions):
            decision["frame"] = {
                key: value for key, value in frame.items() if key != "file"
            }
        width, height = inspection["stream"]["width"], inspection["stream"]["height"]
        if inspection["rotation_degrees"] % 180:
            width, height = height, width
        ledger = {
            "schema_version": 1,
            "source_sha256": self.source_hash,
            "profile": quality.PROFILE,
            "policy": quality.policy(),
            "policy_sha256": canonical_hash(quality.policy()),
            "implementation_sha256": implementation_sha256,
            "sample_fps": self.request["sample_fps"],
            "candidate_limit": min(480, self.limits["max_frames"] * 4),
            "thumbnails": {
                "file": "candidates.gray",
                "sha256": sha256(thumbnail_path),
                "bytes": thumbnail_bytes,
                "format": "GRAY8",
                "transform": "FFmpeg display rotation then area resize; analysis only",
                "display_width": width,
                "display_height": height,
                "scale_x": 96 / width,
                "scale_y": 64 / height,
                "offset_x": (96 / width - 1) / 2,
                "offset_y": (64 / height - 1) / 2,
            },
            "decisions": decisions,
        }
        if calibration:
            ledger["implementation_files"] = implementation_files
        # Preserve a diagnostic ledger even on failure; stage_run never commits it.
        atomic_json(directory / "quality.json", ledger)
        if calibration and any(
            entry["calibration"]["status"] == "ambiguous_scale_change"
            for entry in decisions
        ):
            raise WorkerError(
                "CALIBRATION_REQUIRED",
                "Ambiguous image-scale change (zoom or forward motion); single-calibration decode refused; inspect quality.json",
            )
        if any(
            entry["reason"] in ("tracking_break", "suspected_cut")
            for entry in decisions
        ):
            raise WorkerError(
                "INSUFFICIENT_PARALLAX",
                "Candidate tracking break/suspected cut; inspect quality.json before retrying or splitting capture",
            )
        selected = [entry["frame"].copy() for entry in decisions if entry["selected"]]
        if not 3 <= len(selected) <= self.limits["max_frames"]:
            raise WorkerError(
                "INSUFFICIENT_PARALLAX",
                "Quality selection requires 3..max_frames retained keyframes",
            )
        for index, frame in enumerate(selected):
            frame["file"] = f"images/{index + 1:06d}.png"
        return selected, ledger

    def poses(
        self,
        directory: Path,
        decode_directory: Path,
        decoded: dict,
        *,
        retained_sparse: Path | None = None,
    ) -> dict:
        images = str(decode_directory / "images")
        sparse = directory / "sparse"
        threads = str(self.limits["cpu_threads"])
        pose_python = self.request.get("pose_python", sys.executable)
        if retained_sparse is None:
            self.command(
                [
                    pose_python,
                    str(ROOT / "colmap" / "solve.py"),
                    "--images",
                    images,
                    "--output",
                    str(directory),
                    "--threads",
                    threads,
                    "--recipe",
                    self.request.get("pose_recipe", "sequential"),
                ],
                directory,
            )
        else:
            shutil.copytree(retained_sparse, sparse)
        models = [
            p
            for p in sorted(sparse.iterdir())
            if p.is_dir() and (p / "cameras.bin").exists()
        ]
        if not models:
            raise WorkerError(
                "INSUFFICIENT_PARALLAX",
                "COLMAP produced no registered model; recapture with overlapping lateral motion",
            )
        selection_path = directory / "model_selection.json"
        self.command(
            [
                pose_python,
                str(ROOT / "colmap" / "inspect_models.py"),
                "--sparse",
                str(sparse),
                "--frames",
                str(decode_directory / "result.json"),
                "--output",
                str(selection_path),
            ],
            directory,
        )
        selection = read_json(selection_path, MAX_MESSAGE_BYTES)
        if selection["selected_model"] is None:
            raise WorkerError(
                "POSE_DISCONNECTED",
                "No single COLMAP model covers the other candidates' registered frames; recovery is required",
            )
        model = next(
            (path for path in models if path.name == selection["selected_model"]), None
        )
        if model is None:
            raise WorkerError(
                "CORRUPT_ASSET", "Selected model is outside the solve inventory"
            )
        canonical = directory / "canonical_poses.json"
        self.command(
            [
                self.request.get("pose_python", sys.executable),
                str(ROOT / "colmap" / "export_poses.py"),
                "--model",
                str(model),
                "--frames",
                str(decode_directory / "result.json"),
                "--output",
                str(canonical),
            ],
            directory,
        )
        pose_info = read_json(canonical, 16 * MAX_MESSAGE_BYTES)
        if pose_info["registered_frames"] / len(decoded["frames"]) < 0.9:
            raise WorkerError(
                "POSE_DISCONNECTED",
                "Less than 90% of selected frames registered; recovery is required",
            )
        dense = directory / "training"
        self.command(
            [
                pose_python,
                str(ROOT / "colmap" / "solve.py"),
                "--images",
                images,
                "--output",
                str(dense),
                "--threads",
                threads,
                "--undistort-model",
                str(model),
            ],
            directory,
        )
        # The pinned trainer parser accepts sparse/0 or sparse; preserve COLMAP output unchanged.
        return {
            "model": str(model.relative_to(directory)),
            "training": "training",
            "canonical_poses": "canonical_poses.json",
            "registered_frames": pose_info["registered_frames"],
            "scale_status": "ARBITRARY",
            "geometry": "UNAVAILABLE",
        }

    def appearance(self, directory: Path, poses_directory: Path, poses: dict) -> dict:
        source = self.request.get("gsplat_source")
        weights_hash = self.request.get("alexnet_weights_sha256")
        if not source or not weights_hash:
            raise WorkerError(
                "UNSUPPORTED",
                "Appearance requires installed pinned gsplat source and a reviewed cached AlexNet weights SHA-256; see workers/gsplat/README.md",
            )
        steps = 7000 if self.request["preset"] == "standard" else 30000
        self.command(
            [
                self.request.get("appearance_python", sys.executable),
                str(ROOT / "gsplat" / "train.py"),
                "--source",
                str(Path(source).resolve()),
                "--data",
                str(poses_directory / poses["training"]),
                "--output",
                str(directory),
                "--steps",
                str(steps),
                "--max-splats",
                str(self.limits["max_splats"]),
                "--alexnet-weights-sha256",
                weights_hash,
            ],
            directory,
            capture=MAX_LOG_BYTES,
        )
        export = directory / "appearance.ply"
        if not export.is_file() or not export.stat().st_size:
            raise WorkerError(
                "CORRUPT_ASSET", "Trainer did not produce a validated Gaussian export"
            )
        return {
            "ply": "appearance.ply",
            "exporter": "gsplat-1.5.3",
            "steps": steps,
            "preset": self.request["preset"],
            "scale_status": "ARBITRARY",
            "color_domain": "srgb",
            "geometry": "UNAVAILABLE",
            "walk": "UNAVAILABLE",
            "checkpoint_semantics": "upstream evaluation checkpoint; training restarts on interruption",
            "quality_gate": "UNVERIFIED",
        }

    def run(self) -> dict:
        with self.lock():
            atomic_json(self.directory / "request.json", self.request)
            atomic_json(self.directory / "capabilities.json", self.backend)
            inspect_directory, inspected = self.stage_run(
                "inspect", self.source_identity, self.inspect
            )
            result = {"stage": "inspect", "output": str(inspect_directory)}
            if self.request["stop_after"] != "inspect":
                decode_directory, decoded = self.stage_run(
                    "decode",
                    inspected,
                    lambda directory: self.decode(directory, inspected),
                )
                result = {"stage": "decode", "output": str(decode_directory)}
                if self.request["stop_after"] != "decode":
                    poses_directory, poses = self.stage_run(
                        "poses",
                        decoded,
                        lambda directory: self.poses(
                            directory, decode_directory, decoded
                        ),
                    )
                    result = {"stage": "poses", "output": str(poses_directory)}
                    if self.request["stop_after"] == "appearance":
                        appearance_directory, appearance = self.stage_run(
                            "appearance",
                            poses,
                            lambda directory: self.appearance(
                                directory, poses_directory, poses
                            ),
                        )
                        result = {
                            "stage": "appearance",
                            "output": str(appearance_directory),
                            "ply": str(appearance_directory / appearance["ply"]),
                        }
            result |= {
                "schema_version": 1,
                "status": "complete",
                "source_sha256": self.source_hash,
                "scene_compilation": "required",
            }
            atomic_json(self.directory / "result.json", result)
            return result


def main() -> int:
    global WIRE
    import we_protocol

    WIRE = we_protocol
    WIRE.configure(
        sys.argv,
        cancel_callback=CANCELLED.set,
        backend="world_explorer_local",
        backend_version="0.1.0",
        sensors=["video", "image_sequence"],
        devices=["cpu_poses", "cuda_appearance_requires_probe"],
        checkpoint_semantics="completed stages; upstream appearance restarts",
        checkpoint_rights="local user data; backend package licenses apply",
        limits=LIMITS,
    )
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("capabilities")
    inspect = commands.add_parser("inspect")
    inspect.add_argument("--source", required=True)
    inspect.add_argument("--job-dir", required=True)
    run = commands.add_parser("run")
    run.add_argument("--request", required=True)
    journal = commands.add_parser("solve-journal")
    journal.add_argument("--request", required=True)
    args = parser.parse_args()
    for sig in (signal.SIGINT, signal.SIGTERM):
        signal.signal(sig, lambda *_: CANCELLED.set())
    job = None
    try:
        if args.command == "capabilities":
            emit("capabilities", **capabilities())
            WIRE.completed("capabilities")
            return 0
        if args.command == "solve-journal":
            from colmap.journal import run as run_journal

            with WIRE.diagnostic_context():
                result = run_journal(
                    sys.modules[__name__], read_json(Path(args.request))
                )
            emit("job_completed", result=result)
            return 0
        request = (
            read_json(Path(args.request))
            if args.command == "run"
            else {
                "schema_version": 1,
                "source": args.source,
                "job_dir": args.job_dir,
                "stop_after": "inspect",
            }
        )
        job = Job(request)
        emit("job_started", source_sha256=job.source_hash, job_dir=str(job.directory))
        with WIRE.diagnostic_context():
            result = job.run()
        emit("job_completed", result=result)
        return 0
    except (WorkerError, OSError, ValueError, KeyError, TypeError) as error:
        code = error.code if isinstance(error, WorkerError) else "BAD_INPUT"
        emit(
            "job_failed",
            code=code,
            stage=getattr(error, "stage", None) or (job.stage if job else "request"),
            message=str(error),
            recovery="Committed stages are retained. Correct the cause and resubmit the same request.",
        )
        return 130 if code == "CANCELLED" else 2


if __name__ == "__main__":
    raise SystemExit(main())
