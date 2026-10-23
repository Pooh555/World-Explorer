"""Conservative image-scale preflight; zoom and forward motion are ambiguous.

For a frontoparallel plane, image scale is proportional to focal length/depth.
RGB alone cannot decide which changed. An alarm rejects the single-calibration
job; an absence of alarms does not certify fixed intrinsics or 3D coverage.
"""

import importlib.util
import math
from collections.abc import Callable, Sequence
from pathlib import Path
from typing import Final

__all__ = ["MAX_CANDIDATES", "PROFILE", "analyze", "policy"]

# Resolve the dependency beside this file, independently of the host's sys.path.
_SPEC: Final = importlib.util.spec_from_file_location(
    "we_calibration_tracking", Path(__file__).with_name("frame_tracking.py")
)
_TRACKING: Final = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(_TRACKING)
MAX_CANDIDATES: Final = _TRACKING.MAX_CANDIDATES
PROFILE: Final = "quality_keyframes_candidate_v3"


def policy() -> dict:
    """Return the frozen candidate recipe; all v2 selection minimums remain."""
    result = _TRACKING.policy()
    result |= {
        "metric_version": "gray96_persistent_scale_preflight_v3",
        "threshold_version": "candidate_3_0",
        "lens_change_detection": "ambiguous_image_scale_preflight",
        "calibration_preflight": {
            "model": "centered_least_squares_similarity",
            "min_points": 8,
            "min_cells": 4,
            "min_span_x": 40,
            "min_span_y": 24,
            "min_scale": 0.5,
            "max_scale": 2,
            "max_rms_px": 0.5,
            "max_pair_scale_ratio": 1.04,
            "max_chain_scale_ratio": 1.1,
            "alarm": "reject_single_calibration_job; zoom_or_forward_motion_ambiguous",
            "unsupported": "insufficient_support; no fixed_intrinsics_claim",
        },
    }
    return result


def _measure(reference: int | None, previous: Sequence, continued: Sequence) -> dict:
    result = {
        "reference": reference,
        "status": "unassessed" if reference is None else "insufficient_support",
        "support_points": len(continued),
        "support_cells": 0,
        "span_x": 0.0,
        "span_y": 0.0,
        "scale": None,
        "rms_px": None,
        "chain_start": None,
        "cumulative_log_scale": None,
    }
    if not continued:
        return result
    sources = {(birth, bx, by): (x, y) for birth, bx, by, x, y in previous}
    pairs = [(*sources[(birth, bx, by)], x, y) for birth, bx, by, x, y in continued]
    result["support_cells"] = len({_TRACKING._cell(x, y) for x, y, _, _ in pairs})
    for axis, name in ((0, "span_x"), (1, "span_y")):
        values = [pair[axis] for pair in pairs]
        result[name] = max(values) - min(values)
    means = [sum(pair[axis] for pair in pairs) / len(pairs) for axis in range(4)]
    centered = [tuple(a - b for a, b in zip(pair, means)) for pair in pairs]
    energy = sum(x * x + y * y for x, y, _, _ in centered)
    if energy <= 1e-6:
        return result
    real = sum(x * u + y * v for x, y, u, v in centered) / energy
    imaginary = sum(x * v - y * u for x, y, u, v in centered) / energy
    result["scale"] = math.hypot(real, imaginary)
    result["rms_px"] = math.sqrt(
        sum(
            (u - real * x + imaginary * y) ** 2 + (v - imaginary * x - real * y) ** 2
            for x, y, u, v in centered
        )
        / len(centered)
    )
    return result


def analyze(images: Sequence[bytes], check_cancelled: Callable[[], None]) -> list[dict]:
    """Attach scale observations without changing the underlying v2 decisions."""
    thresholds = policy()["calibration_preflight"]
    observations = []

    def observe(index, reference, previous, continued) -> None:
        check_cancelled()
        observation = _measure(reference, previous, continued)
        if (
            reference is not None
            and observation["support_points"] >= thresholds["min_points"]
            and observation["support_cells"] >= thresholds["min_cells"]
            and observation["span_x"] >= thresholds["min_span_x"]
            and observation["span_y"] >= thresholds["min_span_y"]
            and observation["scale"] is not None
            and thresholds["min_scale"]
            <= observation["scale"]
            <= thresholds["max_scale"]
            and observation["rms_px"] <= thresholds["max_rms_px"]
        ):
            prior = observations[reference]
            connected = prior["status"] == "assessed"
            cumulative = math.log(observation["scale"]) + (
                prior["cumulative_log_scale"] if connected else 0.0
            )
            observation |= {
                "status": (
                    "ambiguous_scale_change"
                    if abs(math.log(observation["scale"]))
                    >= math.log(thresholds["max_pair_scale_ratio"])
                    or abs(cumulative) >= math.log(thresholds["max_chain_scale_ratio"])
                    else "assessed"
                ),
                "chain_start": prior["chain_start"] if connected else reference,
                "cumulative_log_scale": cumulative,
            }
        observations.append(observation)

    rows = _TRACKING.analyze(images, check_cancelled, observe=observe)
    for row, observation in zip(rows, observations):
        row["calibration"] = observation
    return rows
