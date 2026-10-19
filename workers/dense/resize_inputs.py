#!/usr/bin/env python3
"""Prepare an explicit resized dense-input profile; no solver or depth production.

The native manifest must first pass application admission. The application must
independently admit the resized output before a depth worker consumes it.
"""

import argparse
import copy
import io
import json
from pathlib import Path
import signal
import sys

from pilot import digest, parse, publish, read_pinned

__all__ = [
    "PROFILE",
    "main",
    "run",
    "transformed",
    "validate_resize",
    "worker_digest",
]

PROFILE = "colmap_resized_dense_input_v1"


def worker_digest():
    directory = Path(__file__).parent
    files = {
        name: digest((directory / name).read_bytes())
        for name in ("resize_inputs.py", "pilot.py", "colmap_io.py")
    }
    return digest(json.dumps(files, sort_keys=True, separators=(",", ":")).encode())


def validate_resize(recipe):
    expected = {
        "recipe",
        "crop",
        "resize",
        "width",
        "height",
        "pixel_centers",
        "image_encoding",
        "pillow_version",
        "worker_sha256",
        "environment_sha256",
    }
    if (
        set(recipe) != expected
        or recipe["recipe"] != PROFILE
        or recipe["crop"] != "none"
        or recipe["resize"] != "PILLOW_BILINEAR"
        or recipe["pixel_centers"] != "integer"
        or recipe["image_encoding"] != "PNG_RGB8"
        or recipe["pillow_version"] != "11.3.0"
    ):
        raise ValueError("Unsupported explicit resize profile")
    for axis in ("width", "height"):
        if type(recipe[axis]) is not int or not 11 <= recipe[axis] <= 4096:
            raise ValueError("Resize grid exceeds profile")
    for key in ("worker_sha256", "environment_sha256"):
        value = recipe[key]
        if (
            not isinstance(value, str)
            or len(value) != 64
            or any(c not in "0123456789abcdef" for c in value)
        ):
            raise ValueError("Invalid resize implementation identity")
    return recipe


def transformed(native, native_sha, recipe, recipe_sha):
    resize = validate_resize(recipe["resize"])
    if (
        recipe["schema_version"] != 1
        or recipe["input_profile"] != PROFILE
        or recipe["depth_producer"] != "MULTIVIEW_STEREO"
        or native["schema_version"] != 1
        or native["kind"] != "DENSE_INPUTS_NATIVE_GRID"
        or native["preparation_recipe"]["resize"] != "none"
        or not 1 <= len(native["frames"]) <= 120
    ):
        raise ValueError("Unsupported native inputs or depth recipe")
    result = copy.deepcopy(native)
    result.update(
        kind="DENSE_INPUTS_RESIZED_GRID",
        native_manifest_sha256=native_sha,
        preparation_recipe=resize,
        preparation_recipe_sha256=digest(
            json.dumps(resize, sort_keys=True, separators=(",", ":")).encode()
        ),
        depth_recipe_sha256=recipe_sha,
    )
    names = []
    for frame in result["frames"]:
        name = frame["image"]
        if (
            not isinstance(name, str)
            or not name.startswith("training/images/")
            or len(Path(name).parts) != 3
            or "\\" in name
            or ":" in name
            or not Path(name).name.endswith(".png")
        ):
            raise ValueError("Unsafe calibrated image path")
        names.append(name)
        w, h = frame["width"], frame["height"]
        if (
            type(w) is not int
            or type(h) is not int
            or not 11 <= w <= 8192
            or not 11 <= h <= 8192
            or w * h > 4096**2
            or resize["width"] > w
            or resize["height"] > h
            or (resize["width"], resize["height"]) == (w, h)
        ):
            raise ValueError("Resize must explicitly downsample the native grid")
        sx, sy = resize["width"] / w, resize["height"] / h
        fx, fy, cx, cy = frame["intrinsics"]
        frame.update(
            width=resize["width"],
            height=resize["height"],
            intrinsics=[sx * fx, sy * fy, sx * (cx + 0.5) - 0.5, sy * (cy + 0.5) - 0.5],
            native_image_sha256=frame["calibrated_image_sha256"],
        )
    if names != sorted(set(names)):
        raise ValueError("Duplicate or unordered native image inventory")
    return result


def run(args, cancelled=lambda: False):
    recipe = parse(read_pinned(args.recipe, args.recipe_sha256, 1024**2))
    native = parse(read_pinned(args.manifest, args.manifest_sha256, 1024**2))
    manifest = transformed(native, args.manifest_sha256, recipe, args.recipe_sha256)
    resize = recipe["resize"]
    directory = Path(__file__).parent
    if resize["worker_sha256"] != worker_digest() or resize[
        "environment_sha256"
    ] != digest((directory / "environment.txt").read_bytes()):
        raise ValueError("Selected resize implementation/environment differs")
    import PIL
    from PIL import Image

    if PIL.__version__ != resize["pillow_version"]:
        raise ValueError("Pinned Pillow required")
    # No backend imports, package acquisition or network operations.
    stage = args.stage.resolve(strict=True)
    read_pinned(
        stage / "completion.json", native["pose_completion_sha256"], 16 * 1024**2
    )
    if args.output.is_symlink():
        raise ValueError("Resize output cannot be a symlink")
    output = args.output.resolve(strict=False)
    for path in (
        stage,
        args.manifest.resolve(strict=True),
        args.recipe.resolve(strict=True),
    ):
        if output == path or output.is_relative_to(path) or path.is_relative_to(output):
            raise ValueError("Resize output overlaps an input")
    if output.exists():
        raise ValueError("Resize output must be new")
    if cancelled():
        return 130
    output.mkdir()
    (output / "training/images").mkdir(parents=True)
    for original, frame in zip(native["frames"], manifest["frames"]):
        if cancelled():
            return 130
        path = stage / original["image"]
        if path.resolve(strict=True).relative_to(stage).as_posix() != original["image"]:
            raise ValueError("Native image path uses a symlink")
        data = read_pinned(path, original["calibrated_image_sha256"], 64 * 1024**2)
        with Image.open(io.BytesIO(data)) as image:
            if (
                image.format != "PNG"
                or image.mode != "RGB"
                or image.size != (original["width"], original["height"])
            ):
                raise ValueError("Native RGB8 PNG grid differs")
            image.load()
            resized = image.resize(
                (frame["width"], frame["height"]), Image.Resampling.BILINEAR
            )
            stream = io.BytesIO()
            resized.save(stream, format="PNG", compress_level=6)
        encoded = stream.getvalue()
        if len(encoded) > 64 * 1024**2:
            raise ValueError("Resized image exceeds encoded budget")
        if cancelled():
            return 130
        publish(output / frame["image"], encoded)
        frame["calibrated_image_sha256"] = digest(encoded)
    if cancelled():
        return 130
    # Manifest is the commit marker; partial images alone are never complete.
    publish(
        output / "manifest.json",
        (
            json.dumps(manifest, sort_keys=True, indent=2, allow_nan=False) + "\n"
        ).encode(),
    )
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("manifest", "stage", "recipe", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    for name in ("manifest-sha256", "recipe-sha256"):
        parser.add_argument("--" + name, required=True)
    args = parser.parse_args()
    cancelled = False

    def cancel(_signum, _frame):
        nonlocal cancelled
        cancelled = True

    signal.signal(signal.SIGINT, cancel)
    signal.signal(signal.SIGTERM, cancel)
    try:
        return run(args, lambda: cancelled)
    except Exception as error:
        print("Dense resize failed: " + str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
