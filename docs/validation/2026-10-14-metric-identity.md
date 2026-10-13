# Offline CPU metric identity and parity — 2026-10-14 Asia/Hong_Kong

Scope/status: **M1/E1/E2 IN_PROGRESS**; fixed metric policy and local identities
with synthetic numerical parity. No Q2/Q3/Q6 or release certification. Cwd:
repository root. Parent `d63bfa0`; implementation is the local commit containing
this report. Raw ignored evidence: `R = artifacts/validation/metric-identity-20261014`.
`identity.json` binds code, dependency locks, runners, receipts and logs.

## Actual environment and learned bytes

The original training environment still lacks TorchMetrics. The previous handoff's
reference to its installed LPIPS source was inaccurate. The new read-only audit
publishes a blocked receipt with missing TorchMetrics/lightning-utilities/selected
AlexNet bytes; `--require-identities` exits **3**, never successful qualification.
The user then explicitly authorized separate evaluation provisioning and official
model downloads; no training package, recipe or compatible checkpoint was changed.

`.venv-metrics`: CPython **3.10.22**, **16 distributions / 15,339 RECORD-listed
files / 845,218,881 bytes / 43 notice files**. All exact versions and supplied
RECORD hashes match. TorchMetrics **1.8.2**, torch **2.4.1+cpu**, torchvision
**0.19.1+cpu**, NumPy **1.26.4**, lightning-utilities **0.15.2** are distinct from
the CUDA reconstruction profile. [Installer archive hashes](../../workers/metrics/evaluation-lock.txt)
come from official PyPI/PyTorch, enforced by `uv --require-hashes`; the
[compact CPU receipt](../../workers/identity/metric-evaluation-cpu-20261014.json)
locks policy, installed inventories and selected implementations.
[ADR 011](../adr/011-isolated-cpu-metrics.md) describes the isolated profile.

| Selected bytes | Actual identity |
|---|---|
| Explicit official AlexNet checkpoint | **244,408,911 bytes**, SHA-256 `7be5be791159472b1fbf3c69796f7cb30dca7ad8466c2df70058c37116cdee02`; official filename prefix also matches |
| Bundled TorchMetrics AlexNet LPIPS linear weights | **6,009 bytes**, SHA-256 `df73285e35b22355a2df87cdb6b70b343713b667eddbda73e1977e0c860835c0` |
| Fixed policy | SHA-256 `04cafee9d4e0a5452d6e5e11d9bc0ac309270f07380088ce4f9c56e1d842f853` |
| Complete CPU identity | SHA-256 `fd524bb7d76d31d3877a85e658c3745534d86028c83d4f9c654912f529725836` |

[Official TorchMetrics 1.8.2 source](https://github.com/Lightning-AI/torchmetrics/blob/v1.8.2/src/torchmetrics/functional/image/lpips.py)
uses separate pretrained feature and bundled linear weights; selecting `alex`
alone does not provision or lock both. The checkpoint URL/role follows the
[installed TorchVision 0.19.1 AlexNet definition](https://github.com/pytorch/vision/blob/v0.19.1/torchvision/models/alexnet.py).
Raw `backbone-provenance.json` records approved URL, full hash and size. Weights,
package caches and complete installed receipts remain ignored; rights clearance
is **NOT_PERFORMED**. Provisioning is not implicit processing-time downloading.

## Commands and checks

Commands below actually ran; exact argv/cwd/exits/logs are in `provision-commands.json`
and `*-commands.json`. R is expanded to the path above. Successful checks exit 0.

| Command | Actual result / raw evidence |
|---|---|
| `uv pip compile --python .venv-train/bin/python --generate-hashes --default-index https://pypi.org/simple --cache-dir R/uv-cache --output-file R/evaluation.lock R/evaluation.in` | Resolved 16 exact packages and archive hashes; `resolve.log` |
| `uv venv --python .venv-train/bin/python .venv-metrics`; `uv pip install --python .venv-metrics/bin/python --require-hashes --only-binary :all: --default-index https://pypi.org/simple --cache-dir R/uv-cache -r R/evaluation.lock` | Separate CPU environment; `create-env.log`, `install.log` |
| Explicit `curl --fail --location --retry 2 --connect-timeout 20 --max-time 300` of the official AlexNet URL to `R/torch-cache/hub/checkpoints/` | Exit 0; actual file/full SHA/prefix checked; `backbone-download.log`, provenance |
| `python3 R/run_checks.py focused`; `python3 R/run_checks.py workers` | **16/16 new tests and 53/53 worker tests each** on Python **3.10.22 / 3.12.15 / 3.14.8**, no skips |
| `python3 R/run_checks.py dev` | Existing GCC C++26 development preset: **121/121 CTest**; Python additions remain inside one worker CTest; no new C++ build/GPU/platform pass claimed |
| `.venv-metrics/bin/python tools/audit_metric_environment.py` with CPU requirements, SSIM source, explicit weights, new `R/evaluation-metrics.json`, `--require-identities` | Identity ready, no blockers; metric qualification remains false; exact argv retained |
| `.venv-metrics/bin/python tools/check_metric_parity.py` with that manifest/requirements/source/weights and new `R/parity-checked-source.json` | Offline CPU independent references pass; `parity-checked-source.log/json` |
| `.venv-train/bin/python tools/audit_worker_environment.py` with unchanged training requirements and new `R/training-after.json` | Original **37 installed inventory hashes** unchanged; `training-preservation.json` |
| `python3 R/run_checks.py static` | Documentation links, module boundaries and diff checks pass; exact argv/results retained |

New synthetic metadata regressions cover missing/unpinned/version-mismatched
packages, changed/unlisted implementation or learned bytes, absent RECORD,
changing selected inputs, ambiguous sites/aliases, policy/manifest tampering,
blocked receipts, bounded atomic no-replacement publication, network guards and
checksum-selected tensor-only loads. An initial network-guard test attempted to
create a native socket, which the sandbox rejects on all three interpreters.
The fixture now exercises both guarded connection methods using a synthetic
socket owner; no skip, permission weakening or actual connection is required.
Initial failing logs remain as `*-socket-initial-*`.

## Numerical and data scope

Four seeded synthetic RGB pairs **80×96**, CPU/four threads. Independent AlexNet
feature traversal plus learned linear-weight distances equals TorchMetrics LPIPS
exactly (max error **0**). Per-image versus batch error **2.9802322387695312e-08**;
`normalize=True` versus explicit [-1,1] input error **0**; identical pair **0**.
Float64 SSIM against an independent 2D Gaussian-window calculation has maximum
absolute error **1.1102230246251565e-16**. Diagnostic tolerances are LPIPS
atol/rtol **2e-6**, SSIM atol **5e-12**; Q thresholds remain unchanged.

Parity rechecks the complete selected identity before backend imports, disables
network connections, selects only the explicit cache, and permits checksum-checked
`weights_only=True` loading of the two selected learned files. No GPU, decoder,
pose solve, fitting or scene publication occurs. SHA-256/size comparison preserves
**2,393 protected files**, including videos, complete/failed runs, fits/checkpoints,
scenes and AGENTS/runbook/specification. Existing development metrics with null
LPIPS retain their historical FP32 scope; they are not retroactively upgraded.

Remaining: a dataset image-pair evaluator with fixed common crop/mask/resolution,
quantized-versus-FP32 scope, independent inspection corpus, learned-weight/corpus
rights, training archive/system identities, reference/native platform qualification
and all complete Q/P/N/E gates. Synthetic parity supplies metric implementation
checks, not reconstruction quality, runtime performance or redistribution clearance.
