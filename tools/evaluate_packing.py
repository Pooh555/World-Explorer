#!/usr/bin/env python3
"""Score a fixed 100-view GPU packing comparison offline; no Q6 gate claim."""

import argparse
import hashlib
import json
import math
from pathlib import Path
import sys
import sysconfig

from audit_worker_environment import bounded_read, publish_receipt
from check_metric_parity import load_cpu_metrics, offline_network, verify_manifest
from evaluate_image_pairs import (
    aggregate,
    digest_string,
    integer,
    psnr_from_mse,
    read_json,
    relative_file,
)

__all__ = [
    "comparison_inputs",
    "main",
    "ppm_bytes",
]


def ppm_bytes(path, digest, width, height):
    data = bounded_read(path)
    header = f"P6\n{width} {height}\n255\n".encode()
    if (
        not data.startswith(header)
        or len(data) != len(header) + width * height * 3
        or hashlib.sha256(data).hexdigest() != digest_string(digest)
    ):
        raise ValueError("Packing readback header, byte count or checksum differs")
    return data[len(header) :]


def comparison_inputs(path):
    report, digest = read_json(path)
    root = path.resolve(strict=True).parent
    if (
        report.get("schema_version") != 1
        or report.get("status") != "complete"
        or report.get("scope") != "local retained-scene GPU packing distortion"
        or report.get("q6_qualified") is not False
        or report.get("ground_truth_available") is not False
        or report.get("geometry_quantization")
        != "identical FP32 geometry bytes in both GPU profiles"
        or report.get("color_domain")
        != "display-encoded RGB8 in [0,1]; full image; no fitted correction"
        or report.get("sh_order")
        != "gsplat real basis; coefficient-major RGB; degree per splat"
    ):
        raise ValueError("Unsupported packing comparison profile or qualification")
    width = integer(report.get("width"), 63, 4096)
    height = integer(report.get("height"), 63, 4096)
    if width * height > 4 * 1024**2:
        raise ValueError("Packing image exceeds pixel budget")
    integer(report.get("scene_splats"), 1, 1000000)
    digest_string(report.get("scene_content_sha256"))
    digest_string(report.get("manifest_sha256"))
    path_report, path_digest = read_json(relative_file(root, "path.json"))
    views = path_report.get("views")
    if (
        path_digest != digest_string(report.get("path_sha256"))
        or path_report.get("schema_version") != 1
        or path_report.get("profile") != "source_view_local_loop_v1"
        or path_report.get("scene_content_sha256") != report["scene_content_sha256"]
        or path_report.get("width") != width
        or path_report.get("height") != height
        or not isinstance(views, list)
        or len(views) != 100
        or any(not isinstance(view, dict) for view in views)
        or [view.get("index") for view in views] != list(range(100))
    ):
        raise ValueError("Frozen 100-view path differs")
    runs = report.get("runs")
    if not isinstance(runs, list) or len(runs) != 2:
        raise ValueError("Both complete GPU profiles are required")
    selected = []
    for run, (profile, stride) in zip(runs, (("fp32", 256), ("fp16_sh", 160))):
        if not isinstance(run, dict):
            raise ValueError("Invalid GPU profile record")
        images = run.get("images")
        if (
            run.get("profile") != profile
            or run.get("splat_stride_bytes") != stride
            or run.get("validation_errors") != 0
            or not isinstance(images, list)
            or len(images) != 100
        ):
            raise ValueError(
                "Packing profile, stride, validation or image count differs"
            )
        files = []
        for index, image in enumerate(images):
            if (
                not isinstance(image, dict)
                or image.get("index") != index
                or image.get("file") != f"{profile}-{index}.ppm"
            ):
                raise ValueError("Incomplete, duplicated or reordered packing views")
            selected_path = relative_file(root, image["file"])
            ppm_bytes(selected_path, image.get("sha256"), width, height)
            files.append((selected_path, image["sha256"]))
        selected.append(files)
    return report, digest, width, height, list(zip(*selected))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--comparison", required=True, type=Path)
    parser.add_argument("--metric-manifest", required=True, type=Path)
    parser.add_argument("--requirements", required=True, type=Path)
    parser.add_argument("--ssim-implementation", required=True, type=Path)
    parser.add_argument("--alexnet-weights", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        evaluator_bytes = bounded_read(Path(__file__))
        root = Path(__file__).resolve().parents[1]
        helpers = {
            name: bounded_read(root / "tools" / name)
            for name in (
                "check_metric_parity.py",
                "evaluate_image_pairs.py",
                "audit_worker_environment.py",
                "audit_metric_environment.py",
            )
        }
        output = args.output.resolve()
        if (
            not output.is_relative_to(root / "artifacts")
            or args.output.exists()
            or args.output.is_symlink()
            or output.parent != args.output.parent.resolve(strict=True)
        ):
            raise ValueError("Select a new receipt in ignored artifacts/")
        report, digest, width, height, pairs = comparison_inputs(args.comparison)
        prefix = Path(sys.prefix).resolve(strict=True)
        sites = {
            Path(sysconfig.get_path(key)).resolve(strict=True)
            for key in ("purelib", "platlib")
        }
        weights = args.alexnet_weights.resolve(strict=True)
        with offline_network():
            metrics, metric_digest = verify_manifest(
                args.metric_manifest,
                prefix,
                sites,
                args.requirements,
                args.ssim_implementation,
                weights,
            )
            torch, lpips, ssim, _ = load_cpu_metrics(
                metrics, sites, args.ssim_implementation, weights
            )
            import numpy as np

            rows = []
            with torch.inference_mode():
                for index, pair in enumerate(pairs):
                    arrays = [
                        np.frombuffer(
                            ppm_bytes(path, sha, width, height), dtype=np.uint8
                        )
                        .reshape(height, width, 3)
                        .copy()
                        for path, sha in pair
                    ]
                    a, b = [
                        torch.from_numpy(array).permute(2, 0, 1)[None].double() / 255
                        for array in arrays
                    ]
                    lpips.reset()
                    perceptual = lpips(a.float(), b.float()).item()
                    similarity = ssim.gaussian_ssim(a, b).item()
                    if not math.isfinite(perceptual) or not math.isfinite(similarity):
                        raise ValueError("Nonfinite packing metric")
                    rows.append(
                        {
                            "scene": report["scene_content_sha256"],
                            "frame": str(index),
                            **psnr_from_mse((a - b).square().mean().item()),
                            "ssim": similarity,
                            "lpips": perceptual,
                            "max_rgb8_error": int(
                                np.abs(arrays[0].astype(np.int16) - arrays[1]).max()
                            ),
                            "image_sha256": [sha for _, sha in pair],
                        }
                    )
                    print(f"Evaluated packing view {index + 1}/100", flush=True)
        if comparison_inputs(args.comparison)[1] != digest:
            raise ValueError("Packing inputs changed during evaluation")
        if (
            verify_manifest(
                args.metric_manifest,
                prefix,
                sites,
                args.requirements,
                args.ssim_implementation,
                weights,
            )[1]
            != metric_digest
        ):
            raise ValueError("Metric identity changed during evaluation")
        if bounded_read(Path(__file__)) != evaluator_bytes:
            raise ValueError("Packing evaluator changed during evaluation")
        if any(
            bounded_read(root / "tools" / name) != data
            for name, data in helpers.items()
        ):
            raise ValueError("Metric helper changed during evaluation")
        result = {
            "schema_version": 1,
            "status": "complete",
            "q6_qualified": False,
            "scope": "pairwise GPU packing distortion; no ground-truth PSNR/LPIPS quality loss",
            "comparison_sha256": digest,
            "metric_manifest_sha256": metric_digest,
            "metric_identity_sha256": metrics["identity_sha256"],
            "evaluator_sha256": hashlib.sha256(evaluator_bytes).hexdigest(),
            "helper_sha256": {
                name: hashlib.sha256(data).hexdigest() for name, data in helpers.items()
            },
            "device": "cpu",
            "threads": 4,
            "network": "DISABLED",
            "excluded_fraction": 0,
            "precision": {"psnr": "float64", "ssim": "float64", "lpips": "float32"},
            "images": rows,
            **aggregate(rows),
        }
        publish_receipt(output, result)
        print(json.dumps(result["equal_weight_scene_mean"]))
        return 0
    except (
        OSError,
        ValueError,
        RuntimeError,
        KeyError,
        TypeError,
        RecursionError,
    ) as error:
        print(f"Packing evaluation failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
