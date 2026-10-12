"""Local gsplat 1.5.3 comparison for we_gpu_reference; no downloads/training.

Diagnostic tolerances are fixed before comparison: PSNR >=45 dB, max error <=8
8-bit levels. These allow raster rounding over a few overlapping contributors;
they are not the Q2/Q3/Q6 or platform performance gates.
"""
import argparse
import hashlib
import importlib.metadata
import json
import math
from pathlib import Path
import sys

import numpy as np
from PIL import Image
import torch
from gsplat import rasterization, spherical_harmonics

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "workers" / "gsplat"))
from losses import gaussian_ssim

MIN_PSNR = 45.0
MAX_ERROR_LEVELS = 8


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def rotate(q, vectors):
    axis = q[1:].expand_as(vectors)
    twice = 2 * torch.cross(axis, vectors, dim=-1)
    return vectors + q[0] * twice + torch.cross(axis, twice, dim=-1)


def multiply(a, b):
    w = a[0] * b[:, :1] - (a[1:] * b[:, 1:]).sum(-1, keepdim=True)
    xyz = a[0] * b[:, 1:] + b[:, :1] * a[1:] + torch.cross(
        a[1:].expand_as(b[:, 1:]), b[:, 1:], dim=-1)
    return torch.cat((w, xyz), -1)


def reference(data, view):
    tensor = lambda value: torch.tensor(value, dtype=torch.float32, device="cuda")
    position = tensor(view["position"])
    means, quats, scales, opacities, colors = [], [], [], [], []
    for chunk in data["chunks"]:
        q = tensor(chunk["rotation"])
        splats = chunk["splats"]
        local = tensor([s["mean"] for s in splats])
        world = rotate(q, local * chunk["scale"]) + tensor(chunk["translation"])
        means.append(world)
        quats.append(multiply(q, tensor([s["rotation"] for s in splats])))
        scales.append(tensor([s["scale"] for s in splats]) * chunk["scale"])
        opacities.append(tensor([s["opacity"] for s in splats]))
        inverse = q * tensor([1, -1, -1, -1])
        directions = rotate(inverse, world - position)
        coefficients = tensor([s["sh"] for s in splats]).reshape(-1, 16, 3)
        assert all(s["sh_degree"] == 3 for s in splats)
        # Evaluate with pinned gsplat SH in the chunk's local basis, preserving
        # its captured radiance under chunk transforms. No fitted correction.
        colors.append((spherical_harmonics(3, directions, coefficients) + 0.5).clamp_min(0))
    basis = tensor([view["right"], view["down"], view["forward"]])
    viewmat = torch.eye(4, dtype=torch.float32, device="cuda")
    viewmat[:3, :3] = basis
    viewmat[:3, 3] = -(basis @ position)
    fx, fy, cx, cy = view["intrinsics"]
    intrinsic = tensor([[fx, 0, cx + 0.5], [0, fy, cy + 0.5], [0, 0, 1]])
    rendered, alpha, _ = rasterization(
        means=torch.cat(means), quats=torch.cat(quats), scales=torch.cat(scales),
        opacities=torch.cat(opacities), colors=torch.cat(colors),
        viewmats=viewmat[None], Ks=intrinsic[None], width=data["width"], height=data["height"],
        near_plane=view["near"],
        far_plane=view["far"], packed=True, rasterize_mode="classic", eps2d=0.3,
        radius_clip=0.0, tile_size=16, render_mode="RGB")
    # This pinned wheel rejects a [1,3] background in its packed wrapper (it
    # derives image_dims from [nnz,2]). Apply the same residual background to
    # the returned alpha without patching the dependency or switching paths.
    return (rendered[0] + (1 - alpha[0]) * tensor(data["background"])).clamp(0, 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    version = importlib.metadata.version("gsplat")
    if version != "1.5.3+pt24cu124" or torch.__version__ != "2.4.1+cu124":
        raise RuntimeError(f"pinned reference environment required: gsplat={version}, torch={torch.__version__}")
    if not torch.cuda.is_available():
        raise RuntimeError("native CUDA unavailable; this is not a passing comparison")
    root = args.directory.resolve()
    fixture = root / "fixture.json"
    if fixture.stat().st_size > 4 * 1024 * 1024:
        raise RuntimeError("reference fixture exceeds bounded diagnostic input")
    data = json.loads(fixture.read_text())
    assert data["schema"] == 1 and data["label"].startswith("synthetic")
    assert (data["width"], data["height"]) == (1280, 720)
    count = sum(len(c["splats"]) for c in data["chunks"])
    assert 0 < count <= 250000
    report = {"scope": "synthetic fixed-camera diagnostic; no quality/performance certification",
              "fixture_sha256": sha256(fixture), "splat_count": count,
              "comparison_script_sha256": sha256(Path(__file__)),
              "ssim_script_sha256": sha256(Path(__file__).resolve().parents[2] / "workers" / "gsplat" / "losses.py"),
              "gsplat": version, "torch": torch.__version__, "cuda": torch.version.cuda,
              "device": torch.cuda.get_device_name(), "width": data["width"], "height": data["height"],
              "tolerance": {"min_psnr_db": MIN_PSNR, "max_error_levels": MAX_ERROR_LEVELS},
              "views": []}
    distribution = importlib.metadata.distribution("gsplat")
    report["gsplat_files_sha256"] = {
        str(path): sha256(Path(distribution.locate_file(path)))
        for path in distribution.files
        if str(path).endswith((".so", "rendering.py", "_wrapper.py"))}
    with torch.no_grad():
        for view in data["views"]:
            name = view["name"]
            assert name in ("centered", "off_center", "rolled", "translated",
                            "orbit_left", "orbit_right", "close_up")
            rendered = reference(data, view)
            assert torch.isfinite(rendered).all().item(), "nonfinite reference render"
            floating = rendered.cpu().numpy()
            np.save(root / f"{name}-reference-float.npy", floating)
            quantized = np.rint(floating * 255).astype(np.uint8)
            Image.fromarray(quantized).save(root / f"{name}-reference.png")
            native_path = root / f"{name}.ppm"
            with Image.open(native_path) as image:
                assert image.size == (data["width"], data["height"])
                native = np.asarray(image.convert("RGB"))
            assert native.shape == quantized.shape
            Image.fromarray(native).save(root / f"{name}-native.png")
            error = np.abs(native.astype(np.float64) - quantized.astype(np.float64))
            Image.fromarray(np.clip(error * 32, 0, 255).astype(np.uint8)).save(root / f"{name}-error-x32.png")
            mse = np.mean((error / 255) ** 2)
            psnr = -10 * math.log10(mse) if mse else None
            x = torch.tensor(native.copy(), device="cuda").permute(2, 0, 1)[None].float() / 255
            y = torch.tensor(quantized.copy(), device="cuda").permute(2, 0, 1)[None].float() / 255
            ssim = gaussian_ssim(x, y).item()
            passed = (psnr is None or psnr >= MIN_PSNR) and error.max() <= MAX_ERROR_LEVELS
            record = {"name": name, "psnr_db": psnr, "ssim": ssim,
                      "max_error_levels": int(error.max()), "mean_error_levels": float(error.mean()),
                      "native_sha256": sha256(native_path), "passed": bool(passed)}
            report["views"].append(record)
            print(json.dumps(record), flush=True)
    report["passed"] = all(v["passed"] for v in report["views"])
    (root / "comparison.json").write_text(json.dumps(report, indent=2) + "\n")
    return 0 if report["passed"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
