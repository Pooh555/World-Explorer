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
