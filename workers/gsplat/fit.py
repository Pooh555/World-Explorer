#!/usr/bin/env python3
"""Explicit local_gsplat_v1 development fit; never a Q2/Q3 certification."""

# Initialization/Adam settings follow gsplat 1.5.3 examples/simple_trainer.py.
# Copyright 2025 Nerfstudio Team (upstream attribution).
# Adapted under Apache-2.0; see third_party/notices/gsplat-LICENSE.txt.

import argparse
import hashlib
import importlib.metadata
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import we_protocol

import numpy as np
from PIL import Image
from scipy.spatial import cKDTree
import torch
import torch.nn.functional as functional
from gsplat import rasterization
from gsplat.strategy import DefaultStrategy

from dataset import capture_dataset
from losses import gaussian_ssim
from train import GSPLAT_COMMIT, deny_network, export_ascii, file_hash

RECIPE = "local_gsplat_v1"
STOP_REQUESTED = False


def request_stop(*_):
    global STOP_REQUESTED
    STOP_REQUESTED = True


def atomic_json(path, value):
    temporary = path.with_name(path.name + ".tmp")
    with temporary.open("w", encoding="utf-8") as stream:
        json.dump(value, stream, indent=2, sort_keys=True, allow_nan=False)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


def initialize(dataset):
    distances, _ = cKDTree(dataset.points).query(dataset.points, k=4, workers=4)
    radius = np.sqrt(np.mean(distances[:, 1:] ** 2, axis=1)).clip(1e-8)
    means = torch.from_numpy(dataset.points)
    colors = torch.from_numpy(dataset.colors)
    coefficients = torch.zeros((len(means), 16, 3))
    coefficients[:, 0] = (colors - 0.5) / 0.28209479177387814
    values = {
        "means": means,
        "scales": torch.from_numpy(np.log(radius)).float()[:, None].repeat(1, 3),
        "quats": torch.rand((len(means), 4)),
        "opacities": torch.logit(torch.full((len(means),), 0.1)),
        "sh0": coefficients[:, :1],
        "shN": coefficients[:, 1:],
    }
    params = torch.nn.ParameterDict(
        {name: torch.nn.Parameter(value.cuda()) for name, value in values.items()}
    )
    rates = {
        "means": 1.6e-4 * dataset.scene_scale,
        "scales": 5e-3,
        "quats": 1e-3,
        "opacities": 5e-2,
        "sh0": 2.5e-3,
        "shN": 2.5e-3 / 20,
    }
    optimizers = {
        name: torch.optim.Adam(
            [{"params": params[name], "lr": rate, "name": name}],
            eps=1e-15,
            betas=(0.9, 0.999),
        )
        for name, rate in rates.items()
    }
    return params, optimizers


class budget_strategy(DefaultStrategy):
    def __init__(self, maximum):
        super().__init__()
        self.maximum = maximum

    def _grow_gs(self, params, optimizers, state, step):
        gradients = state["grad2d"] / state["count"].clamp_min(1)
        high = gradients > self.grow_grad2d
        small = (
            params["scales"].exp().max(dim=-1).values
            <= self.grow_scale3d * state["scene_scale"]
        )
        duplicates = int((high & small).sum().item())
        splits = int((high & ~small).sum().item())
        # The default strategy allocates children before removing their parents.
        transient = len(params["means"]) + duplicates + 2 * splits
        if transient > self.maximum:
            raise RuntimeError(
                f"RESOURCE_LIMIT: densification needs {transient} transient splats; "
                f"configured maximum is {self.maximum}; no quality downgrade applied"
            )
        return super()._grow_gs(params, optimizers, state, step)


def render(params, record, degree):
    return rasterization(
        means=params["means"],
        quats=params["quats"],
        scales=params["scales"].exp(),
        opacities=params["opacities"].sigmoid(),
        colors=torch.cat((params["sh0"], params["shN"]), dim=1),
        viewmats=torch.from_numpy(record["view"]).cuda()[None],
        Ks=torch.from_numpy(record["intrinsic"]).cuda()[None],
        width=record["width"],
        height=record["height"],
        sh_degree=degree,
        packed=True,
        rasterize_mode="classic",
        near_plane=0.01,
        far_plane=1e10,
    )


def checkpoint(path, identity, params, optimizers, scheduler, state, step):
    temporary = path.with_name(path.name + ".tmp")
    value = {
        "identity": identity,
        "step": step,
        "splats": {name: tensor.detach() for name, tensor in params.items()},
        "optimizers": {
            name: optimizer.state_dict() for name, optimizer in optimizers.items()
        },
        "scheduler": scheduler.state_dict(),
        "strategy": state,
        "cpu_rng": torch.get_rng_state(),
        "cuda_rng": torch.cuda.get_rng_state(),
    }
    with temporary.open("wb") as stream:
        torch.save(value, stream)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


def fit(args):
    if importlib.metadata.version("gsplat").split("+")[0] != "1.5.3":
        raise RuntimeError("This development recipe requires gsplat 1.5.3")
    if torch.__version__ != "2.4.1+cu124":
        raise RuntimeError("Expected pinned PyTorch 2.4.1+cu124 development tuple")
    commit = subprocess.check_output(
        ["git", "-C", str(args.source), "rev-parse", "HEAD"], text=True
    ).strip()
    if (
        commit != GSPLAT_COMMIT
        or subprocess.run(
            ["git", "-C", str(args.source), "diff", "--quiet", "HEAD"]
        ).returncode
    ):
        raise RuntimeError("Pinned gsplat source identity does not match")
    deny_network()
    if not torch.cuda.is_available():
        raise RuntimeError("Native CUDA device unavailable; no remote fallback")
    torch.set_num_threads(4)
    torch.backends.cudnn.allow_tf32 = False
    torch.backends.cuda.matmul.allow_tf32 = False
    torch.manual_seed(42)
    torch.cuda.manual_seed_all(42)
    dataset = capture_dataset(args.data, args.max_image_dimension)
    if len(dataset.points) < 4 or len(dataset.points) > args.max_splats:
        raise RuntimeError(
            "Initial training-visible SfM points violate the splat budget"
        )
    manifest = dataset.manifest()
    recipe_sources = {
        path.name: file_hash(path) for path in Path(__file__).parent.glob("*.py")
    }
    configuration = {
        "recipe": RECIPE,
        "steps": args.steps,
        "max_splats": args.max_splats,
        "max_image_dimension": args.max_image_dimension,
        "seed": 42,
        "gsplat_commit": commit,
        "worker_sources": recipe_sources,
        "torch": torch.__version__,
        "gsplat": importlib.metadata.version("gsplat"),
        "dataset": manifest,
        "source_sha256": file_hash(args.video),
        "loss": "0.8 L1 + 0.2 Gaussian SSIM; window11 sigma1.5 valid crop",
        "color_domain": "srgb",
        "sh_degree": 3,
        "rasterize_mode": "classic",
        "tf32": False,
        "stage_timeout_seconds": 3600,
        "metric_crop": "five-pixel border for SSIM; full image PSNR",
        "lpips": "UNAVAILABLE: reviewed metric/backbone lock pending",
        "q2_qualified": False,
        "q3_qualified": False,
    }
    identity = hashlib.sha256(
        json.dumps(configuration, sort_keys=True).encode()
    ).hexdigest()
    atomic_json(args.output / "input_manifest.json", configuration)
    params, optimizers = initialize(dataset)
    scheduler = torch.optim.lr_scheduler.ExponentialLR(
        optimizers["means"], gamma=0.01 ** (1 / args.steps)
    )
    strategy = budget_strategy(args.max_splats)
    state = strategy.initialize_state(scene_scale=dataset.scene_scale)
    first_step = 0
    if args.resume:
        if args.resume.stat().st_size > 1024**3:
            raise RuntimeError("Checkpoint exceeds the trusted local checkpoint budget")
        saved = torch.load(args.resume, map_location="cuda", weights_only=True)
        if saved["identity"] != identity or not 0 <= saved["step"] <= args.steps:
            raise RuntimeError("Checkpoint recipe/input identity is incompatible")
        for name, value in saved["splats"].items():
            params[name] = torch.nn.Parameter(value)
            optimizers[name].param_groups[0]["params"] = [params[name]]
            optimizers[name].load_state_dict(saved["optimizers"][name])
        scheduler.load_state_dict(saved["scheduler"])
        state = saved["strategy"]
        torch.set_rng_state(saved["cpu_rng"].cpu())
        torch.cuda.set_rng_state(saved["cuda_rng"].cpu())
        first_step = saved["step"]
    strategy.check_sanity(params, optimizers)
    start = time.monotonic()
    checkpoint_path = args.output / "checkpoint.pt"
    with (args.output / "progress.jsonl").open("a", encoding="utf-8") as progress:
        for step in range(first_step, args.steps):
            if time.monotonic() - start > 3600:
                checkpoint(
                    checkpoint_path,
                    identity,
                    params,
                    optimizers,
                    scheduler,
                    state,
                    step,
                )
                raise RuntimeError(
                    "RESOURCE_LIMIT: one-hour fit budget reached; checkpoint retained"
                )
            if STOP_REQUESTED:
                checkpoint(
                    checkpoint_path,
                    identity,
                    params,
                    optimizers,
                    scheduler,
                    state,
                    step,
                )
                raise InterruptedError(
                    "Cancelled; compatible optimizer checkpoint retained"
                )
            index = dataset.train_indices[
                int(torch.randint(len(dataset.train_indices), (1,)).item())
            ]
            record = dataset.records[index]
            target = torch.from_numpy(dataset.image(index)).cuda().float()[None] / 255
            predicted, _, info = render(params, record, min(step // 1000, 3))
            strategy.step_pre_backward(params, optimizers, state, step, info)
            l1 = functional.l1_loss(predicted, target)
            similarity = gaussian_ssim(
                predicted.permute(0, 3, 1, 2), target.permute(0, 3, 1, 2)
            )
            if not -1.001 <= similarity.item() <= 1.001:
                raise RuntimeError(
                    "SSIM outside its valid numerical range; checkpoint retained"
                )
            loss = 0.8 * l1 + 0.2 * (1 - similarity)
            if not torch.isfinite(loss).item():
                raise RuntimeError("Non-finite loss; previous checkpoint retained")
            loss.backward()
            for optimizer in optimizers.values():
                optimizer.step()
                optimizer.zero_grad(set_to_none=True)
            scheduler.step()
            strategy.step_post_backward(
                params, optimizers, state, step, info, packed=True
            )
            if step % 100 == 0 or step + 1 == args.steps:
                item = {
                    "step": step + 1,
                    "loss": loss.item(),
                    "l1": l1.item(),
                    "ssim": similarity.item(),
                    "splats": len(params["means"]),
                    "elapsed_seconds": time.monotonic() - start,
                    "cuda_allocated_bytes": torch.cuda.memory_allocated(),
                    "cuda_peak_allocated_bytes": torch.cuda.max_memory_allocated(),
                    "cuda_peak_reserved_bytes": torch.cuda.max_memory_reserved(),
                }
                progress.write(json.dumps(item, allow_nan=False) + "\n")
                progress.flush()
                we_protocol.progress(
                    "appearance", json.dumps(item), item["elapsed_seconds"]
                )
            if (step + 1) % 500 == 0:
                checkpoint(
                    checkpoint_path,
                    identity,
                    params,
                    optimizers,
                    scheduler,
                    state,
                    step + 1,
                )
        checkpoint(
            checkpoint_path, identity, params, optimizers, scheduler, state, args.steps
        )
    fitting_seconds = time.monotonic() - start
    evaluation = []
    (args.output / "references").mkdir(exist_ok=True)
    with torch.no_grad():
        for index in dataset.holdout_indices:
            if STOP_REQUESTED:
                raise InterruptedError(
                    "Cancelled during evaluation; completed optimizer checkpoint retained"
                )
            record = dataset.records[index]
            predicted, _, _ = render(params, record, 3)
            target = torch.from_numpy(dataset.image(index)).cuda().float()[None] / 255
            mse = functional.mse_loss(predicted, target).item()
            ssim = gaussian_ssim(
                predicted.permute(0, 3, 1, 2), target.permute(0, 3, 1, 2)
            ).item()
            evaluation.append(
                {
                    "frame": record["name"],
                    "psnr_db": -10 * np.log10(max(mse, 1e-12)),
                    "ssim": ssim,
                    "lpips": None,
                }
            )
            Image.fromarray(
                (predicted[0].clamp(0, 1).cpu().numpy() * 255).round().astype(np.uint8)
            ).save(args.output / "references" / record["name"])
    if file_hash(args.video) != configuration["source_sha256"] or any(
        file_hash(record["path"]) != record["sha256"] for record in dataset.records
    ):
        raise RuntimeError("Capture inputs changed during fitting; no scene published")
    temporary_ply = args.output / "appearance.ply.tmp"
    export_ascii(params, temporary_ply, args.max_splats)
    ply_hash = file_hash(temporary_ply)
    assets = args.output / "assets"
    assets.mkdir(exist_ok=True)
    asset = assets / f"{ply_hash}.ply"
    os.replace(temporary_ply, asset)
    result = {
        "schema_version": 1,
        "status": "complete",
        "recipe": RECIPE,
        "identity": identity,
        "source_sha256": configuration["source_sha256"],
        "ply": str(asset.relative_to(args.output)),
        "ply_sha256": ply_hash,
        "steps": args.steps,
        "splats": len(params["means"]),
        "fitting_seconds_this_run": fitting_seconds,
        "gpu": torch.cuda.get_device_name(),
        "peak_cuda_allocated_bytes": torch.cuda.max_memory_allocated(),
        "peak_cuda_reserved_bytes": torch.cuda.max_memory_reserved(),
        "holdout": evaluation,
        "q2_qualified": False,
        "q3_qualified": False,
        "scale_status": "ARBITRARY",
        "geometry": "UNAVAILABLE",
        "walk": "UNAVAILABLE",
    }
    atomic_json(args.output / "result.json", result)
    return result


def main():
    we_protocol.configure(
        sys.argv,
        cancel_callback=request_stop,
        backend="local_gsplat_v1",
        backend_version="gsplat-1.5.3",
        sensors=["video", "undistorted_colmap"],
        devices=["cuda"],
        checkpoint_semantics="complete local optimizer and RNG checkpoint",
        checkpoint_rights="local user data; gsplat Apache-2.0",
        limits={
            "max_frames": 120,
            "max_source_bytes": 16 * 1024**3,
            "max_output_bytes": 32 * 1024**3,
        },
    )
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--data", type=Path, required=True)
    parser.add_argument("--video", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--steps", type=int, choices=(7000, 30000), required=True)
    parser.add_argument("--max-splats", type=int, default=2000000)
    parser.add_argument("--max-image-dimension", type=int, required=True)
    parser.add_argument(
        "--resume",
        type=Path,
        help="Trusted local checkpoint with identical recipe/input hash",
    )
    args = parser.parse_args()
    if (
        not 4 <= args.max_splats <= 2000000
        or not 64 <= args.max_image_dimension <= 4096
    ):
        parser.error("Invalid explicit image/splat budget")
    args.output.mkdir(parents=True, exist_ok=True)
    lock = args.output / "fit.lock"
    descriptor = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    try:
        os.write(descriptor, str(os.getpid()).encode("ascii"))
        os.close(descriptor)
        if (args.output / "result.json").exists():
            raise RuntimeError("Committed output exists; select a fresh job directory")
        if (args.output / "checkpoint.pt").exists() and not args.resume:
            raise RuntimeError(
                "Interrupted job has a checkpoint; explicitly resume or select a fresh directory"
            )
        for sig in (signal.SIGINT, signal.SIGTERM):
            signal.signal(sig, request_stop)
        with we_protocol.diagnostic_context():
            result = fit(args)
        if we_protocol.WRITER:
            we_protocol.completed("appearance", args.output / "result.json")
        else:
            print(json.dumps(result, allow_nan=False), flush=True)
    finally:
        lock.unlink(missing_ok=True)


if __name__ == "__main__":
    try:
        main()
    except InterruptedError as error:
        we_protocol.failure("CANCELLED", "appearance", str(error))
        raise SystemExit(130)
    except Exception as error:
        we_protocol.failure(
            "BACKEND_FAILED", "appearance", f"Development appearance failed: {error}"
        )
        raise SystemExit(2)
