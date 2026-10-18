# Resized neighbor MVS experiment — 2026-10-18 Asia/Hong_Kong

Scope/status: **M3 IN_PROGRESS**. A bounded nine-view development recipe produces
pose-admitted depth; no full reconstruction, fusion, measured geometry, Q5/N2 or
scene/walking gate. Parent **39a5cf7**; implementation is the commit containing
this report. Cwd: repository root. Ignored evidence **R**:
`artifacts/validation/m3-dense-neighbors-20261018`.

## Behavior and identities

The [neighbor worker](../../workers/dense/README.md#resized-neighbor-experiment)
checks native/resized manifests and exact recipe/code/environment identities,
adapts an owned COLMAP model, verifies calibration/poses, and runs nine consecutive
views with the preceding/following three selected views as sources. It retains
the pinned PatchMatch filters, five iterations and two required consistent
sources. Confidence now divides by each reference's actual 3–6 selected sources;
support includes the reference and remains an actual count. Unknowns stay zero.
The existing two-source encoder default retains the native pilot policy.

All **63 sample_1 views**, **441,769 keypoints**, **190,667 tracked keypoints** and
**29,846 landmarks** are checked independently after model serialization. Keypoint
scaling is exact; maximum reprojection scaling error is **4.54747e-13 pixels**.
All poses, track links, landmark positions/colors remain unchanged. The owned
camera is PINHOLE at **2048×1102** with separate X/Y focal lengths. Pixel-based
point errors are recomputed; no pose solve or fit occurs. See
`independent-model.json` and the [upstream camera domain](https://github.com/colmap/colmap/blob/4.2.1/src/colmap/scene/camera.cc)
and [PatchMatch input checks](https://github.com/colmap/colmap/blob/4.2.1/src/colmap/mvs/patch_match.cc).

| Selected input | SHA-256 |
|---|---|
| Native manifest | `495084082f7e1f5e272c700aa836d1143f2352320a1e85d8b8221fd7276e48f8` |
| Resized manifest | `9e0eb770c9b5c5cc4710d64b31911580a98144fbb511871a1a0bfa3bc0a2ea6d` |
| Full neighbor recipe | `2c2e39c9f41e8df9797adb001d732094a0e469b80d694acff080b139ff806efd` |
| Five-file worker bundle | `fca53f70564b84e4d13fe62a6bf201333bbc84dccda0b98c261af09dd9f76645` |

Adding the selected-source parameter changes the shared encoder's code hash;
new recipe/preparation bytes are used. Previous native/resize-only recipes,
outputs and qualification scope remain unchanged; no historical result is
rebound to current code. Native-pilot reconstruction is not rerun.

## Actual local results

Linux x86-64; NVIDIA RTX 4060 Laptop GPU, **8188 MiB**, driver **615.78.08**.
The separate `.venv-dense` remains COLMAP CUDA **4.2.1**, NumPy **2.2.6**, Pillow
**11.3.0** and the other three pinned packages. Fresh RECORD-checked audit:
**six packages / 1,188 listed files**, requirements match (`installed.json`).
No installation or model download occurs; redistribution remains unqualified.

The real **nine-view worker completes in 213.754 s**, including its preparation
and export; solver timer is 3.419 minutes. This is neither whole-video P1 nor a
reference-hardware benchmark. Owner timeout **900 s**, six-second cancellation
grace, forced kill/reap fallback; no timeout/kill occurs in the successful run.
Seventy-one approximately three-second samples observe maximum **554 MiB global
GPU use** and **1,106,588 KiB process VmHWM**. These are sampled observations,
with the desktop active, not isolated allocation peaks or budget qualification.
`real-execution.json` retains argv, exits, timestamps and samples; raw backend
output remains in `real-gpu.log` / `real-pilot/stereo`.

All **nine depth assets pass native C++ input/pose admission at default limits**;
all nine wrong-frame expectations reject. Independent whole-plane comparison
confirms exact depth/validity/support/confidence export and zero-normalized
unknowns: **1,537,092 valid pixel observations**, per-view coverage
**3.34034–9.89244%**, support **3–7 including reference**. `independent-depth.json`
retains per-frame hashes/coverage/axial-depth quantiles; `admit-result.stdout`
retains native admission. This sparse coverage is not a usable room mesh.
Changed grids/views prevent treating it as an equal-workload quality comparison
with the earlier three-view pilot.

Read-only `parallax.json` reports only **3.656–7.612%** of each reference's sparse
landmarks have two currently selected co-visible sources at the unchanged
**3°** filter angle. Several later views have more shared landmarks with useful
parallax. This suggests the consecutive temporal window limits support; it does
not explain every invalid dense pixel or establish dense coverage/ground truth.
No threshold or source configuration was changed after observing the result.

## Commands, safety checks and limits

All successful checks below exit **0**; exact setup/GPU/admission argv and cwd
are retained in `setup-commands.json`, `real-execution.json`, `admit-commands.json`.
Toolchains remain GCC **16.2.1 C++26 Debug**, explicit GCC **C++23 CPU Debug**;
no production C++ code changed and no new native viewer/platform run is claimed.

| Actual command (repository root) | Result / log under R |
|---|---|
| `cmake --build --preset dev -j 4`; `ctest --preset dev --output-on-failure -j 4` | **308/308**, **28.19 s**; `build-dev.log`, `ctest-dev.log` |
| `cmake --preset compat-cxx23 -DWE_BUILD_VIEWER=OFF`; `cmake --build --preset compat-cxx23 -j 4`; `ctest --preset compat-cxx23 --output-on-failure -j 4` | **285/285**, **24.64 s**; `configure-cxx23.log`, `build-cxx23.log`, `ctest-cxx23.log` |
| `.venv-dense/bin/python -m unittest discover -s tests/integration -p 'worker_dense*_test.py' -v` | **23/23**, no skips; `unit-dense.log`. Includes five new synthetic selection, actual model round-trip, adverse calibration/pose/grid/cancellation and support-fraction checks |
| `.venv-dense/bin/python tools/audit_worker_environment.py --requirements workers/dense/environment.txt --output artifacts/validation/m3-dense-neighbors-20261018/installed.json` | Fresh six-package installed audit; `installed-audit.log` |
| `.venv-dense/bin/python artifacts/validation/m3-dense-neighbors-20261018/setup_real.py` | New full recipe, all 63 resized images, native admission and prepare-only worker; `setup-real.log` |
| `.venv-dense/bin/python artifacts/validation/m3-dense-neighbors-20261018/check_model.py` | All model correspondence checks above; `independent-model.log` |
| Native `python3 artifacts/validation/m3-dense-neighbors-20261018/run_gpu.py` | Bounded real worker, exit **0**; `gpu-owner.log` |
| `.venv-dense/bin/python artifacts/validation/m3-dense-neighbors-20261018/check_depth.py`; `python3 artifacts/validation/m3-dense-neighbors-20261018/admit.py` | Independent complete-plane parity and C++ pose admission; `independent-depth.log`, `admit-final.log` |

Seven entry probes reject wrong native/resized/recipe hashes, input overlap,
occupied/dangling outputs and rehashed calibration forgery without publishing a
new workspace (`entry-checks.json`). CPU preparation cancellation is checked in
an owned subprocess after native-copy ownership: no prepared/result marker is
published (`cancel-preparation.json`). GPU cooperative cancellation and shared
worker protocol/resume remain unqualified; the prior forced-kill observation is
not upgraded. Private intermediate workspaces are not resumable completed jobs.

The preparation cancellation returns **130** in **0.114 s** after SIGINT, without
forced kill. This narrow CPU result does not certify GPU cooperative cancellation.
The native admission harness is compiled from the development compile database;
format/repository style gate and separate clang-tidy **23.1.1**, warnings as
errors, check that ignored C++ harness. `harness-style.log`,
`harness-tidy-command.json` and `harness-tidy.log` retain commands/results.
Initial tidy found an uncaught exception at the harness entry point; its explicit
exception boundary fixes that failure without warning suppression
(`harness-tidy-first.log`). No new sanitizer or renderer check is inferred.

Final `audit.json` combines **1,967 pre-task entries** with the preceding dense
baseline: **22,331 distinct protected entries**, zero missing/changed, including
saved sources/runs, both frozen fixtures, old dense/resize outputs and the
pose/training environments. Current worker bytes reproduce the frozen recipe's
bundle digest. `python3 tools/check_docs.py` and `git diff --check` pass.
`python3 tools/check_dependency_manifest.py --local-evidence` still exits **2**,
`Changed evidence: cmake/dependencies.cmake` (`dependency-current.log`). Previously
stale current-byte runtime/mapping/owner receipts and redistribution rights remain
open; no receipts were edited to claim a pass.

Production stage/control integration, parallax-aware selection, full-scene depth,
fusion/compressed storage, measured scale, collision/walking and mesh export remain
open. A0–A6/AP gates stay unstarted; M4 has not begun. No media upload, remote push,
unchanged sample_2/three-view retry, completed refit or long-stress rerun occurs.
