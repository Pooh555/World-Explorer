# Offline CPU metric identity and parity

The Linux x86_64 / CPython 3.10 evaluation profile is separate from reconstruction.
[Exact versions](evaluation-environment.txt) and the [SHA-256 installer lock](evaluation-lock.txt)
select TorchMetrics 1.8.2, PyTorch 2.4.1+cpu and TorchVision 0.19.1+cpu.
The current [identity receipt](../identity/metric-evaluation-cpu-20261014.json)
includes implementation, installed inventory, SSIM source, learned LPIPS linear
weights and explicitly provisioned AlexNet hashes. It does not certify Q2/Q3/Q6,
corpus/weight rights or other platforms. Existing fits and their LPIPS-null results
remain unchanged. [Verification evidence](../../docs/validation/2026-10-14-metric-identity.md).

Environment provisioning and weight downloads must be explicitly authorized.
The user authorized this local environment and official AlexNet download on
2026-10-14. Provisioning commands are recorded in the evidence; evaluation never
installs packages or fetches weights. This profile does not replace `.venv-train`.

With that environment and the local weights already provisioned:

```sh
.venv-metrics/bin/python tools/audit_metric_environment.py --requirements workers/metrics/evaluation-environment.txt --ssim-implementation workers/gsplat/losses.py --alexnet-weights artifacts/validation/metric-identity-20261014/torch-cache/hub/checkpoints/alexnet-owt-7be5be79.pth --output artifacts/worker-audit/metrics-new.json --require-identities
.venv-metrics/bin/python tools/check_metric_parity.py --manifest artifacts/worker-audit/metrics-new.json --requirements workers/metrics/evaluation-environment.txt --ssim-implementation workers/gsplat/losses.py --alexnet-weights artifacts/validation/metric-identity-20261014/torch-cache/hub/checkpoints/alexnet-owt-7be5be79.pth --output artifacts/worker-audit/parity-new.json
```

Choose new outputs; existing files and symlinks are refused. The audit loads no
backend or checkpoint. Parity rechecks the manifest against installed bytes before
loading selected tensors with `weights_only=True`, disables network connections
and uses the explicitly selected cache. Synthetic RGB checks cover independent
LPIPS feature/linear-weight calculations, normalization, per-image reduction and
SSIM windows. No training, scene publication or GPU execution occurs. A real
image-pair evaluator with predetermined dataset region/resolution is still needed.

## Retained image-pair evaluation

`tools/prepare_holdout_evaluation.py` selects every saved temporal holdout from
an unchanged `local_gsplat_v1` fit. Pass explicit fit/image roots, the selected
metric receipt, scene ID, common crop margin and `--mask none`. It checks the
completed-fit/config identity, original source image hashes, recorded dimensions,
bicubic resizing, contiguous training/guard/holdout partition and complete frame
selection before pinning the request. Arbitrary masks and non-RGB8 PNGs are
unsupported. Saved renders are clamped/rounded 8-bit images; they retain a different
measurement domain from the fitter's FP32 output.

`tools/evaluate_image_pairs.py` takes that request plus the same metric receipt,
requirements, SSIM source, explicit pretrained weights and a new `--output` JSON.
It checks all inputs before model loading, uses the shared offline CPU loader,
and applies the same declared rectangular crop to both images after target resize.
PNG decoding uses the checksum-checked bytes, never a second unchecked file read.
PSNR/SSIM use float64; LPIPS uses float32 AlexNet with `normalize=True`, no model
resize. Scores aggregate per image, then per scene, then with equal scene weights.
Identical-image PSNR is infinite: JSON uses null with `IDENTICAL_INFINITE`, and
aggregate status `INFINITE_INCLUDED`; it is never capped or silently excluded.

All selected files and fit/request identities are rechecked before atomic
publication. Failure publishes no partial success and existing outputs are
refused. The current evaluator includes every holdout exactly once and supports
one retained fit/scene per request. Per-scene aggregation can be reused for a
future checked corpus. Bounds: 120 pairs, 128 MiB encoded PNG, 16 Mi source
pixels, 4 Mi rendered pixels and 2 GiB selected encoded bytes. Crop dimensions
must be at least 63 for this AlexNet profile. Decoding rejects animated/alpha/
palette/16-bit PNGs. No observed-region, independent inspection-path or Q gate
qualification is inferred. [Real development results](../../docs/validation/2026-10-14-holdout-evaluation.md)
record all 13 sample_1 frames and actual commands.
