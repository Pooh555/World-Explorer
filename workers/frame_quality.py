"""Deterministic thumbnail diagnostics; candidate thresholds are unqualified.

All distances and coverage refer to the 96x64 analysis grid, never metric
scene geometry. Only standard-library code runs here; no model is downloaded.
"""

import hashlib
import math
from collections.abc import Callable, Sequence
from statistics import median
from typing import Final

__all__ = ["MAX_CANDIDATES", "PROFILE", "analyze", "policy"]

MAX_CANDIDATES: Final = 480
PROFILE: Final = "quality_keyframes_candidate_v1"
_WIDTH: Final = 96
_HEIGHT: Final = 64
_SEARCH: Final = 6


def policy() -> dict:
    """Return a fresh, explicit recipe rather than mutable shared thresholds."""
    return {
        "metric_version": "gray96_harris_patch_v1",
        "threshold_version": "candidate_1",
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
            "max_patch_error": 18,
            "ambiguity_ratio": 1.1,
            "ambiguity_margin": 0.5,
            "forward_backward_pixels": 1,
            "search_radius": _SEARCH,
            "min_tracked_cells": 4,
            "min_displacement_px": 1.5,
            "cut_histogram_distance": 0.65,
        },
        "coverage": "tracked cells in a 4x3 image grid; no 3D coverage claim",
        "lens_change_detection": "dimension_metadata_only",
    }


def _patch(image: bytes, x: int, y: int) -> tuple[float, ...]:
    values = [
        image[(y + dy) * _WIDTH + x + dx] for dy in range(-2, 3) for dx in range(-2, 3)
    ]
    mean = sum(values) / 25
    return tuple(value - mean for value in values)


def _features(image: bytes, minimum: float) -> list[tuple[int, int, int]]:
    features = []
    # One corner per cell prevents a single textured object dominating coverage.
    for cell_y in range(3):
        for cell_x in range(4):
            best = (minimum, 0, 0)
            for y in range(max(9, cell_y * 21), min(55, (cell_y + 1) * 21), 2):
                for x in range(max(9, cell_x * 24), min(87, (cell_x + 1) * 24), 2):
                    xx = yy = xy = 0
                    for dy in (-1, 0, 1):
                        for dx in (-1, 0, 1):
                            index = (y + dy) * _WIDTH + x + dx
                            gx = (image[index + 1] - image[index - 1]) / 2
                            gy = (image[index + _WIDTH] - image[index - _WIDTH]) / 2
                            xx += gx * gx
                            yy += gy * gy
                            xy += gx * gy
                    eigenvalue = (xx + yy - math.hypot(xx - yy, 2 * xy)) / 18
                    if eigenvalue > best[0]:
                        best = (eigenvalue, x, y)
            if best[0] > minimum:
                features.append((best[1], best[2], cell_y * 4 + cell_x))
    return features


def _match(
    patch: tuple[float, ...], image: bytes, x: int, y: int, thresholds: dict
) -> tuple[int, int] | None:
    matches = []
    for target_y in range(max(2, y - _SEARCH), min(_HEIGHT - 3, y + _SEARCH) + 1):
        for target_x in range(max(2, x - _SEARCH), min(_WIDTH - 3, x + _SEARCH) + 1):
            candidate = _patch(image, target_x, target_y)
            error = sum(abs(a - b) for a, b in zip(patch, candidate)) / 25
            matches.append((error, target_x, target_y))
    matches.sort()
    error, target_x, target_y = matches[0]
    if error > thresholds["max_patch_error"]:
        return None
    # Ignore adjacent minima of the same feature, but reject ambiguous patches.
    alternatives = [
        value
        for value, other_x, other_y in matches[1:]
        if abs(other_x - target_x) > 1 or abs(other_y - target_y) > 1
    ]
    if min(alternatives) <= (
        error * thresholds["ambiguity_ratio"] + thresholds["ambiguity_margin"]
    ):
        return None
    return target_x, target_y


def _tracks(
    reference: bytes,
    image: bytes,
    features: list,
    thresholds: dict,
    check_cancelled: Callable[[], None],
) -> list[tuple[float, int]]:
    tracks = []
    for x, y, cell in features:
        check_cancelled()
        target = _match(_patch(reference, x, y), image, x, y, thresholds)
        if target is None:
            continue
        target_x, target_y = target
        back = _match(_patch(image, target_x, target_y), reference, *target, thresholds)
        if (
            back is not None
            and math.hypot(back[0] - x, back[1] - y)
            <= thresholds["forward_backward_pixels"]
        ):
            tracks.append((math.hypot(target_x - x, target_y - y), cell))
    return tracks


def _statistics(image: bytes) -> tuple[float, float, float, list[float]]:
    histogram = [0] * 16
    for pixel in image:
        histogram[pixel // 16] += 1
    laplacians = [
        image[index - 1]
        + image[index + 1]
        + image[index - _WIDTH]
        + image[index + _WIDTH]
        - 4 * image[index]
        for y in range(1, _HEIGHT - 1)
        for x in range(1, _WIDTH - 1)
        for index in [y * _WIDTH + x]
    ]
    mean_laplacian = sum(laplacians) / len(laplacians)
    variance = sum((value - mean_laplacian) ** 2 for value in laplacians) / len(
        laplacians
    )
    return (
        sum(image) / len(image),
        variance,
        sum(value <= 4 or value >= 251 for value in image) / len(image),
        [count / len(image) for count in histogram],
    )


def analyze(images: Sequence[bytes], check_cancelled: Callable[[], None]) -> list[dict]:
    """Analyze bounded GRAY8 frames, reporting every candidate decision.

    A tracking break or suspected cut is diagnostic and must stop the caller
    from publishing a single-camera segment. The caller binds source PTS and
    final full-resolution image hashes to these decisions.
    """
    if not 1 <= len(images) <= MAX_CANDIDATES or any(
        len(image) != _WIDTH * _HEIGHT for image in images
    ):
        raise ValueError("Quality analysis requires 1..480 GRAY8 96x64 frames")
    thresholds = policy()["thresholds"]
    decisions = []
    seen = set()
    reference = None
    reference_features = []
    previous_histogram = previous_mean = None
    for image in images:
        check_cancelled()
        digest = hashlib.sha256(image).hexdigest()
        mean, blur, clipped, histogram = _statistics(image)
        features = _features(image, thresholds["min_corner_eigenvalue"])
        tracks = (
            _tracks(reference, image, reference_features, thresholds, check_cancelled)
            if reference is not None
            else []
        )
        distance = (
            sum(abs(a - b) for a, b in zip(histogram, previous_histogram)) / 2
            if previous_histogram is not None
            else 0.0
        )
        displacement = median([track[0] for track in tracks]) if tracks else 0.0
        cells = len({track[1] for track in tracks})
        if (
            not thresholds["min_mean"] <= mean <= thresholds["max_mean"]
            or clipped > thresholds["max_clipped_fraction"]
        ):
            reason = "exposure"
        elif blur < thresholds["min_laplacian_variance"] or len(features) < 4:
            reason = "blur_or_texture"
        elif digest in seen:
            reason = "repeated"
        elif (
            previous_mean is not None
            and abs(mean - previous_mean) > thresholds["max_exposure_change"]
        ):
            reason = "exposure_change"
        elif reference is None:
            reason = "seed"
        elif cells < thresholds["min_tracked_cells"]:
            reason = (
                "suspected_cut"
                if distance > thresholds["cut_histogram_distance"]
                else "tracking_break"
            )
        elif displacement < thresholds["min_displacement_px"]:
            reason = "low_motion"
        else:
            reason = "motion_coverage"
        selected = reason in ("seed", "motion_coverage")
        decisions.append(
            {
                "thumbnail_sha256": digest,
                "selected": selected,
                "reason": reason,
                "metrics": {
                    "mean_luma": mean,
                    "laplacian_variance": blur,
                    "clipped_fraction": clipped,
                    "feature_count": len(features),
                    "tracked_cells": cells,
                    "displacement_px": displacement,
                    "histogram_distance": distance,
                },
            }
        )
        seen.add(digest)
        previous_histogram, previous_mean = histogram, mean
        if selected:
            reference, reference_features = image, features
    return decisions
