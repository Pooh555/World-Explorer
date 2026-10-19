"""Checked native RGB journal -> transactional PNG stage -> existing COLMAP solve."""

import hashlib
import json
from pathlib import Path
import re
from fractions import Fraction


__all__ = [
    "run",
]


def run(worker, request, *, retained_sparse=None):
    def reject(message, code="BAD_INPUT"):
        raise worker.WorkerError(code, message, stage="journal")

    def digest(value):
        return (
            isinstance(value, str) and re.fullmatch(r"[0-9a-f]{64}", value) is not None
        )

    allowed = {
        "schema_version",
        "journal",
        "journal_sha256",
        "job_dir",
        "pose_python",
        "pose_recipe",
        "limits",
    }
    if (
        set(request) - allowed
        or request.get("schema_version") != 1
        or not digest(request.get("journal_sha256"))
    ):
        reject("Invalid batch journal request")
    for name in ("journal", "job_dir", "pose_python"):
        if (
            not isinstance(request.get(name), str)
            or not request[name]
            or len(request[name]) > 4096
            or "\0" in request[name]
        ):
            reject("Invalid journal request path/executable")
    if request.get("pose_recipe", "sequential") not in (
        "sequential",
        "exhaustive_guided",
    ):
        reject("Unsupported batch pose recipe")
    limits = worker.LIMITS | request.get("limits", {})
    if set(limits) != set(worker.LIMITS) or any(
        type(value) is not int or not 0 < value <= worker.LIMITS[key]
        for key, value in limits.items()
    ):
        reject("Invalid journal worker limits")
    directory = Path(request["job_dir"]).resolve(strict=True)
    journal_path = Path(request["journal"])
    if (
        journal_path.is_symlink()
        or journal_path.resolve(strict=True) != directory / "input/journal.json"
    ):
        reject("Journal must be an immutable file in the batch input directory")
    if (
        not journal_path.is_file()
        or journal_path.stat().st_size > worker.MAX_MESSAGE_BYTES
    ):
        reject("Missing or oversized native journal")
    data = journal_path.read_bytes()
    if (
        len(data) > worker.MAX_MESSAGE_BYTES
        or hashlib.sha256(data).hexdigest() != request["journal_sha256"]
    ):
        reject("Journal hash mismatch", "CORRUPT_ASSET")
    journal = json.loads(data)
    journal_fields = {
        "schema_version",
        "profile",
        "source_sha256",
        "decode_completion_sha256",
        "source_frame_count",
        "frames",
    }
    if (
        not isinstance(journal, dict)
        or set(journal) != journal_fields
        or journal.get("schema_version") != 1
        or journal.get("profile") != "native_rgb_journal_v1"
        or not digest(journal.get("source_sha256"))
        or not digest(journal.get("decode_completion_sha256"))
    ):
        reject("Unsupported journal source/provenance profile")
    frames = journal.get("frames")
    source_count = journal.get("source_frame_count")
    if (
        type(source_count) is not int
        or not 0 < source_count <= limits["max_source_frames"]
        or not isinstance(frames, list)
        or not 3 <= len(frames) <= limits["max_frames"]
    ):
        reject("Journal selection exceeds frame limits", "RESOURCE_LIMIT")
    source_hash = journal["source_sha256"]
    previous_index = previous_ns = -1
    dimensions = None
    total = 0
    frame_fields = {
        "source_index",
        "pts",
        "capture_time_ns",
        "frame_id",
        "calibration_id",
        "width",
        "height",
        "bytes",
        "file",
        "pixel_sha256",
        "sha256",
        "time_base",
    }
    for index, frame in enumerate(frames):
        if not isinstance(frame, dict) or set(frame) != frame_fields:
            reject("Invalid journal frame schema")
        for name in (
            "source_index",
            "pts",
            "capture_time_ns",
            "width",
            "height",
            "bytes",
        ):
            if type(frame.get(name)) is not int or frame[name] < 0:
                reject("Invalid journal integer metadata")
        if (
            not previous_index < frame["source_index"] < source_count
            or frame.get("frame_id") != f"{source_hash[:16]}-{frame['source_index']}"
            or frame.get("calibration_id") != f"{source_hash[:16]}-lens-0"
        ):
            reject("Journal frame/calibration identities differ from the source")
        base = frame.get("time_base")
        if (
            not isinstance(base, str)
            or len(base) > 32
            or not re.fullmatch(r"[1-9][0-9]*/[1-9][0-9]*", base)
        ):
            reject("Invalid journal time base")
        numerator, denominator = map(int, base.split("/"))
        if (
            max(numerator, denominator) > 2147483647
            or frame["pts"] > 9223372036854775807
        ):
            reject("Journal original timestamp overflows")
        ns = int(frame["pts"] * Fraction(base) * 10**9)
        if (
            ns != frame["capture_time_ns"]
            or not previous_ns < ns <= limits["max_duration_seconds"] * 10**9
        ):
            reject("Journal timestamps are inconsistent or unordered")
        width, height = frame["width"], frame["height"]
        if (
            not 0 < width <= 8192
            or not 0 < height <= 8192
            or width * height > limits["max_image_pixels"]
        ):
            reject("Journal dimensions exceed limits", "RESOURCE_LIMIT")
        if dimensions is not None and dimensions != (width, height):
            reject("Journal changes dimensions within one calibration")
        if (
            frame.get("file") != f"{index + 1}.ppm"
            or not digest(frame.get("sha256"))
            or not digest(frame.get("pixel_sha256"))
        ):
            reject("Invalid journal asset reference")
        path = directory / "input" / frame["file"]
        header = f"P6\n{width} {height}\n255\n".encode("ascii")
        if (
            path.is_symlink()
            or not path.is_file()
            or path.resolve(strict=True) != path
            or frame["bytes"] != len(header) + width * height * 3
            or path.stat().st_size != frame["bytes"]
        ):
            reject("Journal asset shape or containment mismatch", "CORRUPT_ASSET")
        total += frame["bytes"]
        if total > limits["max_source_bytes"]:
            reject("Journal input byte budget exceeded", "RESOURCE_LIMIT")
        if worker.sha256(path) != frame["sha256"]:
            reject("Journal asset checksum mismatch", "CORRUPT_ASSET")
        pixels = hashlib.sha256()
        with path.open("rb") as image:
            if image.read(len(header)) != header:
                reject("Journal RGB header mismatch", "CORRUPT_ASSET")
            while block := image.read(1024 * 1024):
                if worker.CANCELLED.is_set():
                    reject("Journal verification cancelled", "CANCELLED")
                pixels.update(block)
        if pixels.hexdigest() != frame["pixel_sha256"]:
            reject("Journal RGB checksum mismatch", "CORRUPT_ASSET")
        previous_index, previous_ns, dimensions = (
            frame["source_index"],
            ns,
            (width, height),
        )
    expected = {"journal.json"} | {frame["file"] for frame in frames}
    if (
        any(
            path.is_symlink() or not path.is_file()
            for path in (directory / "input").iterdir()
        )
        or {path.name for path in (directory / "input").iterdir()} != expected
    ):
        reject("Journal input inventory contains uncommitted assets", "CORRUPT_ASSET")

    job = worker.Job(
        request | {"limits": limits},
        journal_identity={
            "sha256": source_hash,
            "journal_sha256": request["journal_sha256"],
        },
    )
    retained_inventory = None
    if retained_sparse is not None:
        retained_inventory = worker.file_inventory(
            retained_sparse, limits["max_output_bytes"]
        )
        job.recipe_identity = worker.canonical_hash(
            {"recipe": job.recipe_identity, "retained_sparse": retained_inventory}
        )

    def decode(output):
        (output / "images").mkdir()
        selected = []
        hashes = set()
        for index, frame in enumerate(frames):
            path = directory / "input" / frame["file"]
            if worker.sha256(path) != frame["sha256"]:
                reject("Journal input changed before conversion", "CORRUPT_ASSET")
            name = f"images/{index + 1:06d}.png"
            job.command(
                [
                    worker.executable("ffmpeg"),
                    "-nostdin",
                    "-v",
                    "error",
                    "-protocol_whitelist",
                    "file,pipe",
                    "-f",
                    "image2",
                    "-pattern_type",
                    "none",
                    "-c:v",
                    "ppm",
                    "-threads",
                    "1",
                    "-i",
                    str(path),
                    "-map",
                    "0:v:0",
                    "-frames:v",
                    "1",
                    "-pix_fmt",
                    "rgb24",
                    "-threads",
                    "1",
                    str(output / name),
                ],
                output,
            )
            if worker.sha256(path) != frame["sha256"]:
                reject("Journal input changed during conversion", "CORRUPT_ASSET")
            record = {
                key: frame[key]
                for key in (
                    "frame_id",
                    "calibration_id",
                    "source_index",
                    "pts",
                    "time_base",
                    "capture_time_ns",
                )
            }
            record |= {"file": name, "sha256": worker.sha256(output / name)}
            selected.append(record)
            hashes.add(record["sha256"])
        if len(hashes) < 3:
            reject("Fewer than three distinct admitted images", "INSUFFICIENT_PARALLAX")
        return {
            "frames": selected,
            "source_frame_count": source_count,
            "selection": "native_rgb_journal_v1",
            "source_sha256": source_hash,
            "journal_sha256": request["journal_sha256"],
            "crop": "none",
            "resize": "none",
            "rotation_degrees": 0,
            "duplicate_images": len(frames) - len(hashes),
        }

    try:
        with job.lock():
            worker.atomic_json(directory / "capabilities.json", job.backend)
            decoded_path, decoded = job.stage_run(
                "decode", {"journal_sha256": request["journal_sha256"]}, decode
            )

            def solve(output):
                if (
                    retained_sparse is not None
                    and worker.file_inventory(
                        retained_sparse, limits["max_output_bytes"]
                    )
                    != retained_inventory
                ):
                    reject("Retained sparse assets changed", "CORRUPT_ASSET")
                result = job.poses(
                    output, decoded_path, decoded, retained_sparse=retained_sparse
                )
                if retained_sparse is not None and (
                    worker.file_inventory(output / "sparse", limits["max_output_bytes"])
                    != retained_inventory
                    or worker.file_inventory(
                        retained_sparse, limits["max_output_bytes"]
                    )
                    != retained_inventory
                ):
                    reject("Retained/copied sparse inventory changed", "CORRUPT_ASSET")
                return result

            posed_path, _ = job.stage_run("poses", decoded, solve)
            result = {
                "schema_version": 1,
                "status": "complete",
                "stage": "poses",
                "output": str(posed_path),
                "source_sha256": source_hash,
                "journal_sha256": request["journal_sha256"],
                "scene_compilation": "required",
            }
            worker.atomic_json(directory / "result.json", result)
            return result
    except (worker.WorkerError, OSError, ValueError, KeyError, TypeError) as error:
        error.stage = job.stage
        raise
