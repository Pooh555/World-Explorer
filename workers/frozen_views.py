"""Copy an explicitly frozen inference selection into a transactional decode stage.

This pre-pose producer does not admit an asset capture or clear its byte budgets.
"""

import hashlib
import json
from pathlib import Path
import re
import shutil
import struct

__all__ = ["decode", "load_selection", "validate_configuration"]

K_MANIFEST_BYTES = 64 * 1024
K_IMAGE_BYTES = 16 * 1024**2
K_ROLES = {"INFERENCE", "HELD_OUT", "EVALUATION_ONLY"}


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def _digest(value: object) -> bool:
    return isinstance(value, str) and re.fullmatch(r"[0-9a-f]{64}", value) is not None


def _integer(value: object, maximum: int, minimum: int = 0) -> bool:
    return type(value) is int and minimum <= value <= maximum


def _pairs(items: list) -> dict:
    result = {}
    for key, value in items:
        _require(key not in result, "Duplicate frozen-view JSON key")
        result[key] = value
    return result


def load_selection(configuration: dict) -> tuple[dict, bytes]:
    """Read strict hash-bound selection declarations; never decode an image."""
    path = Path(configuration["manifest"])
    _require(
        not path.is_symlink()
        and path.is_file()
        and path.stat().st_size <= K_MANIFEST_BYTES,
        "Missing, linked or oversized frozen-view manifest",
    )
    with path.open("rb") as stream:
        body = stream.read(K_MANIFEST_BYTES + 1)
    _require(
        len(body) <= K_MANIFEST_BYTES
        and b"\0" not in body
        and hashlib.sha256(body).hexdigest() == configuration["sha256"],
        "Frozen-view manifest hash/size mismatch",
    )
    try:
        plan = json.loads(body, object_pairs_hook=_pairs)
    except (RecursionError, UnicodeError) as error:
        raise ValueError("Invalid frozen-view manifest encoding/depth") from error
    _require(
        isinstance(plan, dict)
        and set(plan)
        == {
            "schema_version",
            "source_sha256",
            "source_frame_count",
            "width",
            "height",
            "time_base",
            "role_blocks",
            "frames",
        }
        and type(plan["schema_version"]) is int
        and plan["schema_version"] == 1
        and _digest(plan["source_sha256"])
        and _integer(plan["source_frame_count"], 36000, 1)
        and _integer(plan["width"], 8192, 1)
        and _integer(plan["height"], 8192, 1)
        and plan["width"] * plan["height"] <= 4096**2
        and isinstance(plan["time_base"], str)
        and re.fullmatch(r"[1-9][0-9]{0,9}/[1-9][0-9]{0,9}", plan["time_base"])
        is not None,
        "Invalid frozen-view source/schema",
    )
    numerator, denominator = map(int, plan["time_base"].split("/"))
    _require(
        max(numerator, denominator) <= 2147483647, "Frozen-view time base overflows"
    )
    blocks = plan["role_blocks"]
    _require(
        isinstance(blocks, list) and 1 <= len(blocks) <= 128,
        "Frozen-view block budget exceeded",
    )
    end = 0
    for block in blocks:
        _require(
            isinstance(block, dict)
            and set(block) == {"first", "end", "role"}
            and _integer(block["first"], plan["source_frame_count"])
            and block["first"] == end
            and _integer(block["end"], plan["source_frame_count"], end + 1)
            and isinstance(block["role"], str)
            and block["role"] in K_ROLES,
            "Frozen role blocks must partition the complete source in order",
        )
        end = block["end"]
    _require(end == plan["source_frame_count"], "Frozen role blocks omit source frames")
    frames = plan["frames"]
    _require(
        isinstance(frames, list) and 4 <= len(frames) <= 64,
        "Frozen-view frame budget exceeded",
    )
    previous_index = previous_pts = -1
    names, hashes, roles = set(), set(), []
    for frame in frames:
        _require(
            isinstance(frame, dict)
            and set(frame) == {"source_index", "pts", "role", "file", "sha256", "bytes"}
            and _integer(
                frame["source_index"],
                plan["source_frame_count"] - 1,
                previous_index + 1,
            )
            and _integer(frame["pts"], 9223372036854775807, previous_pts + 1)
            and frame["pts"] * numerator * 10**9 // denominator <= 1200 * 10**9
            and isinstance(frame["role"], str)
            and frame["role"] in K_ROLES
            and isinstance(frame["file"], str)
            and re.fullmatch(r"[A-Za-z0-9_-]{1,120}\.png", frame["file"]) is not None
            and _digest(frame["sha256"])
            and _integer(frame["bytes"], K_IMAGE_BYTES, 1),
            "Invalid frozen-view row/index/timestamp/path/size",
        )
        role = next(
            b["role"] for b in blocks if b["first"] <= frame["source_index"] < b["end"]
        )
        _require(frame["role"] == role, "Frozen view crosses a temporal role boundary")
        _require(
            frame["file"] not in names and frame["sha256"] not in hashes,
            "Duplicate frozen-view path/content across selection",
        )
        names.add(frame["file"])
        hashes.add(frame["sha256"])
        roles.append(role)
        previous_index, previous_pts = frame["source_index"], frame["pts"]
    _require(
        roles.count("INFERENCE") >= 3 and len(set(roles)) >= 2,
        "Frozen selection needs inference and excluded views",
    )
    return plan, body


def validate_configuration(configuration: dict, source: Path, output: Path) -> dict:
    """Check disjoint local inputs before the worker creates its job directory."""
    _require(
        isinstance(configuration, dict)
        and set(configuration) == {"manifest", "sha256", "image_directory"}
        and _digest(configuration["sha256"]),
        "Invalid frozen-view configuration",
    )
    paths = {}
    for name in ("manifest", "image_directory"):
        value = configuration[name]
        _require(
            isinstance(value, str) and 0 < len(value) <= 4096 and "\0" not in value,
            "Invalid frozen-view input path",
        )
        path = Path(value)
        resolved = path.resolve(strict=True)
        _require(
            not path.is_symlink() and path.absolute() == resolved,
            "Frozen-view input path uses an alias",
        )
        _require(
            resolved != output
            and output not in resolved.parents
            and resolved not in output.parents,
            "Frozen-view output overlaps an input",
        )
        paths[name] = str(resolved)
    _require(
        Path(paths["image_directory"]).is_dir(),
        "Frozen-view image directory is missing",
    )
    result = configuration | paths
    load_selection(result)
    return result


def decode(job, directory: Path, inspection: dict, worker) -> dict:
    """Hash every selected input, then stream only inference PNGs into owned staging."""
    plan, body = load_selection(job.request["frozen_views"])
    _require(
        plan["source_sha256"] == job.source_hash,
        "Frozen selection belongs to a different source",
    )
    _require(
        inspection["rotation_degrees"] == 0
        and (inspection["stream"]["width"], inspection["stream"]["height"])
        == (plan["width"], plan["height"])
        and inspection["stream"]["time_base"] == plan["time_base"],
        "Frozen selection differs from inspected source image domain/time base",
    )
    selected = [f for f in plan["frames"] if f["role"] == "INFERENCE"]
    total = sum(f["bytes"] for f in selected) + len(body) + K_MANIFEST_BYTES
    _require(
        len(selected) <= job.limits["max_frames"]
        and total <= job.limits["max_output_bytes"]
        and shutil.disk_usage(directory).free >= total,
        "Frozen inference stage exceeds configured disk/frame budget",
    )
    image_root = Path(job.request["frozen_views"]["image_directory"])
    for frame in plan["frames"]:
        if worker.CANCELLED.is_set():
            raise worker.WorkerError("CANCELLED", "Frozen-view verification cancelled")
        path = image_root / frame["file"]
        _require(
            not path.is_symlink()
            and path.is_file()
            and path.resolve(strict=True) == path
            and path.stat().st_size == frame["bytes"]
            and worker.sha256(path) == frame["sha256"],
            "Frozen-view source image changed or uses an alias",
        )
        with path.open("rb") as stream:
            header = stream.read(29)
        _require(
            len(header) == 29
            and header[:8] == b"\x89PNG\r\n\x1a\n"
            and header[12:16] == b"IHDR"
            and struct.unpack(">II5B", header[16:29])[:4]
            == (plan["width"], plan["height"], 8, 2),
            "Frozen view is not a native RGB8 PNG",
        )
    (directory / "images").mkdir()
    records = []
    for index, frame in enumerate(selected):
        target = directory / "images" / f"{index + 1:06d}.png"
        digest = hashlib.sha256()
        count = 0
        with (
            (image_root / frame["file"]).open("rb") as source,
            target.open("xb") as output,
        ):
            while chunk := source.read(1024 * 1024):
                if worker.CANCELLED.is_set():
                    raise worker.WorkerError("CANCELLED", "Frozen-view copy cancelled")
                count += len(chunk)
                _require(
                    count <= frame["bytes"], "Frozen source image grew during copy"
                )
                digest.update(chunk)
                output.write(chunk)
        _require(
            count == frame["bytes"] and digest.hexdigest() == frame["sha256"],
            "Frozen source image changed during copy",
        )
        numerator, denominator = map(int, plan["time_base"].split("/"))
        records.append(
            dict(
                source_index=frame["source_index"],
                pts=frame["pts"],
                time_base=plan["time_base"],
                capture_time_ns=frame["pts"] * numerator * 10**9 // denominator,
                frame_id=f"{job.source_hash[:16]}-{frame['source_index']}",
                calibration_id=f"{job.source_hash[:16]}-lens-0",
                file=f"images/{index + 1:06d}.png",
                sha256=frame["sha256"],
            )
        )
    _require(
        load_selection(job.request["frozen_views"])[1] == body
        and worker.sha256(job.source) == job.source_hash,
        "Frozen manifest/video changed during preparation",
    )
    (directory / "selection.json").write_bytes(body)
    return dict(
        selection="frozen_inference_views_v1",
        source_sha256=job.source_hash,
        selection_manifest_sha256=job.request["frozen_views"]["sha256"],
        frames=records,
        source_frame_count=plan["source_frame_count"],
        resize="none",
        crop="none",
        rotation_degrees=0,
        diagnostics_pending=[
            "Pre-pose frozen input preparation only; asset capture/recipe/evaluation admission remains open"
        ],
    )
