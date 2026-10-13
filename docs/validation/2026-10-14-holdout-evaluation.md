# Read-only retained holdout evaluation — 2026-10-14 Asia/Hong_Kong

Scope/status: **M1/E1 IN_PROGRESS; Q3 BLOCKED**. All retained sample_1 temporal
holdout images evaluated locally with the locked CPU metric profile. No Q/P/N/E
certification, reconstruction, new fit or scene publication. Cwd: repository root.
Parent `e58e802`; implementation is the local commit containing this report.
Raw ignored evidence: `R = artifacts/validation/holdout-evaluation-20261014`.
`identity.json` binds current code, commands, requests, reports and logs.

## Actual real result

Complete **13/13 frames 000051–000063**, **1280×689**, full bitmap crop, no mask
or observed-region claim, **0% region exclusion**. GT originals are checksum-pinned
undistorted RGB8 PNGs, resized with the recorded Pillow bicubic convention. All
48 training / 2 guard / 13 holdout frame identities and contiguous split match the
unchanged completed-fit manifest. Evaluation poses came from full-sequence COLMAP;
there is no independently captured inspection path. The 295,952-splat 7k fit and
its result remain unchanged.

| Metric: arithmetic mean of every held-out image | Original fit FP32 output | Saved clamped/rounded RGB8 PNG |
|---|---:|---:|
| PSNR dB | 17.4184256703 | **17.5283862320** |
| SSIM | 0.7870896780 | **0.7870728851** |
| LPIPS AlexNet, normalize=True | Unavailable | **0.3825621101** |

The new development means are below Q3's numeric targets (PSNR ≥25, SSIM ≥0.80,
LPIPS ≤0.25). This one temporal block cannot certify the independent-capture or
corpus gate. The +0.10996 dB difference is a measurement-domain change after
clamping/8-bit export, not evidence of improved reconstruction. Every frame's
metrics, encoded/cropped RGB hashes, region fraction and valid SSIM window area
remain in `sample_1-current.json`; no frame or failure was excluded. PSNR/SSIM use
float64, LPIPS float32; SSIM's 11×11 valid window excludes its intrinsic five-pixel
border without changing the declared common input crop. No exposure/color fitting
or automatic resolution/model changes occur.

## Identity and admission

Fit identity `8c9c8efde20b1d76a5d95733b37abd091c41ca9d6b2ed9ba881a359dd8ef2758`;
unchanged result SHA-256
`00370d0076680d20de24f0421ee1c056ccc75e5fb65449e6bea8ad65e0bbb703`.
The request SHA-256 is
`fe2d138bcd63a903796a08fc368947a85220c24bb9d9c00665c0f6a069ab76fa`.
The [CPU metric receipt](../../workers/identity/metric-evaluation-cpu-20261014.json)
retains installed/backbone/linear-weight identities; all reports bind that same
metric identity. Existing optimizer checkpoint **211,915,714 bytes**, SHA-256
`f6dd5e9d8082e781d18f21df5c2b90e0fcf0ed5e43543688ae23214a673e01b0`,
is protected and never deserialized. Saved PNGs, not the optimizer, are inputs.

The [preparer/evaluator](../../workers/metrics/README.md#retained-image-pair-evaluation)
validate fit/config digest, source hashes, PNG headers/domain/size, recorded resize,
complete ordered holdout selection, explicit crop/mask and metric identity before
loading models. Pixels decode from the checked bytes. All selected images, fit
provenance and request are rechecked before atomic no-replacement publication;
input failure publishes no partial success. Requests support one retained fit.
Arbitrary masks and animated/alpha/palette/16-bit PNGs fail explicitly. Model
loading is shared with parity and now uses the exact hash-checked bounded snapshot,
with an 8-MiB spool, selected-file whitelist and `weights_only=True`; network stays
disabled. A path change after hashing cannot substitute another checkpoint.

## Actual commands/results

Exact argv/cwd/exit/logs are in `evaluation-commands.json` and `*-commands.json`.
Profile: Linux x86_64 CPU, CPython 3.10.22, TorchMetrics 1.8.2, torch 2.4.1+cpu,
torchvision 0.19.1+cpu, Pillow 11.3.0; four Torch threads. No new native GPU,
Windows/macOS, C++ build, performance or coverage pass is claimed.

| Commands (R expanded to the path above) | Observed result |
|---|---|
| `python3 tools/prepare_holdout_evaluation.py --fit artifacts/sample_1/fit --images artifacts/sample_1/pose-recovery/stages/poses-887009f42a44529c5f211ea9c3e6f8a3dfd7e793cbf2f1b3f2a70ffd3ce2a66b/training/images --metric-manifest artifacts/validation/metric-identity-20261014/evaluation-metrics.json --scene sample_1 --crop-margin 0 --mask none --output R/pairs-final.json` | Exit 0; every holdout pinned, `prepare-final.log` |
| `.venv-metrics/bin/python tools/evaluate_image_pairs.py` with that request, pinned metric manifest/requirements/SSIM/weights and new `R/sample_1-current.json` | Exit 0; **13/13**, numeric results above; `evaluate-current.log` |
| `python3 R/run_checks.py focused` | **13 image-admission/aggregation cases** each on Python **3.10.22 / 3.12.15 / 3.14.8**, no skips |
| `python3 R/run_checks.py workers` | **67/67** each on those three interpreters, including the new weight-snapshot regression |
| `python3 R/run_checks.py dev` | Existing GCC C++26 development CTest: **121/121**; new Python cases remain inside one worker CTest |
| `.venv-metrics/bin/python R/run_numeric.py` | Synthetic constant PNGs: identical images give infinite PSNR / SSIM 1 / LPIPS 0; a known red-channel offset matches exact MSE/PSNR and finite positive LPIPS; `numeric.log/json` |
| `.venv-metrics/bin/python tools/check_metric_parity.py` with pinned manifest/source/weights and new `R/parity-final.json` | Independent CPU LPIPS/SSIM parity passes with the shared snapshot loader; `parity-final.log/json` |
| `python3 R/run_checks.py static` | Documentation, architecture boundaries and diff checks pass |

Regressions cover changed/missing bytes, fit/config/split changes, training leakage,
subset/reorder/duplicates, path/format/pixel/byte limits, ambiguous photometric
scope, image mutation during read, crop bounds, per-image dB aggregation/equal
scene weights, infinite/nonfinite scores and invalid publication. The initial
fixture left a render deleted while trying to test changed fit metadata; the
missing file correctly failed first. Restoring it isolates the intended provenance
assertion. `focused-initial.log` retains that failure; no production check weakened.

Before/after SHA-256/size comparison preserves **2,393 files**, including every
video/run, fit/checkpoint/scene, failed pose stage and AGENTS/runbook/specification.
No package install, model download, decoder, solve, optimizer, upload or push.
Remaining: M1 contract/coverage qualification, prescribed fixtures/reference/native
platforms, independent inspection/corpus and rights. Retained FP32 metrics and
previous evidence are not retroactively upgraded by this development evaluation.
