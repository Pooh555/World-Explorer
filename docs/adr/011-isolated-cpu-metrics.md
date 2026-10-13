# 011 — Separate pinned CPU metric environment

Date: 2026-10-14, Asia/Hong_Kong. Status: accepted for M1 metric locking.

The training environment lacks TorchMetrics and AlexNet weights. Installing
metrics into it would change the audited environment used by compatible fits.
The user explicitly authorized a separate evaluation environment and official
model download. The available CPU metric path needs no reconstruction GPU.

Use `.venv-metrics` with CPython 3.10.22, TorchMetrics 1.8.2, PyTorch 2.4.1+cpu,
TorchVision 0.19.1+cpu and a SHA-256 installer lock. Keep the established CUDA
training profile and recipe sources unchanged. Bind full installed inventories,
selected implementation/SSIM files, bundled learned LPIPS linear weights and the
explicit AlexNet checkpoint to the specification's fixed policy. Pin package
archives through official PyPI/PyTorch hashes and record the approved backbone's
URL/full digest. This profile is Linux x86_64-specific.

Run parity offline with one explicit cache and `weights_only=True` on both learned
files. A checkpoint guard checks selected paths/digests before loading. Independent
feature/linear-weight and Gaussian-window references pass on synthetic RGB;
[evidence](../validation/2026-10-14-metric-identity.md). Identity/parity receipts
always leave gate qualification false: no independent quality corpus, dataset
regions or complete redistribution review has been established. The trade-off is
an additional environment and CPU evaluation time; no duplicate optimizer exists.
Revisit for a measured evaluation bottleneck or a qualified additional platform,
retaining the same metric semantics and explicit identities.
