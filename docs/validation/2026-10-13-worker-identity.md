# Installed worker identity audit — 2026-10-13 Asia/Hong_Kong

Scope/status: **M1/E1/E2 IN_PROGRESS**. Repeatable offline installed-file/notice
receipts; no release, metric or reconstruction gate certification. Cwd: repository
root. Parent `9d99dfc`; implementation is the local commit containing this report.
Raw ignored evidence `R = artifacts/validation/worker-identity-20261013`.
The [compact receipt](../../workers/identity/local-development-20261013.json)
binds requirement files, installed inventories, notices, backend and auditor source.

## Observed identities and limits

| Environment | Actual installed receipt |
|---|---|
| Training Python 3.10.22 | **37 distributions / 19,301 RECORD-listed files / 5,489,073,341 bytes / 70 notice files**; all declared versions and supplied file hashes match; no extra distributions or legacy metadata |
| Pose Python 3.12.15 | **3 distributions / 1,039 RECORD-listed files / 214,788,169 bytes / 6 notice files**; exact pycolmap 4.2.1, NumPy 2.2.6, Pillow 11.3.0; all supplied hashes match |

Training retains gsplat **1.5.3+pt24cu124**, torch **2.4.1+cu124**, torchvision
**0.19.1+cu124**, official training parser pycolmap **3.12.6**, NumPy **1.26.4**,
Pillow **11.3.0**, SciPy **1.15.3** and all 12 recorded NVIDIA distributions.
The public receipt records each package's exact metadata/RECORD/installed/notice
inventory hashes; complete per-file receipts remain ignored. No host paths or
captured imagery enter the public summary.

`.deps/gsplat` remains clean at
`937e29912570c372bed6747a5c9bf85fed877bae`.
Its trainer matches SHA-256
`79319e1cd7404e4d1ba0c425634235c39e6054f0643b904a01feea6179462c05`.
Checkout and retained project gsplat notices both hash to
`96a4f89293c0df19880da9b0e35f67589f5885ea61b224ef16bb2bd599a8a44d`.
Worker recipe source hashes are recorded without editing those sources, so this
audit does not invalidate existing compatible fit/checkpoint identities.

**Remaining limits:** original wheel archives are not locally available; gsplat's
installed direct-URL metadata has empty archive information. Installed-file hashes
are not original archive hashes or independent provenance. Triton **3.0.0** has
no separate license/notice file identified in its RECORD; its METADATA carries
a MIT license classifier. That classifier does not supply the absent notice.
Transitive rights, system/interpreter libraries and release SBOM
are not cleared by this inventory.

TorchMetrics and LPIPS are absent in both environments. The explicit default
AlexNet cache file `~/.cache/torch/hub/checkpoints/alexnet-owt-7be5be79.pth` is absent;
no other personal cache search occurred. Metric/backbone/learned LPIPS linear-weight
hashes remain **BLOCKED**. The specification's AlexNet/`normalize=True` policy
remains controlling. The pinned trainer selects that policy, but it is unqualified.
No install, model download, new metric result or lower-budget fit occurred.
Upstream [LPIPS source](https://github.com/Lightning-AI/torchmetrics/blob/master/src/torchmetrics/functional/image/lpips.py)
confirms separate backbone and learned linear weights; this reference is a
qualification dependency, not a version/hash lock for an installed package.

## Actual checks and failures

| Commands (R expanded as above) | Observed result / raw evidence |
|---|---|
| `.venv-train/bin/python tools/audit_worker_environment.py --requirements workers/gsplat/local-environment.txt --output R/training-release.json` | Exit 0; actual training inventory captured, metric qualification false |
| `.venv-worker/bin/python tools/audit_worker_environment.py --requirements workers/pose-requirements.txt --output R/poses-release.json` | Exit 0; actual pose inventory captured, metric qualification false |
| `python3 R/run_checks.py focused` | **11/11 new synthetic tests** each on Python 3.14.8, 3.12.15 and 3.10.22; no skips |
| `python3 R/run_checks.py workers` | **36/36 integration tests** each on Python 3.14.8 / 3.12.15; synthetic admissions/recovery fixtures, not successful real reconstruction |
| `python3 R/run_checks.py dev`; `python3 R/run_checks.py clang` | Exit 0; **119/119 CTest each**, using existing GCC C++26 / Clang C++23 build profiles; no C++ changes or new native GPU pass claimed |
| `python3 R/run_checks.py static` | Documentation, architecture boundaries and diff checks exit 0; exact argv/results retained |
| Repeat audit with existing `R/training-release.json` | Intentional exit **2**, existing receipt bytes unchanged; command/hash retained |

Regressions cover changed/missing bytes, mismatched/missing RECORD hashes,
SHA-512, traversal/symlink escape, duplicate paths, limits, mutable requirements/
metadata, notice identity, no backend imports, explicit backbone bytes without
deserialization and atomic publication without replacement.
The [protocol](../protocols/worker_identity.md) states limits and unverified scope.

Initial training audit exits **1** before publication: Python 3.10's email parser
returns a Header object for non-ASCII metadata. Converting header values to text
repairs serialization; the UTF-8 fixture now passes on all three interpreters.
Initial failure log and all intermediate/current receipts remain locally retained.
Final review also replaces separate SHA-384/512 rereads with simultaneous digest
updates from one file read; a changing-file regression verifies consistent bytes.
Both environments retain identical installed inventory hashes after this change.

This audit also corrects the preceding Import report's **120-case count to 119**:
both original retained CTest logs show 119/119. Overlapping CPU/worker labels
double-counted the worker suite; no tests were removed. The historical report now
contains an explicit correction, and current references use the actual count.

Before/after SHA-256 comparison retains all **2,393 protected files** from the prior
Import audit, including every video/run, failed stages, fit/checkpoints/scenes and
AGENTS/runbook/specification. No reconstruction workers, duplicate solve/fit,
upload or push. Native input/HiDPI, qualified fixtures/reference machines, complete
metric and rights locks and the existing Q/P/N/E gates remain pending.
