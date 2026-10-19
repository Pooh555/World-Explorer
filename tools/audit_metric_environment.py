#!/usr/bin/env python3
"""Freeze local metric identities offline; a receipt never certifies evaluation.

No backend import, checkpoint loading, cache search, installation or network I/O.
Run with the selected evaluation virtual environment's Python and exact pins.
"""

import argparse
import csv
import hashlib
import json
from pathlib import Path
import sys
import sysconfig

from audit_worker_environment import (
    MAX_METADATA_BYTES,
    audit_environment,
    canonical_bytes,
    contained,
    file_digest,
    publish_receipt,
    read_requirements,
)

POLICY = {
    "specification": "technical_plan.md section 3.3; Q2/Q3/Q6",
    "color_domain": "display-encoded RGB [0,1]",
    "channels": "RGB",
    "psnr": {
        "data_range": 1,
        "aggregation": "per-image then equal-weight per-scene means",
    },
    "ssim": {
        "window": 11,
        "sigma": 1.5,
        "data_range": 1,
        "k1": 0.01,
        "k2": 0.03,
        "window_reduction": "valid-window mean",
    },
    "lpips": {
        "implementation": "TorchMetrics",
        "net_type": "alex",
        "normalize": True,
        "reduction": "none",
        "resize": None,
    },
    "evaluation_region": "predetermined common valid crop/mask; report excluded area",
    "resolution": "declared by the dataset; no implicit resize",
    "test_exposure_fitting": False,
    "test_color_correction": False,
}

DEPENDENCIES = (
    "torchmetrics",
    "torch",
    "torchvision",
    "numpy",
    "packaging",
    "lightning-utilities",
    "typing-extensions",
)
IMPLEMENTATION_FILES = {
    "torchmetrics": (
        "torchmetrics/functional/image/lpips.py",
        "torchmetrics/image/lpip.py",
        "torchmetrics/metric.py",
        "torchmetrics/utilities/imports.py",
        "torchmetrics/functional/image/lpips_models/alex.pth",
    ),
    "torchvision": (
        "torchvision/models/alexnet.py",
        "torchvision/models/_api.py",
        "torchvision/_internally_replaced_utils.py",
    ),
    "torch": ("torch/hub.py", "torch/serialization.py"),
}


def metric_manifest(
    prefix, sites, requirements, ssim_implementation, alexnet_weights=None
):
    prefix = prefix.resolve(strict=True)
    sites = sorted({contained(site, prefix) for site in sites})
    environment = audit_environment(prefix, sites, requirements)
    pins, digest = read_requirements(requirements)
    if digest != environment["requirements_sha256"]:
        raise ValueError("Requirements changed after environment audit")
    packages = {package["name"]: package for package in environment["packages"]}
    blockers = []
    if not environment["requirements_match"]:
        blockers.append("REQUIREMENTS_MISMATCH")
    if (
        environment["unlisted_distributions"]
        or environment["uninspected_legacy_metadata"]
    ):
        blockers.append("ENVIRONMENT_NOT_FULLY_PINNED")
    for name in DEPENDENCIES:
        package = packages.get(name)
        if package is None:
            blockers.append("NOT_INSTALLED:" + name)
        elif name not in pins:
            blockers.append("NOT_PINNED:" + name)
        elif package["integrity"] != "VERIFIED_RECORD":
            blockers.append("UNVERIFIED_RECORD:" + name)
    if any(
        package["integrity"] != "VERIFIED_RECORD" for package in environment["packages"]
    ):
        blockers.append("ENVIRONMENT_RECORD_UNVERIFIED")
    selected = {}
    for name, paths in IMPLEMENTATION_FILES.items():
        package = packages.get(name)
        if package is None:
            continue
        rows = {row["path"]: row for row in package["files"]}
        for relative in paths:
            candidates = [
                site / relative for site in sites if (site / relative).exists()
            ]
            if len(candidates) > 1:
                raise ValueError(
                    "Ambiguous metric implementation across selected sites"
                )
            if not candidates:
                blockers.append("MISSING_IMPLEMENTATION:" + relative)
                continue
            path = contained(candidates[0], prefix)
            row = rows.get(path.relative_to(prefix).as_posix())
            if row is None or row["record_check"] != "MATCH":
                blockers.append("UNCHECKED_IMPLEMENTATION:" + relative)
                continue
            # Recheck selected implementations after the complete installed audit,
            # without loading code or deserializing either kind of learned weights.
            actual, size = file_digest(path)
            if (actual, size) != (row["sha256"], row["bytes"]):
                raise ValueError("Metric implementation changed after installed audit")
            selected[relative] = {"sha256": actual, "bytes": size}
    ssim_digest, ssim_size = file_digest(ssim_implementation, MAX_METADATA_BYTES)
    if not ssim_size:
        raise ValueError("Empty SSIM implementation")
    ssim = {
        "sha256": ssim_digest,
        "bytes": ssim_size,
        "scope": "selected source bytes; policy conformance requires parity checks",
    }
    backbone = None
    if alexnet_weights is None:
        blockers.append("ALEXNET_BYTES_NOT_SELECTED")
    else:
        backbone_digest, backbone_size = file_digest(alexnet_weights)
        if not backbone_size:
            raise ValueError("Empty selected backbone")
        backbone = {
            "sha256": backbone_digest,
            "bytes": backbone_size,
            "scope": "selected local bytes; architecture/provenance/rights unverified",
        }
    if (
        file_digest(requirements, MAX_METADATA_BYTES)[0]
        != environment["requirements_sha256"]
    ):
        raise ValueError("Requirements changed during metric selection")
    identity = {
        "policy": POLICY,
        "requirements_sha256": environment["requirements_sha256"],
        "python_version": environment["python_version"],
        "platform": environment["platform"],
        "packages": [
            {
                key: package[key]
                for key in (
                    "name",
                    "version",
                    "metadata_sha256",
                    "record_sha256",
                    "installed_files_sha256",
                )
            }
            for package in environment["packages"]
        ],
        "implementation_files": selected,
        "ssim_implementation": ssim,
        "selected_alexnet_bytes": backbone,
    }
    return {
        "schema_version": 1,
        "scope": "offline metric identity receipt; not evaluation clearance",
        "status": "BLOCKED" if blockers else "IMPLEMENTED_UNVERIFIED",
        "identity_ready": not blockers,
        "identity_blockers": blockers,
        "metric_qualified": False,
        "rights_clearance": "NOT_PERFORMED",
        "parity": "NOT_PERFORMED",
        "dataset_region_and_resolution": "NOT_SELECTED",
        "identity": identity,
        "identity_sha256": hashlib.sha256(canonical_bytes(identity)).hexdigest(),
        "policy_sha256": hashlib.sha256(canonical_bytes(POLICY)).hexdigest(),
        "environment": environment,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--requirements", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--ssim-implementation", required=True, type=Path)
    parser.add_argument(
        "--alexnet-weights",
        type=Path,
        help="Explicit local bytes only; no cache search or checkpoint loading",
    )
    parser.add_argument(
        "--require-identities",
        action="store_true",
        help="Exit 3 after publishing a blocked identity receipt",
    )
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    try:
        # Check the entry itself, not just its resolved target: dangling links are
        # existing caller data too. Publication additionally refuses replacement.
        output = args.output.resolve()
        if (
            not output.is_relative_to(root / "artifacts")
            or args.output.is_symlink()
            or args.output.exists()
        ):
            raise ValueError("Select a new receipt path inside ignored artifacts/")
        if sys.prefix == sys.base_prefix:
            raise ValueError(
                "Run with the explicitly selected metric virtual environment"
            )
        sites = {Path(sysconfig.get_path(key)) for key in ("purelib", "platlib")}
        report = metric_manifest(
            Path(sys.prefix),
            sites,
            args.requirements,
            args.ssim_implementation,
            args.alexnet_weights,
        )
        report["auditor_sha256"] = file_digest(Path(__file__), MAX_METADATA_BYTES)[0]
        report["installed_auditor_sha256"] = file_digest(
            root / "tools/audit_worker_environment.py", MAX_METADATA_BYTES
        )[0]
        publish_receipt(output, report)
        print(
            json.dumps(
                {
                    "receipt": str(output.relative_to(root)),
                    "status": report["status"],
                    "identity_ready": report["identity_ready"],
                    "metric_qualified": False,
                    "identity_blockers": report["identity_blockers"],
                    "identity_sha256": report["identity_sha256"],
                }
            )
        )
        return 3 if args.require_identities and not report["identity_ready"] else 0
    except (OSError, ValueError, csv.Error) as error:
        print(f"Metric identity audit failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
