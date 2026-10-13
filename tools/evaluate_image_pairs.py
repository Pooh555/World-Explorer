#!/usr/bin/env python3
"""Evaluate checksum-selected RGB8 PNG pairs offline on CPU; development scope only."""

import argparse
import csv
import hashlib
import io
import json
import math
from pathlib import Path, PurePosixPath
import re
import stat
import struct
import sys
import sysconfig
import zlib

from audit_worker_environment import MAX_METADATA_BYTES, bounded_read, file_digest, publish_receipt
from check_metric_parity import (
    load_cpu_metrics, offline_network, reject_constant, unique_object, verify_manifest,
)

MAX_ENCODED_BYTES = 128 * 1024**2
MAX_SOURCE_PIXELS = 16 * 1024**2
MAX_EVALUATION_PIXELS = 4 * 1024**2
MAX_PAIRS = 120
MAX_TOTAL_ENCODED_BYTES = 2 * 1024**3
SCOPE = "development_temporal_holdout_quantized"
DOMAIN = {"color": "display-encoded RGB [0,1]", "render": "saved RGB8 PNG",
          "target_resize": "Pillow RGB8 bicubic to recorded fit dimensions",
          "mask": "NONE", "observed_region_verified": False,
          "test_exposure_fitting": False, "test_color_correction": False}


def read_json(path):
    data = bounded_read(path)
    value = json.loads(data, object_pairs_hook=unique_object, parse_constant=reject_constant)
    if not isinstance(value, dict):
        raise ValueError("Expected a JSON object")
    return value, hashlib.sha256(data).hexdigest()


def digest_string(value):
    if not isinstance(value, str) or not re.fullmatch(r"[0-9a-f]{64}", value):
        raise ValueError("Expected a SHA-256 digest")
    return value


def integer(value, minimum, maximum):
    if type(value) is not int or not minimum <= value <= maximum:
        raise ValueError("Invalid integer or image bound")
    return value


def dimensions(width, height, maximum=MAX_SOURCE_PIXELS):
    integer(width, 1, 8192)
    integer(height, 1, 8192)
    if width * height > maximum:
        raise ValueError("Image exceeds pixel budget")
    return width, height


def relative_file(root, name):
    if (not isinstance(name, str) or not 1 <= len(name) <= 4096 or
            any(c in name for c in ("\\", ":", "\0"))):
        raise ValueError("Invalid relative image path")
    parts = PurePosixPath(name)
    if parts.is_absolute() or any(p in (".", "..") for p in name.split("/")) or parts.as_posix() != name:
        raise ValueError("Image path must be canonical and relative")
    selected = (root / name).resolve(strict=True)
    if not selected.is_relative_to(root):
        raise ValueError("Image path escapes selected root")
    return selected


def read_image_bytes(path, expected_sha256=None, expected_size=None):
    before = path.stat()
    if not stat.S_ISREG(before.st_mode) or not 33 <= before.st_size <= MAX_ENCODED_BYTES:
        raise ValueError("PNG is not regular or exceeds encoded byte budget")
    with path.open("rb") as stream:
        data = stream.read(MAX_ENCODED_BYTES + 1)
    after = path.stat()
    identity = lambda s: (s.st_dev, s.st_ino, s.st_size, s.st_mtime_ns, s.st_ctime_ns)
    if len(data) != before.st_size or identity(before) != identity(after):
        raise ValueError("PNG changed during read")
    digest = hashlib.sha256(data).hexdigest()
    if expected_sha256 is not None and digest != expected_sha256:
        raise ValueError("PNG checksum differs")
    if expected_size is not None and len(data) != expected_size:
        raise ValueError("PNG byte count differs")
    png_dimensions(data)
    return data, digest


def png_dimensions(data):
    if (len(data) < 33 or data[:8] != b"\x89PNG\r\n\x1a\n" or
            data[8:16] != struct.pack(">I", 13) + b"IHDR" or
            zlib.crc32(data[12:29]) != struct.unpack(">I", data[29:33])[0]):
        raise ValueError("Invalid PNG header or IHDR checksum")
    width, height, depth, color, compression, filtering, interlace = struct.unpack(">IIBBBBB", data[16:29])
    dimensions(width, height)
    if (depth, color, compression, filtering) != (8, 2, 0, 0) or interlace not in (0, 1):
        raise ValueError("Only explicit RGB8 PNG is supported")
    return width, height


def checked_fit(root):
    config, config_sha = read_json(relative_file(root, "input_manifest.json"))
    result, result_sha = read_json(relative_file(root, "result.json"))
    identity = hashlib.sha256(json.dumps(config, sort_keys=True, allow_nan=False).encode()).hexdigest()
    if (config.get("recipe") != "local_gsplat_v1" or config.get("color_domain") != "srgb" or
            config.get("metric_crop") != "five-pixel border for SSIM; full image PSNR" or
            result.get("recipe") != config["recipe"] or result.get("status") != "complete" or
            type(result.get("schema_version")) is not int or result["schema_version"] != 1 or
            result.get("identity") != identity or result.get("source_sha256") != config.get("source_sha256") or
            result.get("steps") != config.get("steps")):
        raise ValueError("Retained fit identity, completion or image domain differs")
    digest_string(config.get("source_sha256"))
    integer(result.get("splats"), 1, 2000000)
    if integer(config.get("steps"), 7000, 30000) not in (7000, 30000):
        raise ValueError("Unsupported retained fit step budget")
    limit = integer(config.get("max_image_dimension"), 63, 4096)
    dataset = config.get("dataset")
    if (not isinstance(dataset, dict) or dataset.get("initial_colors") != "training observations only" or
            dataset.get("resize") != "bicubic; half-pixel coordinates scaled separately in x/y"):
        raise ValueError("Unsupported retained dataset/resize convention")
    frames = dataset.get("frames")
    if not isinstance(frames, list) or not 5 <= len(frames) <= MAX_PAIRS:
        raise ValueError("Invalid retained frame count")
    count = len(frames)
    boundary = count - max(1, (count + 4) // 5)
    expected = {"training_indices": list(range(boundary - 2)),
                "guard_indices": list(range(boundary - 2, boundary)),
                "holdout_indices": list(range(boundary, count))}
    for key, indices in expected.items():
        actual = dataset.get(key)
        if not isinstance(actual, list) or any(type(v) is not int for v in actual) or actual != indices:
            raise ValueError("Temporal training/guard/holdout partition differs")
    names, ids = [], set()
    for frame in frames:
        if not isinstance(frame, dict) or not isinstance(frame.get("name"), str):
            raise ValueError("Invalid retained frame")
        name = frame["name"]
        if not re.fullmatch(r"[A-Za-z0-9_-]{1,128}\.png", name) or name in names:
            raise ValueError("Unsafe or duplicate retained frame name")
        names.append(name)
        ident = integer(frame.get("id"), 1, 2**64 - 1)
        if ident in ids:
            raise ValueError("Duplicate retained frame ID")
        ids.add(ident)
        original = dimensions(frame.get("original_width"), frame.get("original_height"))
        resized = dimensions(frame.get("width"), frame.get("height"), MAX_EVALUATION_PIXELS)
        ratio = min(1.0, limit / max(original))
        if resized != tuple(round(v * ratio) for v in original):
            raise ValueError("Recorded resize differs from fitter convention")
        digest_string(frame.get("sha256"))
    if names != sorted(names):
        raise ValueError("Retained frames are not in temporal filename order")
    holdout = result.get("holdout")
    selected = [frames[i] for i in expected["holdout_indices"]]
    if (not isinstance(holdout, list) or any(not isinstance(v, dict) for v in holdout) or
            [v.get("frame") for v in holdout] != [f["name"] for f in selected]):
        raise ValueError("Completed fit holdout list differs")
    provenance = {"files": {"input_manifest.json": config_sha, "result.json": result_sha},
                  "fit_identity": identity, "source_sha256": config["source_sha256"],
                  "splats": result["splats"], "steps": result["steps"],
                  "training_frames": len(expected["training_indices"]), "guard_frames": 2,
                  "holdout_frames": len(selected),
                  "evaluation_poses": "full-sequence COLMAP; no independent inspection path",
                  "render_domain": "saved clamped/rounded RGB8; original fit metrics used FP32"}
    return selected, provenance


def validate_request(request):
    if (type(request.get("schema_version")) is not int or request["schema_version"] != 1 or
            request.get("scope") != SCOPE or request.get("domain") != DOMAIN):
        raise ValueError("Unsupported evaluation schema, scope or photometric domain")
    digest_string(request.get("metric_manifest_sha256"))
    roots = request.get("roots")
    if not isinstance(roots, dict) or set(roots) != {"render", "target", "fit"}:
        raise ValueError("Explicit fit/render/target roots required")
    resolved = {}
    for name, path in roots.items():
        if not isinstance(path, str) or not path or len(path) > 4096:
            raise ValueError("Invalid evaluation root")
        root = Path(path).resolve(strict=True)
        if not root.is_dir():
            raise ValueError("Evaluation root is not a directory")
        resolved[name] = root
    pairs = request.get("pairs")
    if not isinstance(pairs, list) or not 1 <= len(pairs) <= MAX_PAIRS:
        raise ValueError("Invalid evaluation pair count")
    names, renders, targets, total = set(), set(), set(), 0
    for pair in pairs:
        if not isinstance(pair, dict) or set(pair) != {"scene", "frame", "render", "target", "crop"}:
            raise ValueError("Invalid evaluation pair fields")
        if not isinstance(pair["scene"], str) or not re.fullmatch(r"[A-Za-z0-9_.-]{1,64}", pair["scene"]):
            raise ValueError("Invalid scene ID")
        name = pair["scene"], pair["frame"]
        if not isinstance(pair["frame"], str) or name in names:
            raise ValueError("Duplicate or invalid frame ID")
        names.add(name)
        for role in ("render", "target"):
            image = pair[role]
            if not isinstance(image, dict) or set(image) != {"path", "sha256", "bytes", "width", "height"}:
                raise ValueError("Invalid image descriptor")
            digest_string(image["sha256"])
            total += integer(image["bytes"], 33, MAX_ENCODED_BYTES)
            dimensions(image["width"], image["height"],
                       MAX_EVALUATION_PIXELS if role == "render" else MAX_SOURCE_PIXELS)
            path = relative_file(resolved[role], image["path"])
            selected = renders if role == "render" else targets
            if path in selected:
                raise ValueError("Duplicate image selection")
            selected.add(path)
        crop = pair["crop"]
        if not isinstance(crop, list) or len(crop) != 4:
            raise ValueError("Explicit common rectangular crop required")
        x, y, width, height = (integer(v, 0, 8192) for v in crop)
        if min(width, height) < 63 or x + width > pair["render"]["width"] or y + height > pair["render"]["height"]:
            raise ValueError("Crop lies outside the image or is too small for AlexNet")
    if total > MAX_TOTAL_ENCODED_BYTES or len({p["scene"] for p in pairs}) > 32:
        raise ValueError("Evaluation exceeds total byte or scene budget")
    provenance = request.get("provenance")
    frames, current_provenance = checked_fit(resolved["fit"])
    if provenance != current_provenance:
        raise ValueError("Retained fit provenance changed")
    if (len({p["scene"] for p in pairs}) != 1 or
            [p["frame"] for p in pairs] != [f["name"] for f in frames]):
        raise ValueError("Evaluation must include every retained holdout once in order")
    for pair, frame in zip(pairs, frames):
        if (pair["render"]["path"] != frame["name"] or pair["target"]["path"] != frame["name"] or
                (pair["render"]["width"], pair["render"]["height"]) != (frame["width"], frame["height"]) or
                (pair["target"]["width"], pair["target"]["height"]) != (frame["original_width"], frame["original_height"]) or
                pair["target"]["sha256"] != frame["sha256"]):
            raise ValueError("Pair differs from the retained holdout identity")
    return resolved


def verify_images(request, roots):
    for pair in request["pairs"]:
        for role in ("render", "target"):
            image = pair[role]
            data, _ = read_image_bytes(relative_file(roots[role], image["path"]), image["sha256"], image["bytes"])
            if png_dimensions(data) != (image["width"], image["height"]):
                raise ValueError("PNG dimensions differ from descriptor")


def decode_pair(pair, roots):
    import numpy as np
    from PIL import Image
    arrays, checks = [], {}
    size = pair["render"]["width"], pair["render"]["height"]
    for role in ("render", "target"):
        selected = pair[role]
        data, digest = read_image_bytes(relative_file(roots[role], selected["path"]), selected["sha256"], selected["bytes"])
        with Image.open(io.BytesIO(data)) as image:
            if (image.format != "PNG" or image.mode != "RGB" or image.size != (selected["width"], selected["height"]) or
                    getattr(image, "is_animated", False)):
                raise ValueError("Decoded PNG domain differs from descriptor")
            image.load()
            if role == "target":
                image = image.resize(size, Image.Resampling.BICUBIC)
            array = np.array(image, copy=True)
        x, y, width, height = pair["crop"]
        array = array[y:y + height, x:x + width].copy()
        arrays.append(array)
        checks[role] = {"encoded_sha256": digest, "cropped_rgb8_sha256": hashlib.sha256(array.tobytes()).hexdigest()}
    return arrays, checks


def psnr_from_mse(mse):
    if not math.isfinite(mse) or mse < 0:
        raise ValueError("Nonfinite or negative image MSE")
    return {"mse": mse, "psnr_db": -10 * math.log10(mse) if mse else None,
            "psnr_status": "FINITE" if mse else "IDENTICAL_INFINITE"}


def aggregate(rows):
    if not rows:
        raise ValueError("No image metrics to aggregate")
    scenes = {}
    for row in rows:
        if not all(type(row[k]) in (int, float) and math.isfinite(row[k]) for k in ("mse", "ssim", "lpips")):
            raise ValueError("Nonfinite or missing image metric")
        if row["mse"] < 0:
            raise ValueError("Negative image MSE")
        if row["psnr_status"] not in ("FINITE", "IDENTICAL_INFINITE") or (row["mse"] == 0) != (row["psnr_status"] == "IDENTICAL_INFINITE"):
            raise ValueError("Invalid infinite PSNR representation")
        if row["psnr_status"] == "FINITE" and (row["psnr_db"] is None or not math.isfinite(row["psnr_db"])):
            raise ValueError("Missing finite PSNR")
        scenes.setdefault(row["scene"], []).append(row)
    def means(values):
        infinite = any(v["psnr_db"] is None for v in values)
        return {"psnr_db": None if infinite else math.fsum(v["psnr_db"] for v in values) / len(values),
                "psnr_status": "INFINITE_INCLUDED" if infinite else "FINITE",
                "ssim": math.fsum(v["ssim"] for v in values) / len(values),
                "lpips": math.fsum(v["lpips"] for v in values) / len(values)}
    scene_rows = [{"scene": name, "images": len(values), **means(values)} for name, values in sorted(scenes.items())]
    return {"per_scene": scene_rows, "equal_weight_scene_mean": means(scene_rows)}


def evaluate(request, roots, metric_report, sites, ssim, weights):
    torch, lpips, ssim_module, _ = load_cpu_metrics(metric_report, sites, ssim, weights)
    rows = []
    with torch.inference_mode():
        for pair in request["pairs"]:
            (left, right), checks = decode_pair(pair, roots)
            a = torch.from_numpy(left).permute(2, 0, 1)[None].double() / 255
            b = torch.from_numpy(right).permute(2, 0, 1)[None].double() / 255
            values = psnr_from_mse((a - b).square().mean().item())
            similarity = ssim_module.gaussian_ssim(a, b).item()
            lpips.reset()
            perceptual = lpips(a.float(), b.float()).item()
            if not math.isfinite(similarity) or not math.isfinite(perceptual):
                raise ValueError("Nonfinite SSIM or LPIPS")
            area = pair["render"]["width"] * pair["render"]["height"]
            rows.append({"scene": pair["scene"], "frame": pair["frame"], **values,
                         "ssim": similarity, "lpips": perceptual, "checks": checks,
                         "crop": pair["crop"], "excluded_fraction": 1 - pair["crop"][2] * pair["crop"][3] / area,
                         "ssim_valid_window_pixels": (pair["crop"][2] - 10) * (pair["crop"][3] - 10)})
            print("Evaluated " + pair["scene"] + "/" + pair["frame"], flush=True)
    return {"images": rows, **aggregate(rows)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--request", required=True, type=Path)
    parser.add_argument("--metric-manifest", required=True, type=Path)
    parser.add_argument("--requirements", required=True, type=Path)
    parser.add_argument("--ssim-implementation", required=True, type=Path)
    parser.add_argument("--alexnet-weights", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    try:
        output = args.output.resolve()
        if not output.is_relative_to(root / "artifacts") or args.output.exists() or args.output.is_symlink():
            raise ValueError("Select a new evaluation receipt in ignored artifacts/")
        request, request_digest = read_json(args.request)
        roots = validate_request(request)
        # Validate every encoded pair before loading models or computing scores.
        verify_images(request, roots)
        prefix = Path(sys.prefix).resolve(strict=True)
        sites = {Path(sysconfig.get_path(key)).resolve(strict=True) for key in ("purelib", "platlib")}
        weights = args.alexnet_weights.resolve(strict=True)
        with offline_network():
            metric_report, manifest_digest = verify_manifest(args.metric_manifest, prefix, sites, args.requirements,
                                                             args.ssim_implementation, weights)
            if manifest_digest != request["metric_manifest_sha256"]:
                raise ValueError("Metric manifest differs from evaluation request")
            result = evaluate(request, roots, metric_report, sites, args.ssim_implementation, weights)
        if read_json(args.request)[1] != request_digest:
            raise ValueError("Evaluation request changed during scoring")
        validate_request(request)
        verify_images(request, roots)
        result.update(schema_version=1, scope=SCOPE, domain=DOMAIN, status="complete",
                      request_sha256=request_digest, metric_manifest_sha256=manifest_digest,
                      metric_identity_sha256=metric_report["identity_sha256"],
                      evaluator_sha256=file_digest(Path(__file__), MAX_METADATA_BYTES)[0],
                      metric_loader_sha256=file_digest(root / "tools/check_metric_parity.py", MAX_METADATA_BYTES)[0],
                      device="cpu", threads=4, precision={"psnr": "float64", "ssim": "float64", "lpips": "float32"},
                      provenance=request["provenance"], network="DISABLED", q2_qualified=False, q3_qualified=False,
                      metric_qualified=False, failed_images=[], complete_image_count=len(request["pairs"]))
        publish_receipt(output, result)
        print(json.dumps({"receipt": str(output.relative_to(root)), "images": len(request["pairs"]),
                          "mean": result["equal_weight_scene_mean"], "q3_qualified": False}))
        return 0
    except (OSError, ValueError, RuntimeError, csv.Error, RecursionError) as error:
        print(f"Image-pair evaluation failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
