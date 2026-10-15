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
import re
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
    origin = np.asarray(data.get("render_origin", [0, 0, 0]), dtype=np.float64)
    position = tensor(np.asarray(view["position"]) - origin)
    means, quats, scales, opacities, colors = [], [], [], [], []
    for chunk in data["chunks"]:
        q = tensor(chunk["rotation"])
        splats = chunk.get("rows")
        if splats is None:
            splats = np.array([[*s["mean"], *s["scale"], *s["rotation"], s["opacity"],
                                *s["sh"], s["sh_degree"]] for s in chunk["splats"]], dtype=np.float32)
        local = tensor(splats[:, :3])
        world = rotate(q, local * chunk["scale"]) + tensor(chunk["translation"])
        if "render_origin" in data:
            # Both renderers consume the same explicitly declared floating
            # origin and float32 world payload, retaining every original splat.
            local64 = splats[:, :3].astype(np.float64) * chunk["scale"]
            q64 = np.asarray(chunk["rotation"], dtype=np.float64)
            twice = 2 * np.cross(q64[1:], local64)
            world64 = local64 + q64[0] * twice + np.cross(q64[1:], twice)
            world = tensor(world64 + np.asarray(chunk["translation"]) - origin)
        means.append(world)
        quats.append(multiply(q, tensor(splats[:, 6:10])))
        scales.append(tensor(splats[:, 3:6]) * chunk["scale"])
        opacities.append(tensor(splats[:, 10]))
        inverse = q * tensor([1, -1, -1, -1])
        directions = rotate(inverse, world - position)
        coefficients = tensor(splats[:, 11:59]).reshape(-1, 16, 3)
        assert np.all(splats[:, 59] == 3)
        # Evaluate with pinned gsplat SH in the chunk's local basis, preserving
        # its captured radiance under chunk transforms. No fitted correction.
        colors.append((spherical_harmonics(3, directions, coefficients) + 0.5).clamp_min(0))
    basis = tensor([view["right"], view["down"], view["forward"]])
    viewmat = torch.eye(4, dtype=torch.float32, device="cuda")
    viewmat[:3, :3] = basis
    viewmat[:3, 3] = -(basis @ position)
    if "render_origin" in data:
        basis64 = np.asarray([view["right"], view["down"], view["forward"]])
        viewmat[:3, 3] = tensor(-(basis64 @ (np.asarray(view["position"]) - origin)))
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


def parse_fit_indices(selection, frame_count):
    if (type(frame_count) is not int or not 3 <= frame_count <= 120 or
            not isinstance(selection, str) or not 1 <= len(selection) <= 479):
        raise ValueError("fit index selection exceeds diagnostic budget")
    tokens = selection.split(",")
    if any(re.fullmatch(r"0|[1-9][0-9]{0,2}", token) is None for token in tokens):
        raise ValueError("invalid fit index selection")
    indices = [int(token) for token in tokens]
    if len(set(indices)) != len(indices) or any(index >= frame_count for index in indices):
        raise ValueError("invalid or duplicate fit index selection")
    return indices


def load_real(root, data, fit_indices=None):
    """Explicit real diagnostic profile; bounded, hashed portable binary rows."""
    def check(condition, message):
        if not condition:
            raise ValueError(message)

    def metadata(name, digest):
        path = root / name
        check(path.stat().st_size <= 4 * 1024**2, "fit metadata exceeds budget")
        check(sha256(path) == digest, "fit metadata checksum mismatch")
        return json.loads(path.read_text())

    check(data["schema"] == 2 and data["label"].startswith("real calibrated"),
          "explicit real diagnostic profile required")
    check(data["row_format"] == "little-endian float32[60]: mean3,scale3,wxyz4,opacity,sh48,degree",
          "unknown binary row profile")
    check(all(type(data[k]) is int and 0 < data[k] <= 4096 for k in ("width", "height")),
          "real framebuffer exceeds budget")
    identity = data["identity"]
    config = metadata("fit-input.json", identity["fit_input_sha256"])
    result = metadata("fit-result.json", identity["fit_result_sha256"])
    digest = hashlib.sha256(json.dumps(config, sort_keys=True).encode()).hexdigest()
    check(digest == identity["fit_identity"] == result["identity"], "fit configuration identity mismatch")
    check(config["source_sha256"] == identity["source_sha256"] == result["source_sha256"] and
          result["ply_sha256"] == identity["ply_sha256"] and result["status"] == "complete" and
          config["color_domain"] == "srgb" and config["sh_degree"] == 3 and
          config["rasterize_mode"] == "classic" and
          config["dataset"]["world_transform"] == "native COLMAP; canonical chunk applies Rx(pi)",
          "incompatible completed fit profile")
    check(type(data["splat_count"]) is int and 0 < data["splat_count"] <= 1000000 and
          data["splat_count"] == result["splats"], "full real splat count mismatch")
    check(0 < len(data["chunks"]) <= 1024, "chunk budget exceeded")
    total = 0
    files = set()
    for chunk in data["chunks"]:
        count = chunk["count"]
        check(type(count) is int and 0 < count <= data["splat_count"] - total, "chunk count exceeds budget")
        total += count
        name = chunk["file"]
        check(isinstance(name, str) and Path(name).name == name and name not in files,
              "invalid or repeated diagnostic asset path")
        files.add(name)
        path = (root / name).resolve(strict=True)
        check(path.parent == root and path.stat().st_size == count * 240, "binary asset size/path mismatch")
        check(sha256(path) == chunk["sha256"], "binary asset checksum mismatch")
        rows = np.fromfile(path, dtype="<f4").reshape(count, 60)
        check(np.isfinite(rows).all() and (rows[:, 3:6] > 0).all() and
              ((rows[:, 10] >= 0) & (rows[:, 10] <= 1)).all() and (rows[:, 59] == 3).all() and
              np.allclose((rows[:, 6:10] ** 2).sum(1), 1, atol=1e-4, rtol=0),
              "invalid real splat values/profile")
        rotation = np.asarray(chunk["rotation"], dtype=np.float64)
        translation = np.asarray(chunk["translation"], dtype=np.float64)
        check(rotation.shape == (4,) and translation.shape == (3,) and
              np.isfinite(rotation).all() and np.isfinite(translation).all() and
              abs(np.dot(rotation, rotation) - 1) < 1e-4 and
              math.isfinite(chunk["scale"]) and chunk["scale"] > 0, "invalid chunk transform")
        chunk["rows"] = rows
    check(total == data["splat_count"], "real scene was trimmed")
    frames = config["dataset"]["frames"]
    check(3 <= len(frames) <= 120, "fit view budget mismatch")
    if fit_indices is None:
        check("fit_frame_indices" not in data, "explicit fit index selection is required")
        indices = [0, len(frames) // 2, len(frames) - 1]
        names = ["fit_first", "fit_middle", "fit_last"]
    else:
        indices = parse_fit_indices(fit_indices, len(frames))
        recorded = data.get("fit_frame_indices")
        check(isinstance(recorded, list) and all(type(i) is int for i in recorded) and
              recorded == indices, "fit index selection differs from requested cameras")
        names = [f"fit_index_{index}" for index in indices]
    check(len(data["views"]) == len(indices), "fit view budget mismatch")
    canonical = np.diag([1., -1., -1.])
    for view, index, name in zip(data["views"], indices, names):
        frame = frames[index]
        check(view["name"] == name and type(view["fit_frame_index"]) is int and view["fit_frame_index"] == index and
              view["fit_frame_name"] == frame["name"], "fit view selection mismatch")
        matrix = np.asarray(frame["view"], dtype=np.float64)
        intrinsic = np.asarray(frame["intrinsic"], dtype=np.float64)
        check(matrix.shape == (4, 4) and intrinsic.shape == (3, 3) and
              np.isfinite(matrix).all() and np.isfinite(intrinsic).all(), "invalid fit camera")
        basis = matrix[:3, :3] @ canonical
        position = -np.linalg.solve(basis, matrix[:3, 3])
        check(np.allclose([view["right"], view["down"], view["forward"]], basis, atol=1e-10, rtol=0) and
              np.allclose(view["position"], position, atol=1e-10, rtol=0) and
              np.allclose(view["intrinsics"], [intrinsic[0, 0], intrinsic[1, 1],
                                             intrinsic[0, 2] - 0.5, intrinsic[1, 2] - 0.5], atol=1e-10, rtol=0) and
              frame["width"] == data["width"] and frame["height"] == data["height"],
              "native diagnostic camera differs from original fit")
    check(np.array_equal(data["render_origin"], data["views"][0]["position"]),
          "render origin differs from native admission camera")
    return total


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--real", action="store_true", help="admit the explicit bounded real-asset profile")
    parser.add_argument("--fit-indices", help="required matching explicit zero-based fit camera selection")
    args = parser.parse_args()
    if args.fit_indices is not None and not args.real:
        parser.error("--fit-indices requires --real")
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
    if args.real:
        count = load_real(root, data, args.fit_indices)
    else:
        assert data["schema"] == 1 and data["label"].startswith("synthetic")
        assert (data["width"], data["height"]) == (1280, 720)
        count = sum(len(c["splats"]) for c in data["chunks"])
        assert 0 < count <= 250000
    report = {"scope": ("real calibrated retained fit" if args.real else "synthetic fixed-camera diagnostic") +
                      "; no quality/performance certification",
              "fixture_sha256": sha256(fixture), "splat_count": count,
              "comparison_script_sha256": sha256(Path(__file__)),
              "ssim_script_sha256": sha256(Path(__file__).resolve().parents[2] / "workers" / "gsplat" / "losses.py"),
              "gsplat": version, "torch": torch.__version__, "cuda": torch.version.cuda,
              "device": torch.cuda.get_device_name(), "width": data["width"], "height": data["height"],
              "tolerance": {"min_psnr_db": MIN_PSNR, "max_error_levels": MAX_ERROR_LEVELS},
              "views": []}
    if args.real:
        report["identity"] = data["identity"]
        report["scene_content_sha256"] = data["scene_content_sha256"]
        report["assets_sha256"] = {c["file"]: c["sha256"] for c in data["chunks"]}
        report["fit_frame_indices"] = [v["fit_frame_index"] for v in data["views"]]
    distribution = importlib.metadata.distribution("gsplat")
    report["gsplat_files_sha256"] = {
        str(path): sha256(Path(distribution.locate_file(path)))
        for path in distribution.files
        if str(path).endswith((".so", "rendering.py", "_wrapper.py"))}
    with torch.no_grad():
        for view in data["views"]:
            name = view["name"]
            if not args.real:
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
            if args.real:
                record.update(fit_frame_index=view["fit_frame_index"], fit_frame_name=view["fit_frame_name"])
            report["views"].append(record)
            print(json.dumps(record), flush=True)
    report["passed"] = all(v["passed"] for v in report["views"])
    (root / "comparison.json").write_text(json.dumps(report, indent=2) + "\n")
    return 0 if report["passed"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
