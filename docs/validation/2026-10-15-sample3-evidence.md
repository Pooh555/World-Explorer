# Replacement sample_3 evidence — 2026-10-15 Asia/Hong_Kong

Scope/status: **M1/E1 IN_PROGRESS; Q3 BLOCKED**. The completed replacement scene
opens locally, three calibrated full-scene Vulkan/gsplat comparisons pass, and
all **16** retained RGB8 holdout scores reproduce exactly. No milestone or full
Q/P/N/E gate is certified. Cwd: repository root; parent **0ce687d**. No production,
worker, recipe or environment implementation changed. Raw ignored/local evidence
root **R**: `artifacts/validation/sample3-evidence-20261015/`.

## Retained evidence and identities

Read-only admission checks current source bytes, completed fit/config identity,
complete ordered **62 training / 2 guard / 16 holdout** partition, all encoded
PNG hashes/sizes and decoded/cropped RGB hashes. Independent NumPy float64 MSE/
PSNR and arithmetic aggregation agree with the saved report. Original reference
metadata equals current fit metadata; its hashed binary rows contain all
**648,332 splats**. `R/retained-checks.json` records exact checked file identities.

Two provenance limits required fresh outputs: saved `reference/` contains native
readbacks but **no comparison.json**; the saved holdout request/report references
metric receipt SHA `28c7227425c810245e4b340af2a8d5a107ae87355b348c97da6da00b5470cbf8`,
not found in current local artifact JSONs or tracked metric receipts. Current
[metric receipt](../../workers/identity/metric-evaluation-cpu-20261014.json) SHA
`9921e66a36f1a07c724e3df897cebc9185f2c6dd3c34ac56eba5ff617c35842f`
has the **same metric identity**,
`fd524bb7d76d31d3877a85e658c3745534d86028c83d4f9c654912f529725836`.
Saved preparer/evaluator/loader source hashes match current tools. The saved
114-frame viewer log has no scene/binary hash binding; it remains a retained
claim. Fresh checks below establish current scoped evidence without rewriting it.

| Bound input / fresh output | SHA-256 |
|---|---|
| Source video | `4448a1d9eb93f8b7fc787f9ea037d282128438a2512fad8eb8df8544f53906da` |
| Fit identity, 7k steps / 80 registered poses | `3b7da295d475bf838651ea38d9279a30c32e88314f25d71d4120665913bf0ed6` |
| Fit result | `e1eb5fa50a84ab3538f088f27a29b2c978749c131747cf1fb3f4879ea4e23851` |
| Canonical scene content | `14787aa730211790d4b8310fb61093f2125740bc399497cacc59ff8b842a6cfe` |
| R/pairs-checked.json | `b006977fb11cb3763854727795342f4488a85f68bb7498e3582abee57e1a8c27` |
| R/holdout-current.json | `59290c8d5df016d871b177ad31534f3e452ed177aa9a0ef16f358a5cf27dabf0` |
| R/reference-current/comparison.json | `81b27a6f4c4421cefb19e6280c26d802d3b7df30311018f5d65f67bc8a126e39` |

## Actual checks

`R/run_evidence.py` SHA
`6aa27610255208019a5ddb0ec61ae603ab9bd3fad984a0f615446703c9b965c8`
defines exact arguments; command receipts bind cwd, exit, elapsed time, runner
and executable hashes. `R/run_viewer.py` SHA
`64d3f76eaea55a8175ba38ce7b41891034e929f6b950618cc0b1fb95fae1a4d8`
also binds every scene file and screenshot. All new outputs use distinct paths.

| Commands from repository root | Actual result / raw record in R |
|---|---|
| `python3 R/run_evidence.py before`; `.venv-metrics/bin/python R/run_evidence.py read` | **0**, 2,088 protected / 94 src/include files match baseline; all 16 retained pairs/PSNR/aggregates agree; `preservation-before.json`, `retained-checks.json` |
| `python3 R/run_evidence.py prepare` | Final **0**, complete checked request using current receipt; `prepare-checked-command.json` / `.log` |
| `python3 R/run_evidence.py evaluate` | **0**, offline CPU, 16/16, 18.17 s; `evaluate-command.json` / `.log` |
| `python3 R/run_evidence.py native` | **0**, all splats, three 718×1280 views, four submitted frames, zero Vulkan errors, 12.30 s; `native-command.json`, `reference-current/native.json` |
| `python3 R/run_evidence.py compare` | **0**, pinned gsplat/CUDA, unchanged ≥45 dB / ≤8-level tolerances, 4.83 s; `compare-command.json` / `.log` |
| `python3 R/run_evidence.py compare-results` | **0**, every metric delta **0**, reference fixture and all three native PPMs byte-identical to retained evidence; `comparison-to-retained.json` |
| `python3 R/run_viewer.py` | **0**, `--frames 114` target, actual **126 frames / 648,332 resident splats / zero Vulkan errors**, 5.83 s; `viewer-command.json`, `viewer-current.log/ppm` |
| `python3 R/run_evidence.py after`; `python3 R/run_evidence.py static` | **0**, protected/production bytes unchanged; docs/boundaries/diff pass; `preservation-after.json`, `*-command.json` / logs |

After the metric README's stale evaluator-status sentence and result links were
updated, all three static commands passed again (`R/static-final-commands.json`).

Initial preparation succeeded, but the local runner's receipt writer tried to
hash `python3` as a repository path. `prepare.log` and its first request remain;
resolving executables through PATH repaired the observer, then preparation used
a fresh output. No application check or threshold was changed.

Native device: **RTX 4060 Laptop**, Linux, existing GCC C++26 binaries; not H_R,
H_V or H_M. Reference: Python 3.10.22, gsplat 1.5.3+pt24cu124, torch 2.4.1+cu124,
CUDA 12.4. CPU metrics: separate Python 3.10.22, TorchMetrics 1.8.2,
torch 2.4.1+cpu, four threads. Installed bytes are checked before metric loading;
only selected official metric weights are loaded, offline. No optimizer checkpoint
is deserialized. No C++ rebuild, CTest, coverage or sanitizer rerun was needed
for this evidence/documentation slice; prior passes retain their own scope.

| Native versus gsplat | PSNR dB | SSIM | Maximum RGB8 error |
|---|---:|---:|---:|
| 000001.png, first | 63.0797 | 0.999623 | 1 level |
| 000041.png, middle | 63.1299 | 0.999611 | 1 level |
| 000084.png, last | 63.1207 | 0.999569 | 1 level |

These use the [existing shared-origin/affine camera convention](2026-10-13-real-reference.md),
uncompressed canonical SH3 assets and FP32 compositing. Renderer-owned sampled
allocation **381,077,088 bytes**; viewer **360,867,456 bytes**. These are not whole
VRAM peaks or performance measurements. The inspected **1428×868** Source-view
screenshot shows the real corridor, arbitrary scale, unmeasured gravity, unknown
coverage and disabled walking. Controls/latency were not exercised in this run.

## All holdout frames

Targets **1073×1912 → 718×1280** via declared Pillow RGB8 bicubic resize; full
common crop, excluded area **0%**, no mask, exposure fitting or color correction.
SSIM uses 11×11 valid windows (**899,160** pixels). RGB8 scores reproduce the saved
report exactly; FP32 fitter scores remain separate, with LPIPS null throughout.

| Frame | RGB8 PSNR | RGB8 SSIM | AlexNet LPIPS | Original FP32 PSNR | Original FP32 SSIM |
|---|---:|---:|---:|---:|---:|
| 000065.png | 28.3242 | 0.972938 | 0.096597 | 28.3276 | 0.973693 |
| 000070.png | 21.0943 | 0.854539 | 0.206356 | 21.0938 | 0.855500 |
| 000071.png | 20.7643 | 0.853886 | 0.167115 | 20.7636 | 0.854853 |
| 000072.png | 21.0112 | 0.857037 | 0.235994 | 21.0054 | 0.858032 |
| 000073.png | 21.1095 | 0.851507 | 0.255827 | 21.1036 | 0.852508 |
| 000074.png | 21.0483 | 0.830427 | 0.269769 | 20.9183 | 0.830544 |
| 000075.png | 20.3143 | 0.790454 | 0.294391 | 20.1779 | 0.790361 |
| 000076.png | 20.3986 | 0.812491 | 0.278522 | 20.1336 | 0.812005 |
| 000077.png | 19.7696 | 0.826598 | 0.274414 | 19.4907 | 0.826128 |
| 000078.png | 20.4026 | 0.799277 | 0.267487 | 19.6753 | 0.798200 |
| 000079.png | 20.0627 | 0.770125 | 0.271559 | 19.3885 | 0.768997 |
| 000080.png | 19.9607 | 0.765989 | 0.283330 | 19.2997 | 0.764851 |
| 000081.png | 19.9224 | 0.769158 | 0.280028 | 19.2621 | 0.767971 |
| 000082.png | 19.9339 | 0.770954 | 0.277682 | 19.2653 | 0.769713 |
| 000083.png | 19.9762 | 0.765305 | 0.290631 | 19.2931 | 0.763957 |
| 000084.png | 19.8401 | 0.760304 | 0.299543 | 19.1650 | 0.758742 |
| Mean, one development scene | **20.8708** | **0.815687** | **0.253078** | — | — |

PSNR and LPIPS miss Q3's ≥25 / ≤0.25 numeric targets. SSIM alone exceeds 0.80;
this is no corpus/median pass. Full-sequence COLMAP poses, no independent
inspection path/observed-region evidence and arbitrary scale remain unqualified.
Three camera comparisons do not establish all-camera robustness or Q6's fixed
100-view packing fidelity; the scene exceeds M1's prescribed ≤250k fixture.
Both completed scenes, disconnected sample_2 stages and all **2,088 protected /
94 production files** are unchanged. No decode, pose solve, fitting, installation,
download, upload or push occurred. Native Windows rerun, rights and reference/
platform qualification remain pending. [Progress](../progress.md) owns next work.
