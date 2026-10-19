"""Bounded deterministic sparse-track source planning, not dense coverage evidence."""

import copy
import json
import math
from pathlib import Path

from pilot import digest

DEFAULT_POLICY = {
    "algorithm": "COVISIBLE_PARALLAX_GREEDY_V1",
    "reference_indices": list(range(9)),
    "max_views": 18,
    "max_sources": 6,
    "min_shared_tracks": 30,
    "min_angle_degrees": 3.0,
    "max_angle_degrees": 60.0,
}
MAX_PAIR_TRACKS = 4_000_000


def report_digest(report):
    return digest(
        json.dumps(
            report, sort_keys=True, separators=(",", ":"), allow_nan=False
        ).encode()
    )


def validate_policy(policy, count):
    if (
        set(policy) != set(DEFAULT_POLICY)
        or policy["algorithm"] != DEFAULT_POLICY["algorithm"]
        or not 1 <= count <= 120
    ):
        raise ValueError("Unsupported sparse source-selection policy")
    refs = policy["reference_indices"]
    if (
        not isinstance(refs, list)
        or not 1 <= len(refs) <= 9
        or any(type(i) is not int for i in refs)
        or refs != sorted(set(refs))
        or refs[0] < 0
        or refs[-1] >= count
    ):
        raise ValueError("Invalid reference inventory")
    for key, low, high in [
        ("max_views", max(3, len(refs)), 18),
        ("max_sources", 2, 6),
        ("min_shared_tracks", 1, 100_000),
    ]:
        if type(policy[key]) is not int or not low <= policy[key] <= high:
            raise ValueError("Invalid source-selection resource/track bound")
    for key in ("min_angle_degrees", "max_angle_degrees"):
        value = policy[key]
        if type(value) not in (int, float) or not math.isfinite(value):
            raise ValueError("Invalid parallax angle")
    if not 3 <= policy["min_angle_degrees"] < policy["max_angle_degrees"] <= 60:
        raise ValueError("Unsupported parallax interval")


def choose_sources(
    centers, points, observations, names, policy, cancelled=lambda: False
):
    """Pure array selection. Indices are canonical manifest order; input arrays are borrowed."""
    import numpy as np

    count = len(names)
    validate_policy(policy, count)
    centers = np.asarray(centers, dtype=np.float64)
    points = np.asarray(points, dtype=np.float64)
    if (
        centers.shape != (count, 3)
        or points.ndim != 2
        or points.shape[1] != 3
        or not 1 <= len(points) <= 100_000
        or len(observations) != count
        or len(set(names)) != count
        or not np.isfinite(centers).all()
        or not np.isfinite(points).all()
    ):
        raise ValueError("Invalid bounded sparse geometry")
    visible = []
    total = 0
    for values in observations:
        ids = np.asarray(values)
        if (
            ids.ndim != 1
            or ids.dtype.kind not in "iu"
            or len(np.unique(ids)) != len(ids)
            or (len(ids) and (ids.min() < 0 or ids.max() >= len(points)))
        ):
            raise ValueError("Invalid/duplicate sparse observations")
        total += len(ids)
        visible.append(np.sort(ids))
    if total > 1_000_000:
        raise ValueError("Sparse observation budget exceeded")
    pairs = {}
    incidence = 0
    for first in range(count):
        for second in range(first + 1, count):
            if cancelled():
                raise InterruptedError("Source selection cancelled")
            ids = np.intersect1d(visible[first], visible[second], assume_unique=True)
            if not len(ids):
                continue
            with np.errstate(over="ignore", invalid="ignore"):
                a, b = points[ids] - centers[first], points[ids] - centers[second]
            if not np.isfinite(a).all() or not np.isfinite(b).all():
                raise ValueError("Sparse ray arithmetic exceeds numeric profile")
            # Normalize by each vector's largest component before the norm;
            # this preserves arbitrary scale without squaring huge/tiny units.
            scale_a, scale_b = np.max(np.abs(a), axis=1), np.max(np.abs(b), axis=1)
            nonzero = (scale_a > 0) & (scale_b > 0)
            angles = np.zeros(len(ids))
            unit_a, unit_b = (
                a[nonzero] / scale_a[nonzero, None],
                b[nonzero] / scale_b[nonzero, None],
            )
            angles[nonzero] = np.degrees(
                np.arctan2(
                    np.linalg.norm(np.cross(unit_a, unit_b), axis=1),
                    np.sum(unit_a * unit_b, axis=1),
                )
            )
            ids = ids[
                nonzero
                & (angles >= policy["min_angle_degrees"])
                & (angles <= policy["max_angle_degrees"])
            ]
            if len(ids) >= policy["min_shared_tracks"]:
                incidence += len(ids)
                if incidence > MAX_PAIR_TRACKS:
                    raise ValueError("Sparse pair-observation budget exceeded")
                pairs[first, second] = ids

    empty = np.array([], dtype=np.int64)

    def shared(first, second):
        return pairs.get((min(first, second), max(first, second)), empty)

    refs = policy["reference_indices"]
    selected = set(refs)
    support = {i: np.zeros(len(points), dtype=np.uint16) for i in refs}
    for ref in refs:
        for source in selected - {ref}:
            support[ref][shared(ref, source)] += 1
    added = []
    while len(selected) < min(count, policy["max_views"]):
        if cancelled():
            raise InterruptedError("Source selection cancelled")
        best, best_score = None, (0, 0, 0)
        for candidate in range(count):
            if candidate in selected:
                continue
            weighted, second_gain, first_gain = 0, 0, 0
            for ref in refs:
                values = support[ref][shared(ref, candidate)]
                one, two = int(np.count_nonzero(values == 0)), int(
                    np.count_nonzero(values == 1)
                )
                divisor = max(1, len(visible[ref]))
                weighted += (2 * two + one) * 1_000_000 // divisor
                second_gain += two
                first_gain += one
            score = weighted, second_gain, first_gain
            if score > best_score:
                best, best_score = candidate, score
        if best is None:
            break
        selected.add(best)
        added.append(best)
        for ref in refs:
            support[ref][shared(ref, best)] += 1

    # Every source needs its own geometric depth row with two eligible sources.
    # Remove unsupported auxiliary vertices to a fixed point; references cannot
    # be dropped to improve reported yield.
    dropped = []
    while True:
        bad = [
            i
            for i in sorted(selected)
            if sum(bool(len(shared(i, j))) for j in selected - {i}) < 2
        ]
        if any(i in refs for i in bad):
            raise ValueError("Reference lacks two co-visible parallax sources; no plan")
        if not bad:
            break
        selected.difference_update(bad)
        dropped.extend(bad)

    rows, stats = {}, []
    for ref in sorted(selected):
        if cancelled():
            raise InterruptedError("Source selection cancelled")
        counts = np.zeros(len(points), dtype=np.uint16)
        candidates = sorted(j for j in selected - {ref} if len(shared(ref, j)))
        sources = []
        while candidates and len(sources) < policy["max_sources"]:

            def rank(source):
                values = counts[shared(ref, source)]
                one, two = int(np.count_nonzero(values == 0)), int(
                    np.count_nonzero(values == 1)
                )
                return 2 * two + one, two, len(values), -source

            best = max(candidates, key=rank)
            sources.append(best)
            candidates.remove(best)
            counts[shared(ref, best)] += 1
        rows[names[ref]] = [names[i] for i in sources]
        if ref in refs:
            valid = int(np.count_nonzero(counts >= 2))
            stats.append(
                {
                    "index": ref,
                    "image": names[ref],
                    "visible_landmarks": len(visible[ref]),
                    "landmarks_with_two_sources": valid,
                    "fraction_with_two_sources": valid / max(1, len(visible[ref])),
                }
            )
    return {
        "schema_version": 1,
        "scope": "SPARSE_TRACK_SELECTION_NOT_DENSE_COVERAGE",
        "policy": copy.deepcopy(policy),
        "selected_indices": sorted(selected),
        "neighbors": rows,
        "reference_support": stats,
        "added_indices": added,
        "dropped_indices": dropped,
        "eligible_pair_observations": incidence,
        "eligible_pairs": len(pairs),
    }


def plan_sources(reconstruction, native, policy, cancelled=lambda: False):
    import numpy as np
    import pycolmap

    frames = native["frames"]
    validate_policy(policy, len(frames))
    if (
        reconstruction.num_cameras() != 1
        or not 1 <= reconstruction.num_points3D() <= 100_000
        or sum(image.num_points2D() for image in reconstruction.images.values())
        > 1_000_000
    ):
        raise ValueError("Sparse model exceeds source-selection profile")
    by_name = {image.name: image for image in reconstruction.images.values()}
    names = [Path(frame["image"]).name for frame in frames]
    if names != sorted(set(names)) or set(names) != set(by_name):
        raise ValueError("Native model/manifest inventory differs")
    ids = sorted(reconstruction.points3D)
    lookup = {value: index for index, value in enumerate(ids)}
    points = np.array([reconstruction.points3D[i].xyz for i in ids])
    memberships = {}
    total_tracks = 0
    for point_id in ids:
        entries = reconstruction.points3D[point_id].track.elements
        total_tracks += len(entries)
        if total_tracks > 1_000_000:
            raise ValueError("Sparse track-reference budget exceeded")
        members = {(entry.image_id, entry.point2D_idx) for entry in entries}
        if len(members) != len(entries):
            raise ValueError("Duplicate sparse track element")
        for image_id, index in members:
            if (
                image_id not in reconstruction.images
                or index >= reconstruction.images[image_id].num_points2D()
                or reconstruction.images[image_id].points2D[index].point3D_id
                != point_id
            ):
                raise ValueError("Sparse track back-reference differs")
        memberships[point_id] = members
    centers, observations = [], []
    collapsed = 0
    for frame, name in zip(frames, names):
        if cancelled():
            raise InterruptedError("Source selection cancelled")
        image = by_name[name]
        camera = reconstruction.cameras[image.camera_id]
        if (
            image.image_id != frame["backend_image_id"]
            or image.camera_id != frame["backend_camera_id"]
            or camera.model_name not in ("PINHOLE", "SIMPLE_PINHOLE")
            or (camera.width, camera.height) != (frame["width"], frame["height"])
        ):
            raise ValueError("Source model image/calibration identity differs")
        k = camera.calibration_matrix()
        if not np.allclose(
            [k[0, 0], k[1, 1], k[0, 2] - 0.5, k[1, 2] - 0.5],
            frame["intrinsics"],
            rtol=0,
            atol=1e-10,
        ):
            raise ValueError("Source model calibration differs")
        pose = image.cam_from_world().inverse().matrix().copy()
        pose[1:3] *= -1
        expected = frame["world_from_camera"]
        q = expected["rotation_wxyz"]
        expected_pose = np.column_stack(
            (
                pycolmap.Rotation3d([q[1], q[2], q[3], q[0]]).matrix(),
                expected["translation"],
            )
        )
        if expected["scale"] != 1 or not np.allclose(
            pose, expected_pose, rtol=0, atol=1e-8
        ):
            raise ValueError("Source model pose differs")
        center = image.projection_center()
        centers.append(center)
        seen, visible = set(), set()
        transform = image.cam_from_world().matrix()
        for index, point in enumerate(image.points2D):
            if index % 4096 == 0 and cancelled():
                raise InterruptedError("Source selection cancelled")
            if not np.isfinite(point.xy).all():
                raise ValueError("Nonfinite sparse observation")
            if not point.has_point3D():
                continue
            point_id = point.point3D_id
            if (
                point_id not in lookup
                or (image.image_id, index) not in memberships[point_id]
            ):
                raise ValueError("Missing/inconsistent sparse track identity")
            if point_id in seen:
                collapsed += 1
            seen.add(point_id)
            ordinal = lookup[point_id]
            xyz = points[ordinal]
            if (
                0 <= point.xy[0] < camera.width
                and 0 <= point.xy[1] < camera.height
                and (transform[:3, :3] @ xyz + transform[:3, 3])[2] > 0
            ):
                visible.add(ordinal)
        observations.append(np.array(sorted(visible), dtype=np.int64))
    report = choose_sources(centers, points, observations, names, policy, cancelled)
    report["collapsed_same_view_observations"] = collapsed
    return report
