"""Bounded COLMAP 4.2.1 depth/consistency interchange; no backend imports."""

from array import array
import math
from pathlib import Path
import re
import struct
import sys

__all__ = [
    "MAX_BYTES",
    "MAX_PIXELS",
    "encode_depth",
    "read_depth",
    "read_map",
    "read_support",
]

MAX_BYTES = 256 * 1024**2
MAX_PIXELS = 4096**2


def read_map(path, width, height):
    if not (
        type(width) is int
        and type(height) is int
        and 11 <= width <= 8192
        and 11 <= height <= 8192
        and width * height <= MAX_PIXELS
    ):
        raise ValueError("Unsupported native dense grid")
    path = Path(path)
    if path.is_symlink() or not path.is_file() or path.stat().st_size > MAX_BYTES:
        raise ValueError("Missing, linked or oversized native map")
    with path.open("rb") as stream:
        data = stream.read(MAX_BYTES + 1)
    if len(data) > MAX_BYTES:
        raise ValueError("Native map grew beyond budget")
    # Exact canonical ASCII header, followed immediately by little-endian bytes.
    header = f"{width}&{height}&1&".encode("ascii")
    if not data.startswith(header):
        raise ValueError("Native map grid/header differs")
    return memoryview(data)[len(header) :]


def read_depth(path, width, height):
    data = read_map(path, width, height)
    if len(data) != width * height * 4:
        raise ValueError("Truncated or trailing native depth")
    values = array("f")
    values.frombytes(data)
    if values.itemsize != 4:
        raise RuntimeError("Unsupported host float representation")
    if sys.byteorder != "little":
        values.byteswap()
    if any(not math.isfinite(value) or value < 0 for value in values):
        raise ValueError("Nonfinite or negative native depth")
    return values


def read_support(path, width, height, reference_index, source_indices):
    sources = set(source_indices)
    if (
        type(reference_index) is not int
        or reference_index < 0
        or not sources
        or len(sources) > 20
        or len(sources) != len(source_indices)
        or reference_index in sources
        or any(type(value) is not int or not 0 <= value < 120 for value in sources)
    ):
        raise ValueError("Invalid consistency image inventory")
    data = read_map(path, width, height)
    if len(data) % 4:
        raise ValueError("Truncated consistency scalar")
    support = array("I", [0]) * (width * height)
    if support.itemsize != 4:
        raise RuntimeError("Unsupported host count representation")
    offset = 0
    while offset < len(data):
        if len(data) - offset < 12:
            raise ValueError("Truncated consistency record")
        # Pinned source uses COLUMN then ROW, despite older format descriptions.
        column, row, count = struct.unpack_from("<iii", data, offset)
        offset += 12
        if not (
            0 <= column < width and 0 <= row < height and 1 <= count <= len(sources)
        ):
            raise ValueError("Invalid consistency coordinates/count")
        if len(data) - offset < count * 4:
            raise ValueError("Truncated consistency sources")
        indices = struct.unpack_from("<" + "i" * count, data, offset)
        offset += count * 4
        if len(set(indices)) != count or not set(indices) <= sources:
            raise ValueError("Unknown/duplicate consistency source")
        pixel = row * width + column
        if support[pixel]:
            raise ValueError("Duplicate consistency pixel")
        support[pixel] = count + 1  # Actual source observations plus the reference.
    return support


def encode_depth(frame, manifest, depth, support, producer=2, source_count=2):
    if type(source_count) is not int or not 2 <= source_count <= 20:
        raise ValueError("Invalid selected source count")
    width, height = frame["width"], frame["height"]
    count = width * height
    if (
        producer not in (1, 2)
        or count > MAX_PIXELS
        or len(depth) != count
        or len(support) != count
    ):
        raise ValueError("Invalid depth export inventory")
    hashes = [
        manifest["pose_asset_sha256"],
        frame["observation_image_sha256"],
        frame["calibrated_image_sha256"],
        manifest["depth_recipe_sha256"],
    ]
    if any(
        not isinstance(h, str) or not re.fullmatch("[0-9a-f]{64}", h) for h in hashes
    ):
        raise ValueError("Invalid depth dependency digest")
    intrinsics = frame["intrinsics"]
    if (
        len(intrinsics) != 4
        or not all(math.isfinite(v) for v in intrinsics)
        or min(intrinsics[:2]) <= 0
    ):
        raise ValueError("Invalid export intrinsics")
    identifiers = [
        manifest["world_epoch"],
        manifest["pose_revision"],
        frame["frame"],
        frame["calibration"],
    ]
    if any(type(v) is not int or not 0 <= v < 2**64 for v in identifiers):
        raise ValueError("Invalid export identifier")
    if not identifiers[1] or not identifiers[2] or not identifiers[3]:
        raise ValueError("Missing export dependency")
    header = struct.pack(
        "<8s7I4Q4d",
        b"WEXDEP01",
        1,
        1,
        producer,
        0,
        width,
        height,
        0,
        *identifiers,
        *intrinsics,
    ) + "".join(hashes).encode("ascii")
    valid = bytearray(count)
    confidence = array("f", [0]) * count
    normalized = array("f", [0]) * count
    counts = array("I", [0]) * count
    for pixel, (value, observations) in enumerate(zip(depth, support)):
        if (
            not math.isfinite(value)
            or value < 0
            or not 0 <= observations <= source_count + 1
        ):
            raise ValueError("Invalid export sample")
        if value > 0 and observations >= 3:
            valid[pixel] = 1
            normalized[pixel] = value
            counts[pixel] = observations
            # Fixed recipe weight; no statistical confidence/uncertainty claim.
            confidence[pixel] = (observations - 1) / source_count
            if confidence[pixel] > 1:
                raise ValueError("Depth support exceeds selected sources")
    if sys.byteorder != "little":
        for plane in (normalized, confidence, counts):
            plane.byteswap()
    encoded = (
        header + normalized.tobytes() + valid + confidence.tobytes() + counts.tobytes()
    )
    if len(encoded) != 356 + count * 13 or len(encoded) > MAX_BYTES:
        raise ValueError("Depth export exceeds byte profile")
    return encoded, sum(valid)
