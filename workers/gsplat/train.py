#!/usr/bin/env python3
"""Execute the pinned upstream trainer without downloads or a network viewer."""

import argparse
import hashlib
import importlib.metadata
import json
from pathlib import Path
import socket
import subprocess
import sys
import time

__all__ = [
    "GSPLAT_COMMIT",
    "TRAINER_SHA256",
    "deny_network",
    "export_ascii",
    "file_hash",
    "main",
]

GSPLAT_COMMIT = "937e29912570c372bed6747a5c9bf85fed877bae"
TRAINER_SHA256 = "79319e1cd7404e4d1ba0c425634235c39e6054f0643b904a01feea6179462c05"


def file_hash(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def deny_network():
    original = socket.socket

    class LocalSocket(original):
        def connect(self, address):
            if self.family in (socket.AF_INET, socket.AF_INET6):
                raise RuntimeError(
                    "This worker runs offline; provision dependencies/weights explicitly"
                )
            return super().connect(address)

        def connect_ex(self, address):
            if self.family in (socket.AF_INET, socket.AF_INET6):
                raise RuntimeError("Network access is disabled in the local trainer")
            return super().connect_ex(address)

    socket.socket = LocalSocket


def export_ascii(splats, output, maximum):
    import torch

    count = len(splats["means"])
    if count == 0 or count > maximum:
        raise RuntimeError("Empty or over-budget Gaussian output")
    tensors = [
        splats["means"],
        splats["sh0"].squeeze(1),
        splats["shN"].permute(0, 2, 1).reshape(count, -1),
        splats["opacities"].reshape(count, 1),
        splats["scales"],
        splats["quats"],
    ]
    if any(not torch.isfinite(tensor).all().item() for tensor in tensors):
        raise RuntimeError("Non-finite Gaussian output; no splats silently removed")
    properties = (
        ["x", "y", "z"]
        + [f"f_dc_{i}" for i in range(3)]
        + [f"f_rest_{i}" for i in range(tensors[2].shape[1])]
        + ["opacity"]
        + [f"scale_{i}" for i in range(3)]
        + [f"rot_{i}" for i in range(4)]
    )
    with output.open("w", encoding="ascii") as stream:
        stream.write(
            "ply\nformat ascii 1.0\ncomment exporter gsplat-1.5.3\ncomment color_domain srgb\n"
        )
        stream.write(f"element vertex {count}\n")
        for name in properties:
            stream.write(f"property float {name}\n")
        stream.write("end_header\n")
        # Bound host copies independently of scene size; preserve every coefficient.
        for start in range(0, count, 4096):
            rows = torch.cat(
                [tensor[start : start + 4096].detach().cpu() for tensor in tensors],
                dim=1,
            )
            for row in rows.tolist():
                stream.write(" ".join(format(value, ".9g") for value in row) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--data", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--steps", type=int, choices=(7000, 30000), required=True)
    parser.add_argument("--max-splats", type=int, required=True)
    parser.add_argument("--alexnet-weights-sha256", required=True)
    args = parser.parse_args()
    if args.max_splats <= 0 or args.max_splats > 2000000:
        raise RuntimeError("Invalid Gaussian resource budget")
    source = args.source.resolve(strict=True)
    commit = subprocess.check_output(
        ["git", "-C", str(source), "rev-parse", "HEAD"], text=True
    ).strip()
    if commit != GSPLAT_COMMIT:
        raise RuntimeError(f"Expected gsplat source commit {GSPLAT_COMMIT}")
    if subprocess.run(["git", "-C", str(source), "diff", "--quiet", "HEAD"]).returncode:
        raise RuntimeError("Pinned gsplat checkout has tracked modifications")
    trainer = source / "examples" / "simple_trainer.py"
    if file_hash(trainer) != TRAINER_SHA256:
        raise RuntimeError("Trainer source hash differs from the audited recipe")
    if importlib.metadata.version("gsplat") != "1.5.3":
        raise RuntimeError("Installed gsplat package must be 1.5.3")
    deny_network()
    import torch

    if not torch.cuda.is_available():
        raise RuntimeError(
            "CUDA is unavailable; existing scene viewing remains available"
        )
    weights = Path(torch.hub.get_dir()) / "checkpoints" / "alexnet-owt-7be5be79.pth"
    if not weights.is_file() or file_hash(weights) != args.alexnet_weights_sha256:
        raise RuntimeError(
            "Missing or unreviewed AlexNet LPIPS backbone; no automatic download"
        )
    sys.path.insert(0, str(source))
    sys.path.insert(0, str(source / "examples"))
    import simple_trainer
    from gsplat.strategy import DefaultStrategy

    class BudgetStrategy(DefaultStrategy):
        """Guard the pinned strategy before growth, retaining its numeric behavior."""

        def _grow_gs(self, params, optimizers, state, step):
            # A refinement can add at most two children per current splat.
            # This conservative transient budget aborts, never trims/downgrades output.
            if 3 * len(params["means"]) > args.max_splats:
                raise RuntimeError(
                    "RESOURCE_LIMIT: conservative densification workspace cap reached; use a separately recorded larger recipe"
                )
            return super()._grow_gs(params, optimizers, state, step)

    config = simple_trainer.Config(
        disable_viewer=True,
        disable_video=True,
        data_dir=str(args.data),
        data_factor=1,
        normalize_world_space=False,
        result_dir=str(args.output / "upstream"),
        max_steps=args.steps,
        eval_steps=[args.steps],
        save_steps=[args.steps],
        save_ply=False,
        sh_degree=3,
        lpips_net="alex",
        antialiased=False,
        strategy=BudgetStrategy(verbose=True),
        tb_every=100,
    )
    # Q2's unlimited 30k corpus run remains a separate unqualified recipe.
    start = time.monotonic()
    runner = simple_trainer.Runner(local_rank=0, world_rank=0, world_size=1, cfg=config)
    if len(runner.splats["means"]) > args.max_splats:
        raise RuntimeError("RESOURCE_LIMIT: initial SfM splats exceed configured limit")
    runner.train()
    export_ascii(runner.splats, args.output / "appearance.ply", args.max_splats)
    provenance = {
        "schema_version": 1,
        "gsplat_commit": GSPLAT_COMMIT,
        "trainer_sha256": TRAINER_SHA256,
        "steps": args.steps,
        "splat_count": len(runner.splats["means"]),
        "elapsed_seconds": time.monotonic() - start,
        "torch": torch.__version__,
        "cuda": torch.version.cuda,
        "gpu": torch.cuda.get_device_name(),
        "peak_cuda_allocated_bytes": torch.cuda.max_memory_allocated(),
        "alexnet_weights_sha256": args.alexnet_weights_sha256,
        "max_splats": args.max_splats,
        "cap_policy": "conservative transient allocation; fail before growth",
        "checkpoint_semantics": "upstream evaluation only; no optimizer resume",
        "pixel_centers": "COLMAP half pixel; canonical camera adapter subtracts 0.5",
        "world_transform": "identity during training; scene chunk rotates Rx(pi)",
        "color_domain": "srgb",
        "rasterize_mode": "classic",
        "sh_degree": 3,
        "q2_qualified": False,
    }
    (args.output / "training_provenance.json").write_text(
        json.dumps(provenance, indent=2) + "\n"
    )
    runner.writer.close()


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"Appearance failed: {error}", file=sys.stderr)
        raise SystemExit(2)
