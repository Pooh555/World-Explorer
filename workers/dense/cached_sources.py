#!/usr/bin/env python3
"""Freeze and score an explicitly selected local photometric cache; never solve."""

import argparse
import io
import json
from pathlib import Path
import signal
import sys
import tempfile

from pilot import digest, parse, publish, read_pinned
from source_selection import report_digest

MAX_FILE_BYTES = 256 * 1024**2
MAX_CACHE_BYTES = 512 * 1024**2
MODEL_FILES = ("cameras.bin", "images.bin", "points3D.bin", "rigs.bin", "frames.bin")
KIND = "PRIVATE_PHOTOMETRIC_SOURCE_CACHE_NOT_ADMITTED_GEOMETRY"


def implementation_digest():
    root = Path(__file__).parent
    return report_digest(
        {
            name: digest((root / name).read_bytes())
            for name in (
                "cached_sources.py",
                "visibility.py",
                "pilot.py",
                "source_selection.py",
                "colmap_io.py",
            )
        }
    )


def checked_path(root, name):
    if not isinstance(name, str):
        raise ValueError("Invalid cache-relative path")
    path = Path(name)
    if (
        path.is_absolute()
        or path.as_posix() != name
        or any(part in (".", "..") for part in path.parts)
        or not path.parts
    ):
        raise ValueError("Invalid cache-relative path")
    candidate = root / path
    if (
        candidate.resolve(strict=True).relative_to(root).as_posix() != name
        or candidate.is_symlink()
    ):
        raise ValueError("Linked/escaping cache path")
    return candidate


def new_output(output, inputs):
    if output.exists() or output.is_symlink():
        raise ValueError("Cache/report output must be new")
    target = output.resolve(strict=False)
    for path in inputs:
        path = path.resolve(strict=True)
        if target == path or target.is_relative_to(path) or path.is_relative_to(target):
            raise ValueError("Cache/report output overlaps an input")
    return target


def metadata(descriptor, prepared, result):
    if (
        set(descriptor) != {"schema_version", "kind", "manifest", "recipe", "files"}
        or type(descriptor["schema_version"]) is not int
        or descriptor["schema_version"] != 1
        or descriptor["kind"] != KIND
    ):
        raise ValueError("Unsupported private cache descriptor")
    documents = {}
    for name in ("manifest", "recipe"):
        entry = descriptor[name]
        if set(entry) != {"sha256", "text"} or not isinstance(entry["text"], str):
            raise ValueError("Invalid cached dependency document")
        encoded = entry["text"].encode()
        if len(encoded) > 1024**2 or digest(encoded) != entry["sha256"]:
            raise ValueError("Cached dependency checksum/budget differs")
        documents[name] = parse(encoded)
    manifest, recipe = documents["manifest"], documents["recipe"]
    if (
        manifest["schema_version"] != 1
        or manifest["kind"] != "DENSE_INPUTS_RESIZED_GRID"
        or recipe["scope"]
        not in (
            "PARTIAL_NINE_VIEW_NEIGHBOR_QUALIFICATION",
            "PARTIAL_PARALLAX_NEIGHBOR_QUALIFICATION",
        )
        or prepared["status"] != "PARTIAL_NEIGHBOR_INPUTS_PREPARED"
        or result["status"] != "PARTIAL_NEIGHBOR_PILOT_COMPLETE"
        or manifest["depth_recipe_sha256"] != descriptor["recipe"]["sha256"]
    ):
        raise ValueError("Expected completed resized development cache")
    for document in (prepared, result):
        if (
            document["manifest_sha256"] != descriptor["manifest"]["sha256"]
            or document["recipe_sha256"] != descriptor["recipe"]["sha256"]
            or document["worker_sha256"] != recipe["worker_sha256"]
        ):
            raise ValueError("Cache terminal dependency differs")
    if result["prepared_sha256"] != descriptor["files"]["prepared.json"]:
        raise ValueError("Cache terminal/preparation identity differs")
    selection = recipe["selection_indices"]
    frames = manifest["frames"]
    if (
        not isinstance(selection, list)
        or not 9 <= len(selection) <= 18
        or any(type(i) is not int for i in selection)
        or selection != sorted(set(selection))
        or selection[:9] != list(range(9))
        or selection[-1] >= len(frames)
        or len(frames) > 120
    ):
        raise ValueError("Invalid cached reference/view inventory")
    selected = [frames[i] for i in selection]
    names = [Path(frame["image"]).name for frame in selected]
    if names != sorted(set(names)) or set(prepared["neighbors"]) != set(names):
        raise ValueError("Cache graph/inventory differs")
    for name, row in prepared["neighbors"].items():
        if (
            not isinstance(row, list)
            or not 2 <= len(row) <= 6
            or len(set(row)) != len(row)
            or name in row
            or not set(row) <= set(names)
        ):
            raise ValueError("Cache source graph is not closed")
    if "neighbors" in recipe and recipe["neighbors"] != prepared["neighbors"]:
        raise ValueError("Cache source graph differs from recipe")
    if len(result["frames"]) != len(selected) or [
        Path(f["asset"]).name for f in result["frames"]
    ] != [n + ".wedepth" for n in names]:
        raise ValueError("Cache completion inventory differs")
    required = {
        "prepared.json": descriptor["files"]["prepared.json"],
        "result.json": descriptor["files"]["result.json"],
        "stereo/patch-match.cfg": prepared["config_sha256"],
    }
    if not {"cameras.bin", "images.bin", "points3D.bin"} <= set(
        prepared["sparse_sha256"]
    ) or not set(prepared["sparse_sha256"]) <= set(MODEL_FILES):
        raise ValueError("Invalid cache model inventory")
    required.update(
        {"sparse/" + name: sha for name, sha in prepared["sparse_sha256"].items()}
    )
    for frame, name in zip(selected, names):
        required["images/" + name] = frame["calibrated_image_sha256"]
        key = "stereo/depth_maps/" + name + ".photometric.bin"
        required[key] = descriptor["files"][key]
    if descriptor["files"] != required:
        raise ValueError("Cache file inventory/dependency differs")
    config = "".join(
        name + "\n" + ", ".join(row) + "\n"
        for name, row in prepared["neighbors"].items()
    )
    if digest(config.encode()) != prepared["config_sha256"]:
        raise ValueError("Cache graph/config identity differs")
    return selected, prepared["neighbors"]


def load_cache(root, descriptor, cancelled=lambda: False):
    """Pin bytes once, then parse only the owned bytes; no backend rereads caller paths."""
    import numpy as np
    from PIL import Image
    import pycolmap

    if pycolmap.__version__ != "4.2.1":
        raise RuntimeError("Pinned COLMAP 4.2.1 required")
    files = descriptor["files"]
    if not isinstance(files, dict) or not 1 <= len(files) <= 44:
        raise ValueError("Cache file-count budget exceeded")
    sizes = {name: checked_path(root, name).stat().st_size for name in files}
    if (
        any(size > MAX_FILE_BYTES for size in sizes.values())
        or sum(sizes.values()) > MAX_CACHE_BYTES
    ):
        raise ValueError("Cache input byte budget exceeded")
    data = {}
    for name, sha in files.items():
        if cancelled():
            raise InterruptedError("Cache verification cancelled")
        data[name] = read_pinned(
            checked_path(root, name), sha, min(MAX_FILE_BYTES, sizes[name])
        )
    frames, rows = metadata(
        descriptor, parse(data["prepared.json"]), parse(data["result.json"])
    )
    if sum(frame["width"] * frame["height"] for frame in frames) > 48_000_000:
        raise ValueError("Decoded cache pixel budget exceeded")
    with tempfile.TemporaryDirectory(prefix="we-source-cache-") as directory:
        private = Path(directory)
        for name in MODEL_FILES:
            if "sparse/" + name in data:
                (private / name).write_bytes(data["sparse/" + name])
        model = pycolmap.Reconstruction(private)
    images = {image.name: image for image in model.images.values()}
    views = []
    for frame in frames:
        if cancelled():
            raise InterruptedError("Cache decoding cancelled")
        name = Path(frame["image"]).name
        image = images[name]
        camera = model.cameras[image.camera_id]
        k = camera.calibration_matrix()
        intrinsics = [k[0, 0], k[1, 1], k[0, 2] - 0.5, k[1, 2] - 0.5]
        canonical = frame["world_from_camera"]
        q = canonical["rotation_wxyz"]
        pose = image.cam_from_world().inverse().matrix().copy()
        pose[1:3] *= -1
        expected_pose = np.column_stack(
            (
                pycolmap.Rotation3d([q[1], q[2], q[3], q[0]]).matrix(),
                canonical["translation"],
            )
        )
        width, height = frame["width"], frame["height"]
        if (
            type(width) is not int
            or type(height) is not int
            or not 96 <= width <= 4096
            or not 72 <= height <= 4096
            or camera.model_name not in ("PINHOLE", "SIMPLE_PINHOLE")
            or (camera.width, camera.height) != (width, height)
            or image.image_id != frame["backend_image_id"]
            or image.camera_id != frame["backend_camera_id"]
            or canonical["scale"] != 1
            or not np.allclose(pose, expected_pose, rtol=0, atol=1e-8)
            or not np.allclose(intrinsics, frame["intrinsics"], rtol=0, atol=1e-10)
        ):
            raise ValueError("Cached model/image/calibration/pose differs")
        encoded = data["stereo/depth_maps/" + name + ".photometric.bin"]
        header = f"{width}&{height}&1&".encode()
        if (
            not encoded.startswith(header)
            or len(encoded) != len(header) + width * height * 4
        ):
            raise ValueError("Cached private depth grid/payload differs")
        depth = np.frombuffer(encoded, dtype="<f4", offset=len(header)).reshape(
            height, width
        )
        with Image.open(io.BytesIO(data["images/" + name])) as rgb:
            if rgb.format != "PNG" or rgb.mode != "RGB" or rgb.size != (width, height):
                raise ValueError("Cached RGB8 image grid differs")
            gray = np.asarray(rgb.convert("L"), dtype=np.float32) / 255
        views.append(
            {
                "name": name,
                "intrinsics": intrinsics,
                "cam_from_world": image.cam_from_world().matrix(),
                "depth": depth,
                "gray": gray,
            }
        )
    from visibility import validate_views

    validate_views(views)
    return views, rows


def freeze(args, cancelled):
    root = args.workspace.resolve(strict=True)
    output = new_output(args.output, [root, args.manifest, args.recipe])
    documents = {
        name: {
            "sha256": getattr(args, name + "_sha256"),
            "text": read_pinned(
                getattr(args, name), getattr(args, name + "_sha256"), 1024**2
            ).decode(),
        }
        for name in ("manifest", "recipe")
    }
    result_data = read_pinned(
        checked_path(root, "result.json"), args.result_sha256, 1024**2
    )
    result = parse(result_data)
    prepared_data = read_pinned(
        checked_path(root, "prepared.json"), result["prepared_sha256"], 1024**2
    )
    prepared = parse(prepared_data)
    recipe = parse(documents["recipe"]["text"])
    manifest = parse(documents["manifest"]["text"])
    files = {
        "result.json": digest(result_data),
        "prepared.json": digest(prepared_data),
        "stereo/patch-match.cfg": prepared["config_sha256"],
    }
    files.update(
        {"sparse/" + name: sha for name, sha in prepared["sparse_sha256"].items()}
    )
    if (
        not isinstance(recipe["selection_indices"], list)
        or not 9 <= len(recipe["selection_indices"]) <= 18
    ):
        raise ValueError("Cache view budget exceeded")
    for index in recipe["selection_indices"]:
        if cancelled():
            raise InterruptedError("Cache freeze cancelled")
        if type(index) is not int or not 0 <= index < len(manifest["frames"]):
            raise ValueError("Invalid cached manifest index")
        frame = manifest["frames"][index]
        name = Path(frame["image"]).name
        files["images/" + name] = frame["calibrated_image_sha256"]
        key = "stereo/depth_maps/" + name + ".photometric.bin"
        path = checked_path(root, key)
        if path.stat().st_size > MAX_FILE_BYTES:
            raise ValueError("Cached private map exceeds byte budget")
        # These maps had no terminal hash; this explicit receipt pins current
        # bytes, without retroactively asserting that the producer hashed them.
        with path.open("rb") as stream:
            raw = stream.read(MAX_FILE_BYTES + 1)
        if len(raw) > MAX_FILE_BYTES:
            raise ValueError("Cached private map grew beyond byte budget")
        files[key] = digest(raw)
    descriptor = {"schema_version": 1, "kind": KIND, **documents, "files": files}
    load_cache(root, descriptor, cancelled)
    if cancelled():
        raise InterruptedError("Cache freeze cancelled")
    publish(
        output,
        (
            json.dumps(descriptor, sort_keys=True, indent=2, allow_nan=False) + "\n"
        ).encode(),
    )
    return 0


def score(args, cancelled):
    root = args.workspace.resolve(strict=True)
    output = new_output(args.output, [root, args.cache])
    descriptor = parse(read_pinned(args.cache, args.cache_sha256, 4 * 1024**2))
    views, rows = load_cache(root, descriptor, cancelled)
    from visibility import score_sources

    report = score_sources(views, rows, cancelled)
    report.update(
        cache_sha256=args.cache_sha256, implementation_sha256=implementation_digest()
    )
    if cancelled():
        raise InterruptedError("Cache scoring cancelled")
    publish(
        output,
        (json.dumps(report, sort_keys=True, indent=2, allow_nan=False) + "\n").encode(),
    )
    print("Diagnostic report SHA-256 " + digest(output.read_bytes()))
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    for name in ("freeze", "score"):
        command = sub.add_parser(name)
        for option in ("workspace", "output"):
            command.add_argument("--" + option, type=Path, required=True)
        dependencies = ("manifest", "recipe") if name == "freeze" else ("cache",)
        for option in dependencies:
            command.add_argument("--" + option, type=Path, required=True)
            command.add_argument("--" + option + "-sha256", required=True)
        if name == "freeze":
            command.add_argument("--result-sha256", required=True)
    try:
        args = parser.parse_args()
        import socket

        def no_network(*_args, **_kwargs):
            raise RuntimeError("Network is disabled in cached source diagnostics")

        socket.socket = no_network
        cancelled = [False]

        def cancel(_signum, _frame):
            cancelled[0] = True

        signal.signal(signal.SIGINT, cancel)
        signal.signal(signal.SIGTERM, cancel)
        return (freeze if args.command == "freeze" else score)(
            args, lambda: cancelled[0]
        )
    except InterruptedError:
        return 130
    except Exception as error:
        print("Cached source diagnostic failed: " + str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
