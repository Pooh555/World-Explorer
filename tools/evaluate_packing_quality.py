#!/usr/bin/env python3
"""Pin and score both GPU storage profiles against retained calibrated RGB targets.

Development evidence only: all available cameras, including training/guard views,
are retained. This is neither an independent Q3 path nor Q6's 100-view corpus.
"""

import argparse
import hashlib
import io
import json
import math
from pathlib import Path
import sys
import sysconfig

from audit_worker_environment import MAX_METADATA_BYTES, file_digest, publish_receipt
from check_metric_parity import load_cpu_metrics, offline_network, verify_manifest
from evaluate_image_pairs import (
    aggregate, checked_fit, digest_string, integer, png_dimensions, psnr_from_mse,
    read_image_bytes, read_json, relative_file,
)
from evaluate_packing import ppm_bytes

SCOPE = "development calibrated GPU packing quality against captured targets"
DOMAIN = {"color": "display-encoded RGB [0,1]", "render": "native RGB8 PPM",
          "target_resize": "Pillow RGB8 bicubic to recorded fit dimensions",
          "mask": "NONE", "observed_region_verified": False,
          "test_exposure_fitting": False, "test_color_correction": False}
HELPERS = ("evaluate_packing_quality.py", "evaluate_packing.py", "evaluate_image_pairs.py",
           "check_metric_parity.py", "audit_metric_environment.py", "audit_worker_environment.py")


def require(valid, message):
    if not valid:
        raise ValueError(message)


def camera_matches(view, frame):
    """Independently invert the saved affine rows (not their approximate transpose)."""
    matrix, intrinsic = frame["view"], frame["intrinsic"]
    require(isinstance(matrix, list) and len(matrix) == 4 and
            all(isinstance(row, list) and len(row) == 4 for row in matrix) and
            isinstance(intrinsic, list) and len(intrinsic) == 3 and
            all(isinstance(row, list) and len(row) == 3 for row in intrinsic), "Invalid camera matrices")
    require(all(type(v) in (int, float) and math.isfinite(v)
                for row in matrix + intrinsic for v in row) and
            matrix[3] == [0, 0, 0, 1] and intrinsic[2] == [0, 0, 1] and
            intrinsic[0][1] == intrinsic[1][0] == 0 and
            intrinsic[0][0] > 0 and intrinsic[1][1] > 0, "Unsupported calibrated camera")
    basis = [[row[0], -row[1], -row[2]] for row in matrix[:3]]
    def cross(a, b):
        return [a[1]*b[2] - a[2]*b[1], a[2]*b[0] - a[0]*b[2], a[0]*b[1] - a[1]*b[0]]
    cofactors = [cross(basis[1], basis[2]), cross(basis[2], basis[0]), cross(basis[0], basis[1])]
    determinant = sum(a*b for a, b in zip(basis[0], cofactors[0]))
    require(abs(determinant - 1) < 1e-4, "Invalid camera rotation")
    position = [-sum(cofactors[j][i] * matrix[j][3] for j in range(3)) / determinant for i in range(3)]
    expected = {"right": basis[0], "down": basis[1], "forward": basis[2], "position": position,
                "intrinsics": [intrinsic[0][0], intrinsic[1][1], intrinsic[0][2] - .5, intrinsic[1][2] - .5]}
    for key, values in expected.items():
        actual = view.get(key)
        require(isinstance(actual, list) and len(actual) == len(values) and
                all(type(a) in (int, float) and math.isfinite(a) and abs(a-b) <= 1e-9
                    for a, b in zip(actual, values)), "Native camera differs from retained calibration")
    require(view.get("near") == 0.009999999776482582 and view.get("far") == 100000,
            "Camera clipping differs from frozen diagnostic")


def prepare(roots, metric_manifest, crop_margin):
    require(isinstance(roots, dict) and set(roots) == {"fit", "scene", "target", "fp32", "fp16_sh"},
            "Explicit input roots required")
    roots = {name: Path(path).resolve(strict=True) for name, path in roots.items()}
    require(all(path.is_dir() for path in roots.values()), "Input root is not a directory")
    require(roots["fp32"] != roots["fp16_sh"], "Separate native profile exports required")
    integer(crop_margin, 0, 8192)
    metric, metric_sha = read_json(metric_manifest)
    require(metric.get("schema_version") == 1 and metric.get("identity_ready") is True,
            "Complete metric receipt required")
    _, provenance = checked_fit(roots["fit"])
    config, _ = read_json(relative_file(roots["fit"], "input_manifest.json"))
    result, _ = read_json(relative_file(roots["fit"], "result.json"))
    frames = config["dataset"]["frames"]
    require(config.get("sh_degree") == 3 and config.get("rasterize_mode") == "classic" and
            config["dataset"].get("world_transform") == "native COLMAP; canonical chunk applies Rx(pi)",
            "Unsupported fit camera/SH profile")
    require(file_digest(relative_file(roots["fit"], result["ply"]), 512 * 1024**2)[0] ==
            digest_string(result["ply_sha256"]), "Fitted asset changed")
    manifest, manifest_sha = read_json(relative_file(roots["scene"], "manifest.json"))
    scene_receipt, provenance_sha = read_json(relative_file(roots["scene"], manifest["provenance"]))
    scene_provenance = scene_receipt.get("metadata", {})
    require(isinstance(scene_provenance, dict), "Invalid scene provenance")
    require(file_digest(relative_file(roots["scene"], manifest["index"]), MAX_METADATA_BYTES)[0] ==
            digest_string(manifest["index_sha256"]), "Scene index changed")
    require(provenance_sha == manifest["provenance_sha256"] and
            scene_provenance.get("source_kind") == "video_reconstruction" and
            scene_provenance.get("fit_identity") == provenance["fit_identity"] and
            scene_provenance.get("input_sha256") == result["ply_sha256"] and
            scene_provenance.get("capture_sha256") == provenance["source_sha256"],
            "Scene and retained fit differ")
    identity = {"fit_identity": provenance["fit_identity"], "source_sha256": provenance["source_sha256"],
                "ply_sha256": result["ply_sha256"], "scene_manifest_sha256": manifest_sha,
                "fit_input_sha256": provenance["files"]["input_manifest.json"],
                "fit_result_sha256": provenance["files"]["result.json"]}
    exports, fixtures = {}, []
    for profile, stride in (("fp32", 256), ("fp16_sh", 160)):
        root = roots[profile]
        fixture, fixture_sha = read_json(relative_file(root, "fixture.json"))
        native, native_sha = read_json(relative_file(root, "native.json"))
        require(type(fixture.get("schema")) is int and fixture.get("schema") == 2 and fixture.get("label") ==
                "real calibrated retained fit; no quality/performance certification" and
                fixture.get("identity") == identity and fixture.get("gpu_storage") == profile and
                fixture.get("splat_stride_bytes") == stride and
                fixture.get("splat_count") == provenance["splats"] and
                fixture.get("row_format") == "little-endian float32[60]: mean3,scale3,wxyz4,opacity,sh48,degree" and
                native.get("fixture_sha256") == fixture_sha and type(native.get("validation_errors")) is int and
                native.get("validation_errors") == 0 and
                native.get("splat_count") == provenance["splats"] and native.get("fp32_compositing") is True,
                "Native profile, validation, fit identity or full count differs")
        digest_string(fixture.get("scene_content_sha256"))
        for name, key in (("fit-input.json", "fit_input_sha256"), ("fit-result.json", "fit_result_sha256")):
            require(read_json(relative_file(root, name))[1] == identity[key], "Exported fit metadata changed")
        indices = fixture.get("fit_frame_indices")
        require(isinstance(indices, list) and all(type(i) is int for i in indices) and
                indices == list(range(len(frames))), "Every available camera is required once in original order")
        views, images = fixture.get("views"), native.get("images")
        require(isinstance(views, list) and isinstance(images, list) and len(views) == len(images) == len(frames),
                "Missing calibrated views or readbacks")
        for index, (view, image, frame) in enumerate(zip(views, images, frames)):
            require(isinstance(view, dict) and isinstance(image, dict) and
                    view.get("name") == f"fit_index_{index}" and view.get("fit_frame_index") == index and
                    type(view.get("fit_frame_index")) is int and
                    view.get("fit_frame_name") == frame["name"] and
                    image.get("file") == f"fit_index_{index}.ppm" and
                    (fixture.get("width"), fixture.get("height")) == (frame["width"], frame["height"]),
                    "View order, identity or dimensions differ")
            camera_matches(view, frame)
            ppm_bytes(relative_file(root, image["file"]), image.get("sha256"), frame["width"], frame["height"])
        require(fixture.get("render_origin") == views[0]["position"], "Render origin differs")
        chunks = fixture.get("chunks")
        require(isinstance(chunks, list) and 1 <= len(chunks) <= 1024, "Invalid chunk list")
        count, files = 0, set()
        for chunk in chunks:
            size = integer(chunk["count"], 1, provenance["splats"] - count)
            selected = relative_file(root, chunk["file"])
            require(selected not in files and file_digest(selected, 240 * size) ==
                    (digest_string(chunk["sha256"]), 240 * size), "Chunk bytes, checksum or uniqueness differs")
            files.add(selected)
            count += size
        require(count == provenance["splats"], "Full scene payload required")
        exports[profile] = {"fixture_sha256": fixture_sha, "native_sha256": native_sha, "images": images}
        fixtures.append({k: v for k, v in fixture.items() if k not in ("gpu_storage", "splat_stride_bytes")})
    require(fixtures[0] == fixtures[1], "Profiles must render identical scene, cameras, clipping and background")
    pairs, total = [], 0
    for index, frame in enumerate(frames):
        data, sha = read_image_bytes(relative_file(roots["target"], frame["name"]), frame["sha256"])
        require(png_dimensions(data) == (frame["original_width"], frame["original_height"]), "Target size differs")
        total += len(data) + frame["width"] * frame["height"] * 6
        width, height = frame["width"] - 2 * crop_margin, frame["height"] - 2 * crop_margin
        require(min(width, height) >= 63, "Common crop is too small for AlexNet")
        roles = [role for role in ("training", "guard", "holdout") if index in config["dataset"][role + "_indices"]]
        require(len(roles) == 1, "Camera split is ambiguous")
        pairs.append({"frame": frame["name"], "index": index, "split": roles[0],
                      "target_sha256": sha, "target_bytes": len(data),
                      "crop": [crop_margin, crop_margin, width, height]})
    require(total <= 2 * 1024**3, "Evaluation exceeds total encoded byte budget")
    return {"schema_version": 1, "scope": SCOPE, "domain": DOMAIN, "q6_qualified": False,
            "fixed_100_view_corpus": False, "available_camera_count": len(frames), "pairs": pairs,
            "crop_margin": crop_margin, "roots": {name: str(path) for name, path in roots.items()},
            "metric_manifest": str(metric_manifest.resolve(strict=True)), "metric_manifest_sha256": metric_sha,
            "identity": identity, "scene_content_sha256": fixtures[0]["scene_content_sha256"],
            "provenance": provenance, "exports": exports,
            "implementation_sha256": {name: file_digest(Path(__file__).parent / name, MAX_METADATA_BYTES)[0]
                                      for name in HELPERS}}


def validate(request):
    require(type(request.get("schema_version")) is int and request.get("q6_qualified") is False and
            request.get("fixed_100_view_corpus") is False, "Invalid schema or qualification claim")
    require(request == prepare(request["roots"], Path(request["metric_manifest"]), request["crop_margin"]),
            "Request, native exports, targets, fit or evaluation implementation changed")


def quality_delta(baseline, packed):
    # Infinity - infinity has no defined loss. Preserve that case explicitly;
    # never turn it into a numeric zero or a passing comparison.
    finite = baseline["psnr_db"] is not None and packed["psnr_db"] is not None
    loss = baseline["psnr_db"] - packed["psnr_db"] if finite else None
    increase = packed["lpips"] - baseline["lpips"]
    require(math.isfinite(increase) and (loss is None or math.isfinite(loss)), "Nonfinite quality delta")
    return {"psnr_loss_db": loss, "psnr_loss_status": "FINITE" if finite else "UNDEFINED_INFINITE",
            "lpips_increase": increase,
            "numeric_limits_met": loss <= .2 and increase <= .01 if finite else None}


def evaluate(request, metrics, sites, ssim_path, weights):
    import numpy as np
    from PIL import Image
    torch, lpips, ssim, _ = load_cpu_metrics(metrics, sites, ssim_path, weights)
    root = {name: Path(path) for name, path in request["roots"].items()}
    config, _ = read_json(root["fit"] / "input_manifest.json")
    rows = []
    with torch.inference_mode():
        for pair, frame in zip(request["pairs"], config["dataset"]["frames"]):
            data, _ = read_image_bytes(relative_file(root["target"], pair["frame"]),
                                       pair["target_sha256"], pair["target_bytes"])
            with Image.open(io.BytesIO(data)) as image:
                require(image.format == "PNG" and image.mode == "RGB" and
                        image.size == (frame["original_width"], frame["original_height"]) and
                        not getattr(image, "is_animated", False), "Decoded target domain differs")
                target = np.array(image.resize((frame["width"], frame["height"]), Image.Resampling.BICUBIC))
            x, y, width, height = pair["crop"]
            target = target[y:y+height, x:x+width].copy()
            tensor = lambda a: torch.from_numpy(a).permute(2, 0, 1)[None].double() / 255
            b = tensor(target)
            scores = {}
            for profile in ("fp32", "fp16_sh"):
                descriptor = request["exports"][profile]["images"][pair["index"]]
                pixels = ppm_bytes(relative_file(root[profile], descriptor["file"]), descriptor["sha256"],
                                   frame["width"], frame["height"])
                array = np.frombuffer(pixels, dtype=np.uint8).reshape(frame["height"], frame["width"], 3)
                array = array[y:y+height, x:x+width].copy()
                a = tensor(array)
                lpips.reset()
                scores[profile] = {"scene": request["scene_content_sha256"], "frame": pair["frame"],
                                   **psnr_from_mse((a-b).square().mean().item()),
                                   "ssim": ssim.gaussian_ssim(a, b).item(), "lpips": lpips(a.float(), b.float()).item(),
                                   "cropped_rgb8_sha256": hashlib.sha256(array.tobytes()).hexdigest()}
            rows.append({**pair, "target_cropped_rgb8_sha256": hashlib.sha256(target.tobytes()).hexdigest(),
                         "excluded_fraction": 1 - width * height / (frame["width"] * frame["height"]),
                         "scores": scores, **quality_delta(scores["fp32"], scores["fp16_sh"])})
            print(f"Evaluated captured packing camera {pair['index'] + 1}/{len(request['pairs'])}", flush=True)
    summaries = {}
    for split in ("all", "training", "guard", "holdout"):
        selected = [row for row in rows if split == "all" or row["split"] == split]
        profiles = {profile: aggregate([row["scores"][profile] for row in selected])
                    for profile in ("fp32", "fp16_sh")}
        summaries[split] = {"images": len(selected), "profiles": profiles,
                            **quality_delta(*(profiles[p]["equal_weight_scene_mean"] for p in ("fp32", "fp16_sh")))}
    return {"images": rows, "summaries": summaries}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    pin = commands.add_parser("prepare")
    for name in ("fit", "scene", "target", "fp32", "fp16-sh", "metric-manifest"):
        pin.add_argument("--" + name, required=True, type=Path)
    pin.add_argument("--crop-margin", required=True, type=int)
    score = commands.add_parser("evaluate")
    score.add_argument("--request", required=True, type=Path)
    for name in ("requirements", "ssim-implementation", "alexnet-weights"):
        score.add_argument("--" + name, required=True, type=Path)
    for command in (pin, score):
        command.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    try:
        output = args.output.resolve()
        require(output.is_relative_to(root / "artifacts") and not args.output.exists() and
                not args.output.is_symlink() and output.parent == args.output.parent.resolve(strict=True),
                "Select a new output in ignored artifacts/")
        if args.command == "prepare":
            request = prepare({name: getattr(args, name) for name in ("fit", "scene", "target", "fp32", "fp16_sh")},
                              args.metric_manifest, args.crop_margin)
            publish_receipt(output, request)
            print(f"Pinned all {request['available_camera_count']} available calibrated cameras; Q6 unqualified")
            return 0
        request, request_sha = read_json(args.request)
        validate(request)
        prefix = Path(sys.prefix).resolve(strict=True)
        sites = {Path(sysconfig.get_path(key)).resolve(strict=True) for key in ("purelib", "platlib")}
        weights = args.alexnet_weights.resolve(strict=True)
        with offline_network():
            metrics, metric_sha = verify_manifest(Path(request["metric_manifest"]), prefix, sites,
                                                  args.requirements, args.ssim_implementation, weights)
            require(metric_sha == request["metric_manifest_sha256"], "Metric manifest changed")
            result = evaluate(request, metrics, sites, args.ssim_implementation, weights)
        require(read_json(args.request)[1] == request_sha, "Request changed during scoring")
        validate(request)
        require(verify_manifest(Path(request["metric_manifest"]), prefix, sites, args.requirements,
                                args.ssim_implementation, weights)[1] == metric_sha, "Metric identity changed")
        result.update(schema_version=1, status="complete", scope=SCOPE, domain=DOMAIN, q6_qualified=False,
                      fixed_100_view_corpus=False, request_sha256=request_sha, metric_manifest_sha256=metric_sha,
                      metric_identity_sha256=metrics["identity_sha256"], implementation_sha256=request["implementation_sha256"],
                      identity=request["identity"], available_camera_count=request["available_camera_count"],
                      device="cpu", threads=4, network="DISABLED",
                      precision={"psnr": "float64", "ssim": "float64", "lpips": "float32"})
        publish_receipt(output, result)
        print(json.dumps(result["summaries"]["all"]))
        return 0
    except (OSError, ValueError, RuntimeError, KeyError, TypeError, RecursionError) as error:
        print(f"Packing quality failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
