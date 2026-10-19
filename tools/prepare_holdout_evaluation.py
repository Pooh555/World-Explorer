#!/usr/bin/env python3
"""Pin all retained development holdout PNG pairs; never reconstruct or change a fit."""

import argparse
from pathlib import Path
import re
import sys

from audit_worker_environment import MAX_METADATA_BYTES, file_digest, publish_receipt
from evaluate_image_pairs import (
    DOMAIN,
    SCOPE,
    checked_fit,
    integer,
    png_dimensions,
    read_image_bytes,
    read_json,
    relative_file,
    validate_request,
    verify_images,
)


__all__ = [
    "main",
    "prepare",
]


def prepare(fit, images, metric_manifest, scene, crop_margin):
    fit, images = fit.resolve(strict=True), images.resolve(strict=True)
    renders = (fit / "references").resolve(strict=True)
    if not renders.is_relative_to(fit) or not images.is_dir():
        raise ValueError("Invalid selected input roots")
    if not isinstance(scene, str) or not re.fullmatch(r"[A-Za-z0-9_.-]{1,64}", scene):
        raise ValueError("Invalid scene ID")
    integer(crop_margin, 0, 8192)
    metric, metric_sha = read_json(metric_manifest)
    if metric.get("schema_version") != 1 or metric.get("identity_ready") is not True:
        raise ValueError("Select a complete metric identity receipt")
    frames, provenance = checked_fit(fit)
    pairs = []
    for frame in frames:
        pair = {"scene": scene, "frame": frame["name"]}
        for role, root in (("render", renders), ("target", images)):
            data, digest = read_image_bytes(
                relative_file(root, frame["name"]),
                frame["sha256"] if role == "target" else None,
            )
            width, height = png_dimensions(data)
            pair[role] = {
                "path": frame["name"],
                "sha256": digest,
                "bytes": len(data),
                "width": width,
                "height": height,
            }
        pair["crop"] = [
            crop_margin,
            crop_margin,
            frame["width"] - 2 * crop_margin,
            frame["height"] - 2 * crop_margin,
        ]
        pairs.append(pair)
    request = {
        "schema_version": 1,
        "scope": SCOPE,
        "domain": DOMAIN,
        "metric_manifest_sha256": metric_sha,
        "roots": {"fit": str(fit), "target": str(images), "render": str(renders)},
        "provenance": provenance,
        "pairs": pairs,
    }
    roots = validate_request(request)
    verify_images(request, roots)
    return request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fit", required=True, type=Path)
    parser.add_argument("--images", required=True, type=Path)
    parser.add_argument("--metric-manifest", required=True, type=Path)
    parser.add_argument("--scene", required=True)
    parser.add_argument(
        "--crop-margin",
        required=True,
        type=int,
        help="Predetermined common rectangular margin; 0 means full bitmap",
    )
    parser.add_argument(
        "--mask",
        required=True,
        choices=("none",),
        help="Arbitrary masks are unsupported; no observed-region claim",
    )
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    try:
        output = args.output.resolve()
        if (
            not output.is_relative_to(root / "artifacts")
            or args.output.exists()
            or args.output.is_symlink()
        ):
            raise ValueError("Choose a new evaluation request in ignored artifacts/")
        request = prepare(
            args.fit, args.images, args.metric_manifest, args.scene, args.crop_margin
        )
        request["preparer_sha256"] = file_digest(Path(__file__), MAX_METADATA_BYTES)[0]
        publish_receipt(output, request)
        print(
            f"Pinned {len(request['pairs'])} retained holdout pairs; development scope only"
        )
        return 0
    except (OSError, ValueError, RecursionError) as error:
        print(f"Holdout selection failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
