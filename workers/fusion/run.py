#!/usr/bin/env python3
"""Supervised local observed TSDF pilot; never a scene/collision publisher."""

import argparse
import importlib.metadata
import json
import os
from pathlib import Path
import signal
import shutil
import subprocess
import sys
import time

from contract import file_record, load_request, new_output, parse, publish

__all__ = [
    "execute",
    "main",
    "supervise",
]


def execute(args, cancelled):
    from admitted_visibility import iter_admitted_sequence, load_admitted_cache
    from geometry import (
        robust_extent,
        consistent_masks,
        integrate,
        observed_arrays,
        check_cancel,
    )
    import numpy as np

    request, recipe, cache, inputs = load_request(args.request, args.sha256)
    for name, version in [
        ("open3d-cpu", "0.19.0"),
        ("numpy", "2.2.6"),
        ("Pillow", "11.3.0"),
    ]:
        if importlib.metadata.version(name) != version:
            raise ValueError("Fusion environment version differs: " + name)
    output = new_output(Path(args.output), inputs)
    sequence = None
    if recipe["kind"] == "OBSERVED_TSDF_CPU_SEQUENCE_V1":
        from contract import read_pinned
        from sequence import StagedSequence

        receipt = parse(
            read_pinned(cache / "manifest.json", request["receipt_sha256"], 1024**2)
        )
        output.mkdir(parents=True, exist_ok=False)
        sequence = StagedSequence(output / "scratch", recipe, cancelled)
        sequence.stage(
            iter_admitted_sequence(cache, request["receipt_sha256"], cancelled)
        )
        print(
            json.dumps(
                {
                    "phase": "staged",
                    "frames": len(sequence.records),
                    "reserved_scratch_bytes": sequence.reserved_bytes,
                }
            ),
            flush=True,
        )
        extent, bounds, samples = sequence.extent()
        print(
            json.dumps(
                {
                    "phase": "extent",
                    "extent": extent,
                    "bounds": bounds,
                    "samples": samples,
                }
            ),
            flush=True,
        )
        coverage = sequence.filter(
            lambda row: print(json.dumps({"phase": "filtered_view", **row}), flush=True)
        )
        views, masks = None, None
    else:
        views, receipt = load_admitted_cache(
            cache, request["receipt_sha256"], cancelled
        )
        extent, bounds, samples = robust_extent(views, cancelled)
        masks, coverage = consistent_masks(views, recipe, cancelled)
    voxel_size = extent / recipe["voxel_divisor"]
    print(
        json.dumps(
            {
                "phase": "filtered",
                "extent": extent,
                "voxel_size": voxel_size,
                "coverage": coverage,
            }
        ),
        flush=True,
    )
    grid, indices, blocks = integrate(
        views,
        masks,
        voxel_size,
        recipe,
        cancelled,
        pairs=sequence.pairs if sequence is not None else None,
    )
    arrays = observed_arrays(grid, indices, voxel_size, recipe, cancelled)
    check_cancel(cancelled)
    mesh = grid.extract_triangle_mesh(weight_threshold=1e-6)
    mesh.compute_vertex_normals()
    positions = mesh.vertex.positions.numpy()
    normals = mesh.vertex.normals.numpy()
    triangles = mesh.triangle.indices.numpy()
    if (
        not np.isfinite(positions).all()
        or not np.isfinite(normals).all()
        or (
            triangles.size
            and (triangles.min() < 0 or triangles.max() >= len(positions))
        )
    ):
        raise ValueError("Invalid extracted mesh")
    # No hole filling, topology repair, connected-component removal or simplification.
    # Raw extraction is explicitly partial; later qualification owns those policies.
    if sequence is None:
        output.mkdir(parents=True, exist_ok=False)
    artifacts = {}
    if (
        sum(array.nbytes for array in arrays.values())
        + positions.nbytes
        + normals.nbytes
        + triangles.nbytes
        + 1024**2
        > recipe["max_output_bytes"]
    ):
        raise ValueError("Fusion raw output byte budget exceeded before writing")
    for name, data in [
        ("observed-voxels.npz", arrays),
        (
            "mesh.npz",
            {"positions": positions, "normals": normals, "triangles": triangles},
        ),
    ]:
        check_cancel(cancelled)
        path = output / name
        with path.open("xb") as stream:
            np.savez_compressed(stream, **data)
            stream.flush()
            os.fsync(stream.fileno())
        artifacts[name] = file_record(path)
        if (
            sum(record["bytes"] for record in artifacts.values())
            > recipe["max_output_bytes"]
        ):
            raise ValueError("Fusion output byte budget exceeded")
    check_cancel(cancelled)
    result = {
        "schema_version": 1,
        "kind": "PARTIAL_OBSERVED_TSDF_V1",
        "request_sha256": args.sha256,
        "receipt_sha256": request["receipt_sha256"],
        "recipe_sha256": request["recipe_sha256"],
        "pose_asset_sha256": receipt["pose_asset_sha256"],
        "world_epoch": receipt["world_epoch"],
        "pose_revision": receipt["pose_revision"],
        "units": (
            "ARBITRARY_POSE_CAMERA_UNITS" if sequence is not None else receipt["units"]
        ),
        "world_axes": "RIGHT_HANDED_Y_UP",
        "depth_convention": "AXIAL_CAMERA_Z",
        "extent_bounds_p01_p99": bounds,
        "extent_samples": samples,
        "extent": extent,
        "voxel_size": voxel_size,
        "truncation": 4 * voxel_size,
        "block_resolution": 8,
        "allocated_blocks": blocks,
        "observed_voxels": len(arrays["weight"]),
        "unknown_allocated_voxels": blocks * 8**3 - len(arrays["weight"]),
        "front_band_voxels": int((arrays["front_count"] > 0).sum()),
        "sigma_available_voxels": int((arrays["sigma_weight"] > 0).sum()),
        "vertices": len(positions),
        "triangles": len(triangles),
        "coverage": coverage,
        "files": artifacts,
        "scope": recipe["scope"],
        "free_space": recipe["free_space"],
        "walking_enabled": False,
        "unknown": "ABSENT_LATTICE_ENTRIES_AND_ZERO_WEIGHT_ARE_UNKNOWN; NO_FULL_RAY_CARVING",
        "uncertainty": "WEIGHTED_RMS_INPUT_SIGMA_WHERE_SIGMA_WEIGHT_POSITIVE; NOT_SURFACE_ERROR",
        "radiance": "NO_COLOR_OR_RELIGHTABLE_ALBEDO_PRODUCT",
        "mesh_policy": "RAW_WEIGHT_SUPPORTED_EXTRACTION; NO_FILL_OR_SIMPLIFICATION",
    }
    # Successful scratch has no published meaning. Remove only this new owned
    # workspace before committing; failed/cancelled attempts keep their scratch.
    if sequence is not None:
        check_cancel(cancelled)
        shutil.rmtree(sequence.directory)
        check_cancel(cancelled)
    # Only the successful supervisor may publish the consumable terminal receipt.
    publish(
        output / "result.pending.json",
        (json.dumps(result, sort_keys=True, indent=2) + "\n").encode(),
    )
    print(
        json.dumps(
            {
                "phase": "published",
                "blocks": blocks,
                "observed_voxels": len(arrays["weight"]),
                "vertices": len(positions),
                "triangles": len(triangles),
            }
        ),
        flush=True,
    )


def supervise(args, cancelled):
    if sys.platform != "linux":
        raise ValueError("RSS-supervised CPU pilot currently requires Linux /proc")
    _, recipe, _, inputs = load_request(args.request, args.sha256)
    new_output(Path(args.output), inputs)
    env = dict(
        os.environ,
        OMP_NUM_THREADS=str(recipe["omp_threads"]),
        OPENBLAS_NUM_THREADS="1",
        OPEN3D_CPU_RENDERING="true",
    )
    command = [
        sys.executable,
        str(Path(__file__).resolve()),
        str(args.request),
        args.sha256,
        str(args.output),
        "--child",
    ]
    started, peak, reason = time.monotonic(), 0, None
    child = subprocess.Popen(command, env=env, start_new_session=True)
    try:
        while child.poll() is None:
            try:
                status = Path(f"/proc/{child.pid}/status").read_text()
                rss = (
                    int(
                        next(
                            line.split()[1]
                            for line in status.splitlines()
                            if line.startswith("VmRSS:")
                        )
                    )
                    * 1024
                )
                peak = max(peak, rss)
            except (FileNotFoundError, ProcessLookupError, StopIteration):
                pass
            if cancelled():
                reason = "CANCELLED"
            elif time.monotonic() - started > recipe["max_seconds"]:
                reason = "TIME_BUDGET"
            elif peak > recipe["max_rss_bytes"]:
                reason = "RSS_BUDGET"
            if reason:
                os.killpg(child.pid, signal.SIGTERM)
                try:
                    child.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    os.killpg(child.pid, signal.SIGKILL)
                    child.wait()
                break
            time.sleep(0.02)
    finally:
        if child.poll() is None:
            os.killpg(child.pid, signal.SIGKILL)
        child.wait()
    print(
        json.dumps(
            {
                "phase": "supervisor",
                "child_exit": child.returncode,
                "reason": reason,
                "seconds": time.monotonic() - started,
                "peak_child_rss_bytes_sampled_20ms": peak,
            }
        ),
        flush=True,
    )
    if not reason and child.returncode == 0 and not cancelled():
        pending = Path(args.output) / "result.pending.json"
        data = pending.read_bytes()
        if len(data) > 1024**2:
            raise ValueError("Oversized fusion receipt")
        result = parse(data)
        result["supervisor"] = {
            "seconds": time.monotonic() - started,
            "peak_child_rss_bytes_sampled_20ms": peak,
        }
        publish(
            Path(args.output) / "manifest.json",
            (json.dumps(result, sort_keys=True, indent=2) + "\n").encode(),
        )
    elif cancelled():
        return 130
    return 130 if reason == "CANCELLED" else 2 if reason else child.returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("request", type=Path)
    parser.add_argument("sha256")
    parser.add_argument("output", type=Path)
    parser.add_argument("--child", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    stopped = False

    def stop(*_):
        nonlocal stopped
        stopped = True

    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)
    try:
        if args.child:
            execute(args, lambda: stopped)
            return 0
        return supervise(args, lambda: stopped)
    except InterruptedError as error:
        print(str(error), file=sys.stderr)
        return 130
    except (ValueError, OSError, KeyError, TypeError, RuntimeError) as error:
        print(str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
