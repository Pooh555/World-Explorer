"""Display-encoded RGB SSIM: Gaussian 11x11, sigma 1.5, valid crop."""

import torch
import torch.nn.functional as functional


def gaussian_ssim(left, right):
    if left.shape != right.shape or left.ndim != 4 or left.shape[1] != 3:
        raise ValueError("SSIM expects matching NCHW RGB images")
    if min(left.shape[-2:]) < 11:
        raise ValueError("SSIM image is smaller than its fixed window")
    positions = torch.arange(-5, 6, device=left.device, dtype=left.dtype)
    kernel = torch.exp(-positions.square() / (2 * 1.5**2))
    kernel = kernel / kernel.sum()
    moments = torch.cat(
        (left, right, left.square(), right.square(), left * right), dim=1
    )
    horizontal = kernel.reshape(1, 1, 1, 11).expand(15, 1, 1, 11)
    vertical = kernel.reshape(1, 1, 11, 1).expand(15, 1, 11, 1)
    # TF32's truncated mantissa corrupts the subtraction E[x²]-E[x]² in
    # low-contrast patches. Preserve float precision before forming variances.
    with torch.backends.cudnn.flags(allow_tf32=False):
        filtered = functional.conv2d(moments, horizontal, groups=15)
        filtered = functional.conv2d(filtered, vertical, groups=15)
    mean_left, mean_right, second_left, second_right, cross = filtered.split(3, dim=1)
    variance_left = second_left - mean_left.square()
    variance_right = second_right - mean_right.square()
    covariance = cross - mean_left * mean_right
    numerator = (2 * mean_left * mean_right + 0.01**2) * (2 * covariance + 0.03**2)
    denominator = (mean_left.square() + mean_right.square() + 0.01**2) * (
        variance_left + variance_right + 0.03**2
    )
    return (numerator / denominator).mean()
