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
CLIPPING_LABEL = "synthetic inclusive clipping float neighbors v1; not a capture"


REBASING_LABEL = "synthetic floating origin translation v1; not a capture"
REBASING_OFFSETS = {"zero": [0, 0, 0], "positive": [2**30, -2**29, 2**28],
                   "negative": [-2**30, 2**29, -2**28]}


def rebasing_profile(case):
    """Independent fixed binary-grid input specification; never a fitted scene."""
    offset = np.asarray(REBASING_OFFSETS[case], dtype=np.float64)
    chunks = []
    for group, count in enumerate((12, 4)):
        splats = []
        for index in range(count):
            sh = np.zeros(48, dtype=np.float32)
            for channel in range(3):
                color = np.float32(.125 + ((index + channel) % 4) * .1875)
                sh[channel] = (color - np.float32(.5)) / np.float32(.2820947918)
                for coefficient in range(1, 16):
                    sh[coefficient * 3 + channel] = ((index + coefficient + channel) % 3 - 1) * .015625
            splats.append({"mean": [(index % 4 - 1.5) * .5, (index // 4 - 1) * .5, -(index % 3) * .25],
                           "scale": [.25, .125, .0625], "rotation": [1, 0, 0, 0],
                           "opacity": .625, "sh_degree": 3, "sh": sh.astype(float).tolist()})
        chunks.append({"rotation": [1, 0, 0, 0] if group == 0 else [0, 1, 0, 0],
                       "translation": (offset + ([0, 0, 0] if group == 0 else [.5, -.25, 1])).tolist(),
                       "scale": 1 if group == 0 else .5, "splats": splats})
    views = []
    for name in ("centered", "off_center", "rolled", "translated"):
        rolled = name in ("rolled", "translated")
        views.append({"name": name,
                      "position": (offset + ([-.25, .125, 6.5] if name == "translated" else [0, 0, 6])).tolist(),
                      "right": [math.cos(.37), -math.sin(.37), 0] if rolled else [1, 0, 0],
                      "down": [-math.sin(.37), -math.cos(.37), 0] if rolled else [0, -1, 0],
                      "forward": [0, 0, -1],
                      "intrinsics": [900, 840, 639.5, 359.5] if name == "centered" else [930, 790, 481.25, 302.75],
                      "near": float(np.float32(.01)), "far": 20})
    return {"schema": 1, "label": REBASING_LABEL, "width": 1280, "height": 720,
            "background": np.asarray([.045, .051, .063], dtype=np.float32).astype(float).tolist(),
            "chunks": chunks, "views": views, "rebasing_case": case,
            "world_offset": offset.tolist(), "render_origin": views[0]["position"], "rebase_excursion": 16384}


def load_rebasing(data, case):
    def finite_numbers(value):
        if isinstance(value, dict):
            return all(finite_numbers(v) for v in value.values())
        if isinstance(value, list):
            return all(finite_numbers(v) for v in value)
        return isinstance(value, str) or (type(value) in (int, float) and math.isfinite(value))
    payload = dict(data)
    digest = payload.pop("scene_content_sha256", None)
    if (case not in REBASING_OFFSETS or not finite_numbers(payload) or
            not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{64}", digest) or
            payload != rebasing_profile(case)):
        raise ValueError("rebasing fixture differs from fixed diagnostic profile")
    return 16


def load_rebasing_baseline(root):
    """Bind every baseline image to its passing report and this comparator."""
    fixture = root / "fixture.json"
    report_path = root / "comparison.json"
    if fixture.stat().st_size > 4 * 1024**2 or report_path.stat().st_size > 1024**2:
        raise ValueError("rebasing baseline metadata exceeds budget")
    load_rebasing(json.loads(fixture.read_text()), "zero")
    report = json.loads(report_path.read_text())
    if (report.get("passed") is not True or report.get("rebasing_case") != "zero" or
            report.get("fixture_sha256") != sha256(fixture) or
            report.get("comparison_script_sha256") != sha256(Path(__file__)) or
            report.get("tolerance") != {"min_psnr_db": MIN_PSNR, "max_error_levels": MAX_ERROR_LEVELS} or
            [v.get("name") for v in report.get("views", [])] != ["centered", "off_center", "rolled", "translated"]):
        raise ValueError("rebasing baseline is not a matching passing zero report")
    for view in report["views"]:
        name = view["name"]
        if (view.get("passed") is not True or view.get("native_sha256") != sha256(root / f"{name}.ppm") or
                view.get("reference_sha256") != sha256(root / f"{name}-reference.png")):
            raise ValueError("rebasing baseline readback checksum mismatch")
    return report


def image_comparison(left, right):
    """Fixed RGB8 limits also apply to joint-translation invariance."""
    if left.shape != (720, 1280, 3) or right.shape != left.shape or left.dtype != np.uint8 or right.dtype != np.uint8:
        raise ValueError("rebasing readback must be full RGB8")
    error = np.abs(left.astype(np.float64) - right.astype(np.float64))
    mse = np.mean((error / 255) ** 2)
    psnr = -10 * math.log10(mse) if mse else None
    return {"psnr_db": psnr, "max_error_levels": int(error.max()),
            "passed": bool((psnr is None or psnr >= MIN_PSNR) and error.max() <= MAX_ERROR_LEVELS)}


def load_clipping(data):
    """Admit exactly the six predeclared one-ULP boundary contributions."""
    def check(condition):
        if not condition:
            raise ValueError("clipping boundary fixture differs from fixed diagnostic profile")
    def numbers(values, count):
        return (isinstance(values, list) and len(values) == count and
                all(type(v) in (int, float) and math.isfinite(v) for v in values))
    check(type(data.get("schema")) is int and data["schema"] == 1 and data.get("label") == CLIPPING_LABEL)
    check((data.get("width"), data.get("height")) == (1280, 720))
    check(numbers(data["background"], 3))
    check(np.array_equal(np.asarray(data["background"], dtype=np.float32),
                         np.array([.045, .051, .063], dtype=np.float32)))
    check(isinstance(data["views"], list) and isinstance(data["chunks"], list) and
          len(data["views"]) == 1 and len(data["chunks"]) == 1)
    view = data["views"][0]
    for key, expected in {"name": "clipping_boundaries", "position": [0, 0, 0],
                          "right": [1, 0, 0], "down": [0, -1, 0], "forward": [0, 0, -1],
                          "near": 1.0, "far": 8.0, "intrinsics": [900, 840, 639.5, 359.5]}.items():
        value = view.get(key)
        check(value == expected)
        if isinstance(expected, list):
            check(numbers(value, len(expected)))
        elif type(expected) is float:
            check(type(value) in (int, float))
    chunk = data["chunks"][0]
    check(numbers(chunk["rotation"], 4) and numbers(chunk["translation"], 3) and
          type(chunk["scale"]) in (int, float))
    check(chunk["rotation"] == [1, 0, 0, 0] and chunk["translation"] == [0, 0, 0] and chunk["scale"] == 1)
    check("render_origin" not in data and len(chunk["splats"]) == 6)
    depths = np.array([0x3f7fffff, 0x3f800000, 0x3f800001,
                       0x40ffffff, 0x41000000, 0x41000001], dtype=np.uint32).view(np.float32)
    for index, (splat, depth) in enumerate(zip(chunk["splats"], depths)):
        check(numbers(splat["mean"], 3) and numbers(splat["scale"], 3) and numbers(splat["rotation"], 4) and
              numbers(splat["sh"], 48) and type(splat["opacity"]) in (int, float) and
              math.isfinite(splat["opacity"]) and type(splat["sh_degree"]) is int)
        mean = np.asarray(splat["mean"], dtype=np.float32)
        expected = np.array([depth * ((index % 3) - 1) * .5,
                             depth * (.125 if index < 3 else -.125), -depth], dtype=np.float32)
        check(mean.shape == (3,) and np.array_equal(mean, expected))
        scale = np.asarray(splat["scale"], dtype=np.float32)
        check(scale.shape == (3,) and np.array_equal(scale, [depth / np.float32(64)] * 3))
        check(splat["rotation"] == [1, 0, 0, 0] and splat["sh_degree"] == 3 and
              np.float32(splat["opacity"]) == np.float32(.8))
        sh = np.asarray(splat["sh"], dtype=np.float32)
        check(sh.shape == (48,) and np.isfinite(sh).all() and np.all(sh[3:] == 0))
        color = np.array([.85, .35, .15] if index < 3 else [.15, .45, .85], dtype=np.float32)
        check(np.array_equal(sh[:3], (color - np.float32(.5)) / np.float32(.2820947918)))
    return 6


def clipping_regions(image, background):
    """Independent presence checks: outside slots stay background; equality stays visible."""
    if image.shape != (720, 1280, 3) or image.dtype != np.uint8:
        raise ValueError("clipping readback must be full RGB8")
    bg = np.asarray(background, dtype=np.float64)
    if bg.shape != (3,) or not np.isfinite(bg).all() or (bg < 0).any() or (bg > 1).any():
        raise ValueError("invalid clipping background")
    bg = np.rint(bg * 255).astype(np.int16)
    checks = []
    for index, visible in enumerate([False, True, True, True, True, False]):
        x = 190 + 450 * (index % 3)
        y = 255 if index < 3 else 465
        patch = image[y-8:y+8, x-8:x+8].astype(np.int16)
        delta = int(np.max(np.abs(patch - bg)))
        checks.append({"slot": index, "expected_visible": visible, "maximum_background_delta": delta,
                       "passed": delta > 32 if visible else delta <= 1})
    return checks


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
    parser.add_argument("--clipping", action="store_true", help="require the fixed synthetic clipping boundary profile")
    parser.add_argument("--rebasing", choices=tuple(REBASING_OFFSETS), help="require the fixed synthetic floating-origin profile")
    parser.add_argument("--baseline", type=Path, help="passing zero-offset comparison required for translated profiles")
    args = parser.parse_args()
    if sum((args.real, args.clipping, args.rebasing is not None)) > 1:
        parser.error("--real, --clipping and --rebasing are exclusive")
    if (args.baseline is not None) != (args.rebasing in ("positive", "negative")):
        parser.error("--baseline is required only for positive/negative rebasing")
    if args.fit_indices is not None and not args.real:
        parser.error("--fit-indices requires --real")
    if args.clipping and args.real:
        parser.error("--clipping cannot combine with --real")
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
    baseline = load_rebasing_baseline(args.baseline.resolve()) if args.baseline else None
    if args.rebasing:
        count = load_rebasing(data, args.rebasing)
    elif args.real:
        count = load_real(root, data, args.fit_indices)
    elif args.clipping:
        count = load_clipping(data)
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
    if args.rebasing:
        report["rebasing_case"] = args.rebasing
        report["world_offset"] = data["world_offset"]
        if baseline:
            for key in ("gsplat", "torch", "cuda", "device", "gsplat_files_sha256", "ssim_script_sha256"):
                if baseline.get(key) != report[key]:
                    raise ValueError("rebasing baseline reference environment differs")
            report["baseline_comparison_sha256"] = sha256(args.baseline / "comparison.json")
    with torch.no_grad():
        for view in data["views"]:
            name = view["name"]
            if not args.real and not args.clipping:
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
            boundary_checks = None
            if args.clipping:
                boundary_checks = {"native": clipping_regions(native, data["background"]),
                                   "reference": clipping_regions(quantized, data["background"])}
                passed = passed and all(c["passed"] for rows in boundary_checks.values() for c in rows)
            record = {"name": name, "psnr_db": psnr, "ssim": ssim,
                      "max_error_levels": int(error.max()), "mean_error_levels": float(error.mean()),
                      "native_sha256": sha256(native_path), "passed": bool(passed)}
            if args.real:
                record.update(fit_frame_index=view["fit_frame_index"], fit_frame_name=view["fit_frame_name"])
            if boundary_checks is not None:
                record["clipping_regions"] = boundary_checks
            if args.rebasing:
                record["reference_sha256"] = sha256(root / f"{name}-reference.png")
                background = np.rint(np.asarray(data["background"]) * 255).astype(np.int16)
                record["nonblank"] = all(np.abs(pixels.astype(np.int16) - background).max() > 32
                                         for pixels in (native, quantized))
                passed = passed and record["nonblank"]
                if baseline:
                    checks = {}
                    for kind, pixels, suffix in (("native", native, ".ppm"), ("reference", quantized, "-reference.png")):
                        with Image.open(args.baseline / (name + suffix)) as image:
                            checks[kind] = image_comparison(pixels, np.asarray(image.convert("RGB")))
                    record["translation_invariance"] = checks
                    passed = passed and all(c["passed"] for c in checks.values())
                record["passed"] = bool(passed)
            report["views"].append(record)
            print(json.dumps(record), flush=True)
    report["passed"] = all(v["passed"] for v in report["views"])
    (root / "comparison.json").write_text(json.dumps(report, indent=2) + "\n")
    return 0 if report["passed"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
