#!/usr/bin/env python3
"""Native CUDA regression against an independent double-precision SSIM formula."""

from pathlib import Path
import sys

import numpy as np
from scipy.signal import convolve2d
import torch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "workers" / "gsplat"))
from losses import gaussian_ssim


def reference(left, right):
    positions = np.arange(-5, 6, dtype=np.float64)
    window = np.exp(-(positions**2) / (2 * 1.5**2))
    window /= window.sum()
    kernel = np.outer(window, window)
    values = []
    for channel in range(3):
        x, y = left[channel].astype(np.float64), right[channel].astype(np.float64)
        mx = convolve2d(x, kernel, mode="valid")
        my = convolve2d(y, kernel, mode="valid")
        vx = convolve2d(x * x, kernel, mode="valid") - mx * mx
        vy = convolve2d(y * y, kernel, mode="valid") - my * my
        cross = convolve2d(x * y, kernel, mode="valid") - mx * my
        values.append(
            (
                (2 * mx * my + 0.01**2)
                * (2 * cross + 0.03**2)
                / ((mx * mx + my * my + 0.01**2) * (vx + vy + 0.03**2))
            ).mean()
        )
    return float(np.mean(values))


def main():
    if not torch.cuda.is_available():
        raise RuntimeError("CUDA unavailable; this check is not a pass")
    # Exercise the default host setting that triggered the real training defect.
    torch.backends.cudnn.allow_tf32 = True
    rng = np.random.default_rng(42)
    for level, contrast, size in ((0.9, 0.002, (513, 769)), (0.5, 0.1, (64, 80))):
        left = np.clip(level + rng.normal(0, contrast, (3, *size)), 0, 1).astype(
            np.float32
        )
        right = np.clip(left + rng.normal(0, contrast, left.shape), 0, 1).astype(
            np.float32
        )
        expected = reference(left, right)
        x = torch.tensor(left[None], device="cuda", requires_grad=True)
        y = torch.tensor(right[None], device="cuda")
        actual = gaussian_ssim(x, y)
        assert abs(actual.item() - expected) < 5e-4, (actual.item(), expected)
        actual.backward()
        assert torch.isfinite(x.grad).all().item()
        assert -1.001 <= actual.item() <= 1.001
        print(
            f"Gaussian SSIM: shape={size}, actual={actual.item():.8f}, double={expected:.8f}"
        )
    print("SSIM numerical regression passed on", torch.cuda.get_device_name())


if __name__ == "__main__":
    main()
