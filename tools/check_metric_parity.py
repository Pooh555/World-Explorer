#!/usr/bin/env python3
"""Check pinned offline CPU LPIPS/SSIM on synthetic images; no Q gate certification."""

import argparse
from contextlib import contextmanager
import hashlib
from types import ModuleType
import json
import os
from pathlib import Path
import socket
import stat
import sys
import sysconfig
import tempfile

from audit_metric_environment import POLICY, metric_manifest
from audit_worker_environment import MAX_FILE_BYTES, MAX_METADATA_BYTES, bounded_read, canonical_bytes, file_digest, publish_receipt


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("Duplicate metric manifest key")
        result[key] = value
    return result


def reject_constant(value):
    raise ValueError("Nonfinite metric manifest")


def verify_manifest(path, prefix, sites, requirements, ssim, weights):
    manifest_bytes = bounded_read(path)
    expected = json.loads(manifest_bytes, object_pairs_hook=unique_object,
                          parse_constant=reject_constant)
    if (not isinstance(expected, dict) or type(expected.get("schema_version")) is not int or
            expected.get("schema_version") != 1 or expected.get("identity_ready") is not True or
            expected.get("status") != "IMPLEMENTED_UNVERIFIED" or expected.get("metric_qualified") is not False):
        raise ValueError("A complete metric identity receipt is required")
    identity = expected.get("identity", {})
    if (not isinstance(identity, dict) or identity.get("policy") != POLICY or
            hashlib.sha256(canonical_bytes(identity)).hexdigest() != expected.get("identity_sha256")):
        raise ValueError("Metric policy or identity digest differs")
    current = metric_manifest(prefix, sites, requirements, ssim, weights)
    if not current["identity_ready"] or current["identity_sha256"] != expected["identity_sha256"]:
        raise ValueError("Current metric bytes differ from the selected identity receipt")
    if bounded_read(path) != manifest_bytes:
        raise ValueError("Metric manifest changed during verification")
    return current, hashlib.sha256(manifest_bytes).hexdigest()


@contextmanager
def offline_network():
    original = socket.socket

    class offline_socket(original):
        def connect(self, address):
            if self.family in (socket.AF_INET, socket.AF_INET6):
                raise RuntimeError("Metric parity runs offline; no model/package download")
            return super().connect(address)

        def connect_ex(self, address):
            if self.family in (socket.AF_INET, socket.AF_INET6):
                raise RuntimeError("Metric parity runs offline; no network fallback")
            return super().connect_ex(address)

    socket.socket = offline_socket
    try:
        yield
    finally:
        socket.socket = original


@contextmanager
def checked_tensor_loading(torch, allowed):
    original = torch.load

    def load(path, *args, **kwargs):
        selected = Path(path).resolve(strict=True)
        if selected not in allowed:
            raise ValueError("Unselected or changed metric checkpoint")
        info = selected.stat()
        if not stat.S_ISREG(info.st_mode) or info.st_size > MAX_FILE_BYTES:
            raise ValueError("Metric checkpoint is not regular or exceeds byte budget")
        # Hash and load one bounded snapshot. Reopening a checked path could load
        # different bytes after a concurrent edit or replacement.
        with selected.open("rb") as stream, tempfile.SpooledTemporaryFile(max_size=8 * 1024**2) as snapshot:
            digest = hashlib.sha256()
            total = 0
            while block := stream.read(1024**2):
                total += len(block)
                if total > MAX_FILE_BYTES:
                    raise ValueError("Metric checkpoint exceeds byte budget")
                digest.update(block)
                snapshot.write(block)
            if digest.hexdigest() != allowed[selected]:
                raise ValueError("Unselected or changed metric checkpoint")
            snapshot.seek(0)
            kwargs["weights_only"] = True
            return original(snapshot, *args, **kwargs)

    torch.load = load
    try:
        yield
    finally:
        torch.load = original


def load_cpu_metrics(report, sites, ssim_path, weights):
    # Avoid the library's optional slow-doctest download probe. Normal metric
    # evaluation remains inside our network guard and uses one explicit cache.
    os.environ["SKIP_SLOW_DOCTEST"] = ""
    import torch
    torch.set_num_threads(4)
    torch.hub.set_dir(str(weights.parent.parent))
    if weights.name != "alexnet-owt-7be5be79.pth" or weights.parent.name != "checkpoints":
        raise ValueError("Select the explicitly provisioned AlexNet cache file")
    relative = "torchmetrics/functional/image/lpips_models/alex.pth"
    candidates = [site / relative for site in sites if (site / relative).is_file()]
    if len(candidates) != 1:
        raise ValueError("Ambiguous or missing pinned LPIPS linear weights")
    linear_path = candidates[0].resolve(strict=True)
    allowed = {weights: report["identity"]["selected_alexnet_bytes"]["sha256"],
               linear_path: report["identity"]["implementation_files"][relative]["sha256"]}
    with checked_tensor_loading(torch, allowed):
        import torchvision
        from torchmetrics.image.lpip import LearnedPerceptualImagePatchSimilarity
        metric = LearnedPerceptualImagePatchSimilarity(net_type="alex", reduction="none", normalize=True)
        metric.eval()
    ssim_bytes = bounded_read(ssim_path)
    if hashlib.sha256(ssim_bytes).hexdigest() != report["identity"]["ssim_implementation"]["sha256"]:
        raise ValueError("SSIM source changed before loading")
    ssim = ModuleType("checked_ssim")
    # Execute the exact checked source bytes rather than reopening the path.
    exec(compile(ssim_bytes, str(ssim_path), "exec"), ssim.__dict__)
    return torch, metric, ssim, allowed


def run_parity(report, sites, ssim_path, weights):
    torch, metric, ssim, allowed = load_cpu_metrics(report, sites, ssim_path, weights)
    with checked_tensor_loading(torch, allowed):
        import torchvision
        # Independent feature traversal and weighted distance, using the same
        # checked tensors without calling TorchMetrics' LPIPS helpers.
        backbone = torch.load(weights, map_location="cpu")
        features = torchvision.models.alexnet(weights=None).features.eval()
        features.load_state_dict({key.removeprefix("features."): value for key, value in backbone.items()
                                  if key.startswith("features.")}, strict=True)
        del backbone
        linear_path = next(path for path in allowed if path != weights)
        linear = torch.load(linear_path, map_location="cpu")
    generator = torch.Generator().manual_seed(20261014)
    left = torch.rand((4, 3, 80, 96), generator=generator)
    right = torch.rand(left.shape, generator=generator)
    right[0] = left[0]
    right[2] = (0.5 * left[2] + 0.25).clamp(0, 1)
    right[3] = left[3].flip(-1)
    shift = torch.tensor([-0.030, -0.088, -0.188]).reshape(1, 3, 1, 1)
    scale = torch.tensor([0.458, 0.448, 0.450]).reshape(1, 3, 1, 1)
    x, y = (2 * left - 1 - shift) / scale, (2 * right - 1 - shift) / scale
    reference = torch.zeros(4)
    feature_index = 0
    with torch.inference_mode():
        actual = metric(left, right)
        for index, layer in enumerate(features):
            x, y = layer(x), layer(y)
            if index not in (1, 4, 7, 9, 11):
                continue
            normalized_x = x / torch.sqrt(1e-8 + (x * x).sum(1, keepdim=True))
            normalized_y = y / torch.sqrt(1e-8 + (y * y).sum(1, keepdim=True))
            weight = linear[f"lin{feature_index}.model.1.weight"]
            reference += ((normalized_x - normalized_y).square() * weight).sum(1).mean((1, 2))
            feature_index += 1
        if not torch.isfinite(actual).all() or not torch.allclose(actual, reference, atol=2e-6, rtol=2e-6):
            raise ValueError("LPIPS differs from independent feature/linear-weight reference")
        metric.reset()
        singles = torch.cat([metric(left[i:i + 1], right[i:i + 1]).reshape(1) for i in range(4)])
        if not torch.allclose(singles, actual, atol=2e-6, rtol=2e-6) or actual[0].item() != 0:
            raise ValueError("LPIPS per-image/identical-image behavior differs")
        metric.reset()
        metric.normalize = False
        inverse = metric(2 * left - 1, 2 * right - 1)
        if not torch.allclose(inverse, actual, atol=2e-6, rtol=2e-6):
            raise ValueError("LPIPS normalization policy differs")
        ssim_rows = []
        for index in range(4):
            a, b = left[index:index + 1].double(), right[index:index + 1].double()
            positions = torch.arange(-5, 6, dtype=torch.float64)
            g = torch.exp(-positions.square() / (2 * 1.5**2))
            g /= g.sum()
            kernel = torch.outer(g, g).reshape(1, 1, 11, 11).expand(3, 1, 11, 11)
            conv = lambda image: torch.nn.functional.conv2d(image, kernel, groups=3)
            ma, mb = conv(a), conv(b)
            va, vb, covariance = conv(a * a) - ma.square(), conv(b * b) - mb.square(), conv(a * b) - ma * mb
            expected = (((2 * ma * mb + 0.01**2) * (2 * covariance + 0.03**2)) /
                        ((ma.square() + mb.square() + 0.01**2) * (va + vb + 0.03**2))).mean()
            measured = ssim.gaussian_ssim(a, b)
            if not torch.isfinite(measured) or abs((measured - expected).item()) > 5e-12:
                raise ValueError("SSIM differs from independent double-precision 2D window")
            ssim_rows.append({"case": index, "ssim": measured.item(),
                              "reference": expected.item(), "absolute_error": abs((measured - expected).item())})
    return {"fixture": "synthetic seeded RGB; not reconstructed quality evidence", "seed": 20261014,
            "shape": list(left.shape), "device": "cpu", "threads": 4,
            "lpips_dtype": "float32", "ssim_dtype": "float64",
            "tolerances": {"lpips_atol": 2e-6, "lpips_rtol": 2e-6, "ssim_atol": 5e-12},
            "input_sha256": hashlib.sha256(left.numpy().tobytes() + right.numpy().tobytes()).hexdigest(),
            "lpips": actual.tolist(), "lpips_reference": reference.tolist(),
            "lpips_max_absolute_error": (actual - reference).abs().max().item(),
            "lpips_per_image_max_error": (actual - singles).abs().max().item(),
            "lpips_normalization_max_error": (actual - inverse).abs().max().item(),
            "ssim": ssim_rows, "network": "DISABLED", "checkpoint_loading": "weights_only=True",
            "parity_passed": True, "metric_qualified": False,
            "remaining": "dataset crop/mask/resolution, real corpus and rights/release review"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--requirements", required=True, type=Path)
    parser.add_argument("--ssim-implementation", required=True, type=Path)
    parser.add_argument("--alexnet-weights", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    try:
        output = args.output.resolve()
        if not output.is_relative_to(root / "artifacts") or args.output.exists() or args.output.is_symlink():
            raise ValueError("Select a new parity receipt in ignored artifacts/")
        prefix = Path(sys.prefix).resolve(strict=True)
        sites = {Path(sysconfig.get_path(key)).resolve(strict=True) for key in ("purelib", "platlib")}
        weights = args.alexnet_weights.resolve(strict=True)
        with offline_network():
            report, manifest_sha256 = verify_manifest(args.manifest, prefix, sites, args.requirements,
                                                      args.ssim_implementation, weights)
            results = run_parity(report, sites, args.ssim_implementation, weights)
        results.update(schema_version=1, manifest_sha256=manifest_sha256,
                       identity_sha256=report["identity_sha256"],
                       checker_sha256=file_digest(Path(__file__), MAX_METADATA_BYTES)[0])
        publish_receipt(output, results)
        print(json.dumps({"receipt": str(output.relative_to(root)), "parity_passed": True,
                          "metric_qualified": False, "lpips_max_absolute_error": results["lpips_max_absolute_error"]}))
        return 0
    except (OSError, ValueError, RuntimeError, RecursionError) as error:
        print(f"Metric parity failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
