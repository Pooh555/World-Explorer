"""Axial-depth geometry and custom CPU integration; backend imported only on use."""

import itertools


def check_cancel(cancelled):
    if cancelled():
        raise InterruptedError("Fusion cancelled")


def world_points(view, x, y, depth):
    import numpy as np

    fx, fy, cx, cy = view["intrinsics"]
    camera = np.column_stack(((x - cx) * depth / fx, (y - cy) * depth / fy, depth))
    transform = view["cam_from_world"]
    return (camera - transform[:, 3]) @ transform[:, :3]


def project(view, points):
    import numpy as np

    camera = points @ view["cam_from_world"][:, :3].T + view["cam_from_world"][:, 3]
    fx, fy, cx, cy = view["intrinsics"]
    h, w = view["depth"].shape
    z = camera[:, 2]
    safe_z = np.where(z > 0, z, 1)
    u = camera[:, 0] * fx / safe_z + cx
    v = camera[:, 1] * fy / safe_z + cy
    inside = (
        (z > 0)
        & np.isfinite(u)
        & np.isfinite(v)
        & (u >= -0.5)
        & (u < w - 0.5)
        & (v >= -0.5)
        & (v < h - 0.5)
    )
    # Floor(x+.5) fixes half-pixel tie semantics; never cast unbounded projections.
    x = np.floor(np.clip(u, 0, w - 1) + 0.5).astype(np.int64)
    y = np.floor(np.clip(v, 0, h - 1) + 0.5).astype(np.int64)
    return x, y, z, inside


def robust_extent(views, cancelled=lambda: False):
    import numpy as np

    points = []
    for view in views:
        check_cancel(cancelled)
        y, x = np.nonzero((view["valid"] == 1) & (view["confidence"] > 0))
        points.append(world_points(view, x, y, view["depth"][y, x]))
    points = np.concatenate(points)
    if not len(points) or not np.isfinite(points).all():
        raise ValueError("No finite positive-weight depth extent")
    bounds = np.percentile(points, [1, 99], axis=0, method="linear")
    extent = float(np.max(bounds[1] - bounds[0]))
    if not 1e-12 < extent < 1e12:
        raise ValueError("Degenerate or unsupported arbitrary-unit extent")
    return extent, bounds.tolist(), len(points)


def consistent_masks(views, recipe, cancelled=lambda: False):
    import numpy as np

    masks, summaries = [], []
    for index, view in enumerate(views):
        mask = np.zeros(view["depth"].shape, dtype=bool)
        y, x = np.nonzero((view["valid"] == 1) & (view["confidence"] > 0))
        agreed, occluded = 0, 0
        for start in range(0, len(x), recipe["chunk_voxels"]):
            check_cancel(cancelled)
            xx, yy = (
                x[start : start + recipe["chunk_voxels"]],
                y[start : start + recipe["chunk_voxels"]],
            )
            depth = view["depth"][yy, xx]
            points = world_points(view, xx, yy, depth)
            support = np.zeros(len(points), dtype=np.uint8)
            blocked = np.zeros(len(points), dtype=bool)
            for source_index, source in enumerate(views):
                if source_index == index:
                    continue
                sx, sy, z, inside = project(source, points)
                sd = source["depth"][sy, sx]
                known = (
                    inside
                    & (source["valid"][sy, sx] == 1)
                    & (source["confidence"][sy, sx] > 0)
                )
                close = known & (
                    np.abs(sd - z) <= recipe["relative_depth_error"] * np.maximum(sd, z)
                )
                blocked |= known & (z > sd * (1 + recipe["relative_depth_error"]))
                back = world_points(source, sx, sy, sd)
                camera = (
                    back @ view["cam_from_world"][:, :3].T
                    + view["cam_from_world"][:, 3]
                )
                fx, fy, cx, cy = view["intrinsics"]
                bz = np.where(camera[:, 2] > 0, camera[:, 2], 1)
                distance = np.hypot(
                    camera[:, 0] * fx / bz + cx - xx, camera[:, 1] * fy / bz + cy - yy
                )
                support += (
                    close
                    & (camera[:, 2] > 0)
                    & (distance <= recipe["roundtrip_pixels"])
                ).astype(np.uint8)
            enough = support >= recipe["minimum_consistent_sources"]
            keep = enough & ~blocked
            mask[yy, xx] = keep
            agreed += int(enough.sum())
            occluded += int(blocked.sum())
        valid = int((view["valid"] == 1).sum())
        summaries.append(
            {
                "name": view["name"],
                "pixels": int(mask.size),
                "valid": valid,
                "unknown": int(mask.size) - valid,
                "zero_confidence_valid": valid - len(x),
                "enough_consistent_sources": agreed,
                "known_occlusion": occluded,
                "accepted": int(mask.sum()),
                "rejected_positive_weight": len(x) - int(mask.sum()),
                "uncertainty": (
                    "UNAVAILABLE"
                    if view["sigma"] is None
                    else "CAMERA_UNIT_STANDARD_DEVIATION"
                ),
            }
        )
        masks.append(mask)
    return masks, summaries


ATTRIBUTES = (
    "tsdf",
    "weight",
    "observation_count",
    "support_sum",
    "sigma2_sum",
    "sigma_weight",
    "front_count",
)


def integrate(views, masks, voxel_size, recipe, cancelled=lambda: False):
    import numpy as np
    import open3d as o3d

    if o3d.__version__ != "0.19.0":
        raise ValueError("Unselected Open3D version")
    resolution = recipe["block_resolution"]
    trunc = recipe["truncation_voxels"] * voxel_size
    # Surface blocks plus a one-block halo cover the entire four-voxel band.
    # Allocation never marks a voxel as observed and is bounded before activation.
    keys = set()
    offsets = tuple(itertools.product((-1, 0, 1), repeat=3))
    for view, mask in zip(views, masks):
        y, x = np.nonzero(mask)
        for start in range(0, len(x), recipe["chunk_voxels"]):
            check_cancel(cancelled)
            xx, yy = (
                x[start : start + recipe["chunk_voxels"]],
                y[start : start + recipe["chunk_voxels"]],
            )
            blocks = np.floor(
                world_points(view, xx, yy, view["depth"][yy, xx])
                / (resolution * voxel_size)
            )
            # Backend lattice indexing is int32 and positions are float32.
            # Bound before narrowing/activation, including halo and block corners.
            # Large translated worlds need explicit rebasing in a future recipe.
            if not np.isfinite(blocks).all() or np.any(
                np.abs(blocks) >= 2**20 // resolution - 2
            ):
                raise ValueError(
                    "Fusion lattice coordinate range/precision exceeded; explicit rebasing required"
                )
            for block in np.unique(blocks.astype(np.int32), axis=0):
                for offset in offsets:
                    keys.add(tuple(int(a) + b for a, b in zip(block, offset)))
                if len(keys) > recipe["max_blocks"]:
                    raise ValueError(
                        "Fusion sparse block budget exceeded; no automatic coarsening"
                    )
    if not keys:
        raise ValueError("No cross-view-consistent observed depth")
    c = o3d.core
    grid = o3d.t.geometry.VoxelBlockGrid(
        ATTRIBUTES,
        (c.float32, c.float32, c.uint32, c.uint32, c.float64, c.float32, c.uint32),
        ((1,),) * len(ATTRIBUTES),
        voxel_size,
        resolution,
        len(keys),
        c.Device("CPU:0"),
    )
    ordered = np.array(sorted(keys), dtype=np.int32)
    tensor = c.Tensor(ordered)
    grid.hashmap().activate(tensor)
    indices, found = grid.hashmap().find(tensor)
    if not found.numpy().all() or grid.hashmap().size() != len(keys):
        raise ValueError("Sparse allocation differs")
    attributes = {name: grid.attribute(name).numpy().reshape(-1) for name in ATTRIBUTES}
    for array in attributes.values():
        array.fill(0)
    block_chunk = max(1, recipe["chunk_voxels"] // resolution**3)
    for start in range(0, len(keys), block_chunk):
        check_cancel(cancelled)
        coordinates, flat = grid.voxel_coordinates_and_flattened_indices(
            indices[start : start + block_chunk]
        )
        points, flat = coordinates.numpy().astype(np.float64), flat.numpy().reshape(-1)
        for view, mask in zip(views, masks):
            check_cancel(cancelled)
            x, y, z, inside = project(view, points)
            sdf = view["depth"][y, x] - z
            keep = inside & mask[y, x] & (np.abs(sdf) <= trunc)
            ids, x, y, sdf = flat[keep], x[keep], y[keep], sdf[keep]
            weight = view["confidence"][y, x]
            previous = attributes["weight"][ids]
            total = previous + weight
            attributes["tsdf"][ids] = (
                attributes["tsdf"][ids] * previous + weight * sdf / trunc
            ) / total
            attributes["weight"][ids] = total
            attributes["observation_count"][ids] += 1
            attributes["support_sum"][ids] += view["support"][y, x]
            attributes["front_count"][ids] += (sdf > 0).astype(np.uint32)
            if view["sigma"] is not None:
                attributes["sigma2_sum"][ids] += (
                    weight * view["sigma"][y, x].astype(np.float64) ** 2
                )
                attributes["sigma_weight"][ids] += weight
    for value in attributes.values():
        if not np.isfinite(value).all():
            raise ValueError("Nonfinite fused attributes")
    return grid, indices, len(keys)


def observed_arrays(grid, indices, voxel_size, recipe, cancelled=lambda: False):
    import numpy as np

    arrays = {name: [] for name in ("lattice",) + ATTRIBUTES}
    step = max(1, recipe["chunk_voxels"] // recipe["block_resolution"] ** 3)
    attributes = {name: grid.attribute(name).numpy().reshape(-1) for name in ATTRIBUTES}
    for start in range(0, len(indices), step):
        check_cancel(cancelled)
        points, flat = grid.voxel_coordinates_and_flattened_indices(
            indices[start : start + step]
        )
        flat = flat.numpy().reshape(-1)
        known = attributes["weight"][flat] > 0
        arrays["lattice"].append(
            np.rint(points.numpy()[known] / voxel_size).astype(np.int32)
        )
        for name in ATTRIBUTES:
            arrays[name].append(attributes[name][flat[known]])
    return {name: np.concatenate(parts) for name, parts in arrays.items()}
