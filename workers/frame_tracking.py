"""Bounded persistent thumbnail tracks; this candidate policy is unqualified.

Point positions carry pairwise correspondences through intermediate candidates.
Displacement is measured from each point's birth since the last selection,
not from an invented correspondence to the keyframe. It is not 3D parallax.
"""

import hashlib
import math
from collections.abc import Callable, Sequence
from itertools import combinations
from statistics import median
from typing import Final

__all__ = ["MAX_CANDIDATES", "PROFILE", "analyze", "policy"]

MAX_CANDIDATES: Final = 480
PROFILE: Final = "quality_keyframes_candidate_v2"
_WIDTH: Final = 96
_HEIGHT: Final = 64


def policy() -> dict:
    """Return the versioned recipe, including unchanged selection minimums."""
    return {
        "metric_version": "gray96_persistent_affine_v2",
        "threshold_version": "candidate_2_2",
        "qualification": "UNQUALIFIED",
        "analysis_width": _WIDTH,
        "analysis_height": _HEIGHT,
        "thresholds": {
            "min_laplacian_variance": 25,
            "min_mean": 12,
            "max_mean": 243,
            "max_clipped_fraction": 0.8,
            "max_exposure_change": 55,
            "min_corner_eigenvalue": 100,
            "max_patch_rms": 18,
            "ambiguity_ratio": 1.1,
            "ambiguity_margin": 0.5,
            "forward_backward_pixels": 1,
            "search_radius": 6,
            "min_tracked_cells": 4,
            "min_continued_tracks": 4,
            "min_displacement_px": 1.5,
            "cut_histogram_distance": 0.65,
            "points_per_cell": 4,
            "point_separation": 4,
            "seed_window": 3,
            "max_tracking_gap": 12,
            "refinement_iterations": 8,
            "refinement_tolerance": 0.01,
            "motion_consensus_radius": 1.5,
            "min_motion_consensus_ratio": 0.6,
            "max_affine_models": 128,
            "min_affine_triangle_determinant": 16,
            "min_affine_jacobian_determinant": 0.25,
            "max_affine_jacobian_determinant": 4,
            "min_raw_patch_correlation": 0.6,
        },
        "coverage": "tracked cells in a 4x3 image grid; no 3D coverage claim",
        "displacement_reference": "point birth since last selected keyframe; not net camera motion",
        "tracking_prefilter": "binomial_3x3_edge_replicate",
        "lens_change_detection": "dimension_metadata_only",
    }


def _sample(image: bytes, x: float, y: float) -> float:
    left, top = math.floor(x), math.floor(y)
    dx, dy = x - left, y - top
    index = top * _WIDTH + left
    return (
        image[index] * (1 - dx) * (1 - dy)
        + image[index + 1] * dx * (1 - dy)
        + image[index + _WIDTH] * (1 - dx) * dy
        + image[index + _WIDTH + 1] * dx * dy
    )


def _center(values: list[float]) -> list[float]:
    mean = sum(values) / len(values)
    return [value - mean for value in values]


def _patch(image: bytes, x: float, y: float) -> list[float]:
    return _center(
        [_sample(image, x + dx, y + dy) for dy in range(-2, 3) for dx in range(-2, 3)]
    )


def _integer_patch(image: bytes, x: int, y: int) -> list[float]:
    return _center(
        [
            image[(y + dy) * _WIDTH + x + dx]
            for dy in range(-2, 3)
            for dx in range(-2, 3)
        ]
    )


def _features(image: bytes, thresholds: dict) -> list[tuple[float, float]]:
    points = []
    for cell_y in range(3):
        for cell_x in range(4):
            candidates = []
            for y in range(max(9, cell_y * 21), min(55, (cell_y + 1) * 21), 2):
                for x in range(max(9, cell_x * 24), min(87, (cell_x + 1) * 24), 2):
                    xx = yy = xy = 0.0
                    for dy in (-1, 0, 1):
                        for dx in (-1, 0, 1):
                            index = (y + dy) * _WIDTH + x + dx
                            gx = (image[index + 1] - image[index - 1]) / 2
                            gy = (image[index + _WIDTH] - image[index - _WIDTH]) / 2
                            xx += gx * gx
                            yy += gy * gy
                            xy += gx * gy
                    score = (xx + yy - math.hypot(xx - yy, 2 * xy)) / 18
                    if score > thresholds["min_corner_eigenvalue"]:
                        candidates.append((score, x, y))
            selected = []
            for _, x, y in sorted(candidates, reverse=True):
                if all(
                    math.hypot(x - px, y - py) >= thresholds["point_separation"]
                    for px, py in selected
                ):
                    selected.append((float(x), float(y)))
                    if len(selected) == thresholds["points_per_cell"]:
                        break
            points.extend(selected)
    return points


def _cell(x: float, y: float) -> int:
    return min(2, int(y * 3 / _HEIGHT)) * 4 + min(3, int(x * 4 / _WIDTH))


def _refine(
    patch: list[float], image: bytes, x: float, y: float, thresholds: dict
) -> tuple[float, float] | None:
    initial_x, initial_y = x, y
    for _ in range(thresholds["refinement_iterations"]):
        if not (3 <= x < _WIDTH - 4 and 3 <= y < _HEIGHT - 4):
            return None
        target = _patch(image, x, y)
        gx = _center(
            [
                (
                    _sample(image, x + dx + 1, y + dy)
                    - _sample(image, x + dx - 1, y + dy)
                )
                / 2
                for dy in range(-2, 3)
                for dx in range(-2, 3)
            ]
        )
        gy = _center(
            [
                (
                    _sample(image, x + dx, y + dy + 1)
                    - _sample(image, x + dx, y + dy - 1)
                )
                / 2
                for dy in range(-2, 3)
                for dx in range(-2, 3)
            ]
        )
        xx, yy, xy = (
            sum(a * a for a in gx),
            sum(a * a for a in gy),
            sum(a * b for a, b in zip(gx, gy)),
        )
        determinant = xx * yy - xy * xy
        if determinant <= 1e-6:
            return None
        residual = [a - b for a, b in zip(target, patch)]
        bx = sum(a * b for a, b in zip(gx, residual))
        by = sum(a * b for a, b in zip(gy, residual))
        step_x, step_y = (yy * bx - xy * by) / determinant, (
            xx * by - xy * bx
        ) / determinant
        length = max(1.0, math.hypot(step_x, step_y))
        x, y = x - step_x / length, y - step_y / length
        if math.hypot(x - initial_x, y - initial_y) > 1.5:
            return None
        if math.hypot(step_x, step_y) < thresholds["refinement_tolerance"]:
            break
    if not (3 <= x < _WIDTH - 4 and 3 <= y < _HEIGHT - 4):
        return None
    rms = math.sqrt(sum((a - b) ** 2 for a, b in zip(_patch(image, x, y), patch)) / 25)
    return (x, y) if rms <= thresholds["max_patch_rms"] else None


def _match(
    patch: list[float], image: bytes, x: float, y: float, thresholds: dict
) -> tuple[float, float] | None:
    matches = []
    radius = thresholds["search_radius"]
    for target_y in range(
        max(3, round(y) - radius), min(_HEIGHT - 5, round(y) + radius) + 1
    ):
        for target_x in range(
            max(3, round(x) - radius), min(_WIDTH - 5, round(x) + radius) + 1
        ):
            candidate = _integer_patch(image, target_x, target_y)
            error = math.sqrt(sum((a - b) ** 2 for a, b in zip(patch, candidate)) / 25)
            matches.append((error, target_x, target_y))
    if not matches:
        return None
    matches.sort()
    error, target_x, target_y = matches[0]
    if error > thresholds["max_patch_rms"]:
        return None
    alternatives = [
        value
        for value, px, py in matches[1:]
        if abs(px - target_x) > 1 or abs(py - target_y) > 1
    ]
    if (
        not alternatives
        or min(alternatives)
        <= error * thresholds["ambiguity_ratio"] + thresholds["ambiguity_margin"]
    ):
        return None
    refined = _refine(patch, image, float(target_x), float(target_y), thresholds)
    if (
        refined is not None
        and max(abs(refined[0] - x), abs(refined[1] - y)) <= radius + 0.5
    ):
        return refined
    return None


def _affine_inliers(reference: list, target: list, thresholds: dict) -> list[int]:
    triples = list(combinations(range(len(target)), 3))
    count = min(len(triples), thresholds["max_affine_models"])
    best = []
    for model in range(count):
        first, second, third = triples[model * len(triples) // count]
        x, y = reference[first]
        bx, by = reference[second][0] - x, reference[second][1] - y
        cx, cy = reference[third][0] - x, reference[third][1] - y
        determinant = bx * cy - cx * by
        if abs(determinant) < thresholds["min_affine_triangle_determinant"]:
            continue
        coefficients = []
        for axis in (0, 1):
            origin = target[first][axis]
            db, dc = target[second][axis] - origin, target[third][axis] - origin
            a, b = (db * cy - dc * by) / determinant, (bx * dc - cx * db) / determinant
            coefficients.append((a, b, origin - a * x - b * y))
        (a, b, c), (d, e, f) = coefficients
        jacobian = a * e - b * d
        if (
            not thresholds["min_affine_jacobian_determinant"]
            <= jacobian
            <= thresholds["max_affine_jacobian_determinant"]
        ):
            continue
        inliers = [
            index
            for index, ((px, py), (tx, ty)) in enumerate(zip(reference, target))
            if math.hypot(a * px + b * py + c - tx, d * px + e * py + f - ty)
            <= thresholds["motion_consensus_radius"]
        ]
        if len(inliers) > len(best):
            best = inliers
    return best


def _continue(
    reference: bytes,
    image: bytes,
    tracks: list,
    thresholds: dict,
    check_cancelled: Callable[[], None],
    raw_reference: bytes,
    raw_image: bytes,
) -> list:
    continued = []
    for birth, birth_x, birth_y, x, y in tracks:
        check_cancelled()
        target = _match(_patch(reference, x, y), image, x, y, thresholds)
        if target is None:
            continue
        back = _match(_patch(image, *target), reference, *target, thresholds)
        if (
            back is not None
            and math.hypot(back[0] - x, back[1] - y)
            <= thresholds["forward_backward_pixels"]
        ):
            original = _patch(raw_reference, x, y)
            observed = _patch(raw_image, *target)
            energy = math.sqrt(
                sum(value * value for value in original)
                * sum(value * value for value in observed)
            )
            if (
                energy <= 1e-6
                or sum(a * b for a, b in zip(original, observed)) / energy
                < thresholds["min_raw_patch_correlation"]
            ):
                continue
            continued.append((birth, birth_x, birth_y, *target))
    if not continued:
        return []
    # A local photometric match can survive a cut by chance. Require a
    # consistent small-motion cohort, rather than trusting isolated patches.
    # Anchor/birth coordinates cannot supply pairwise flow after point renewal.
    # Match each continued point to its immutable birth key in the input cohort.
    previous = {(birth, bx, by): (x, y) for birth, bx, by, x, y in tracks}
    flows = [
        (x - previous[(birth, bx, by)][0], y - previous[(birth, bx, by)][1])
        for birth, bx, by, x, y in continued
    ]
    groups = [
        [
            index
            for index, (dx, dy) in enumerate(flows)
            if math.hypot(dx - center_x, dy - center_y)
            <= thresholds["motion_consensus_radius"]
        ]
        for center_x, center_y in flows
    ]
    inliers = max(groups, key=len)
    affine = _affine_inliers(
        [previous[(birth, bx, by)] for birth, bx, by, _, _ in continued],
        [(x, y) for _, _, _, x, y in continued],
        thresholds,
    )
    if len(affine) > len(inliers):
        inliers = affine
    if len(inliers) < len(continued) * thresholds["min_motion_consensus_ratio"]:
        return []
    return [continued[index] for index in inliers]


def _statistics(image: bytes) -> tuple[float, float, float, list[float]]:
    histogram = [0] * 16
    for pixel in image:
        histogram[pixel // 16] += 1
    values = [
        image[index - 1]
        + image[index + 1]
        + image[index - _WIDTH]
        + image[index + _WIDTH]
        - 4 * image[index]
        for y in range(1, _HEIGHT - 1)
        for x in range(1, _WIDTH - 1)
        for index in [y * _WIDTH + x]
    ]
    mean = sum(values) / len(values)
    return (
        sum(image) / len(image),
        sum((value - mean) ** 2 for value in values) / len(values),
        sum(value <= 4 or value >= 251 for value in image) / len(image),
        [count / len(image) for count in histogram],
    )


def _tracking_image(image: bytes) -> bytes:
    # Band-limit the tracking signal before estimating fractional positions.
    # Raw thumbnail hashes and image-quality metrics retain the original bytes.
    return bytes(
        (
            sum(
                image[
                    min(_HEIGHT - 1, max(0, y + dy)) * _WIDTH
                    + min(_WIDTH - 1, max(0, x + dx))
                ]
                * wx
                * wy
                for dy, wy in ((-1, 1), (0, 2), (1, 1))
                for dx, wx in ((-1, 1), (0, 2), (1, 1))
            )
            + 8
        )
        // 16
        for y in range(_HEIGHT)
        for x in range(_WIDTH)
    )


def analyze(images: Sequence[bytes], check_cancelled: Callable[[], None]) -> list[dict]:
    """Keep bounded point histories; never reseed across a lost connection."""
    if not 1 <= len(images) <= MAX_CANDIDATES or any(
        len(image) != _WIDTH * _HEIGHT for image in images
    ):
        raise ValueError("Tracking requires 1..480 GRAY8 96x64 frames")
    thresholds = policy()["thresholds"]
    candidates = []
    tracking_images = []
    for image in images:
        check_cancelled()
        mean, blur, clipped, histogram = _statistics(image)
        features = _features(image, thresholds)
        cells = len({_cell(x, y) for x, y in features})
        quality_reason = None
        if (
            not thresholds["min_mean"] <= mean <= thresholds["max_mean"]
            or clipped > thresholds["max_clipped_fraction"]
        ):
            quality_reason = "exposure"
        elif (
            blur < thresholds["min_laplacian_variance"]
            or cells < thresholds["min_tracked_cells"]
        ):
            quality_reason = "blur_or_texture"
        candidates.append(
            (mean, blur, clipped, histogram, features, cells, quality_reason)
        )
        tracking_images.append(_tracking_image(image))
    eligible = [
        index for index, candidate in enumerate(candidates) if candidate[-1] is None
    ]
    window = (
        [index for index in eligible if index < eligible[0] + thresholds["seed_window"]]
        if eligible
        else []
    )
    seed = (
        max(
            window,
            key=lambda index: (candidates[index][5], len(candidates[index][4]), -index),
        )
        if window
        else None
    )
    rows = []
    tracks = []
    anchor = reference = None
    seen = set()
    previous_mean = previous_histogram = None
    for index, (image, candidate) in enumerate(zip(images, candidates)):
        check_cancelled()
        mean, blur, clipped, histogram, features, feature_cells, quality_reason = (
            candidate
        )
        digest = hashlib.sha256(image).hexdigest()
        distance = (
            sum(abs(a - b) for a, b in zip(histogram, previous_histogram)) / 2
            if previous_histogram is not None
            else 0.0
        )
        continued = (
            _continue(
                tracking_images[reference],
                tracking_images[index],
                tracks,
                thresholds,
                check_cancelled,
                images[reference],
                image,
            )
            if reference is not None
            and index - reference <= thresholds["max_tracking_gap"]
            else []
        )
        cells = len({_cell(x, y) for _, _, _, x, y in continued})
        displacement = (
            median([math.hypot(x - bx, y - by) for _, bx, by, x, y in continued])
            if continued
            else 0.0
        )
        if quality_reason is not None:
            reason = quality_reason
        elif index == seed:
            reason = "seed"
        elif anchor is None:
            reason = "seed_candidate"
        elif digest in seen:
            reason = "repeated"
        elif (
            previous_mean is not None
            and abs(mean - previous_mean) > thresholds["max_exposure_change"]
        ):
            reason = "exposure_change"
        elif len(continued) < thresholds["min_continued_tracks"]:
            reason = (
                "suspected_cut"
                if distance > thresholds["cut_histogram_distance"]
                else "tracking_break"
            )
        elif cells < thresholds["min_tracked_cells"]:
            reason = "insufficient_tracks"
        elif displacement < thresholds["min_displacement_px"]:
            reason = "low_motion"
        else:
            reason = "motion_coverage"
        selected = reason in ("seed", "motion_coverage")
        added = []
        if selected:
            added = [(index, x, y, x, y) for x, y in features]
        elif len(continued) >= thresholds["min_continued_tracks"]:
            for x, y in features:
                if len(continued) + len(added) == 48:
                    break
                if all(
                    math.hypot(x - px, y - py) >= thresholds["point_separation"]
                    for _, _, _, px, py in continued + added
                ):
                    added.append((index, x, y, x, y))
        rows.append(
            {
                "thumbnail_sha256": digest,
                "selected": selected,
                "reason": reason,
                "metrics": {
                    "mean_luma": mean,
                    "laplacian_variance": blur,
                    "clipped_fraction": clipped,
                    "feature_count": len(features),
                    "feature_cells": feature_cells,
                    "track_count": len(continued),
                    "tracked_cells": cells,
                    "displacement_px": displacement,
                    "histogram_distance": distance,
                },
                "tracking": {
                    "anchor": anchor,
                    "reference": reference,
                    "birth_first": (
                        min(point[0] for point in continued) if continued else None
                    ),
                    "birth_last": (
                        max(point[0] for point in continued) if continued else None
                    ),
                    "new_points": len(added),
                },
            }
        )
        if selected:
            anchor = reference = index
            tracks = added
        elif len(continued) >= thresholds["min_continued_tracks"]:
            tracks, reference = continued + added, index
        seen.add(digest)
        previous_mean, previous_histogram = mean, histogram
    return rows
