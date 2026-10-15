# Clang C++23 calibrated rendering — 2026-10-15 Asia/Hong_Kong

Scope/status: **M1/E1/E2 IN_PROGRESS**. The existing full native Clang profile
builds, passes **262/262** CPU checks, and matches GCC's default, all **80** and
reordered calibrated comparisons byte for byte. The viewer opens the complete
saved scene with zero Vulkan errors. No milestone, reference-hardware, other-OS
or full Q/P/N/E gate is certified. Cwd: repository root; parent **4a10a94**.
No implementation changed. Raw ignored/local root **R**:
`artifacts/validation/clang-camera-reference-20261015/`.

## Profile and identity

`R/profile-before.json` records the inspected cache and actual compile commands:
**Clang 23.1.1, explicit `-std=c++23`, Debug, viewer ON, no sanitizer**, existing
warnings including `-Werror`. `readelf` confirms **libstdc++.so.6**, not libc++.
The C++26 development profile remains separate and unchanged. Build regeneration
uses the existing dependency cache; no dependency or standard was replaced.

| Bound identity | SHA-256 |
|---|---|
| Clang native diagnostic | `ad5b0ab69eb305038b5517c55345d8ca61799b35037a976a2a141a253c3a2e5c` |
| Clang viewer | `d04aeef7192fb3ce8089b5d8ad2f7ec0f8593d3d34e2f93841e2e66bd284e5d5` |
| Source video | `4448a1d9eb93f8b7fc787f9ea037d282128438a2512fad8eb8df8544f53906da` |
| Completed fit identity | `3b7da295d475bf838651ea38d9279a30c32e88314f25d71d4120665913bf0ed6` |
| Canonical scene content | `14787aa730211790d4b8310fb61093f2125740bc399497cacc59ff8b842a6cfe` |
| R/all-80/comparison.json | `ad3643dac80a253c2bb728fea7cd4f88a83de0099a77ce99eb6d10e7b41ec182` |

`R/profile-built.json` binds binaries, the projection shader and all five
diagnostic/test sources. The same retained **7k-step / 648,332-splat** SH3 fit,
camera intrinsics/poses and uncompressed scene are used throughout. Arbitrary
scale and unavailable geometry/walking remain explicit.

## Actual checks

`R/run_checks.py` SHA
`0e490e1a8c6cddd9b5fc1699f61a92ed315943572534e925d5a48447a8b98656`
records exact arguments, cwd, exit, elapsed time, executable/source hashes and
selected native display/cache environment in `LABEL-command.json` / `LABEL.log`.
`R/run_matrix.py` SHA
`ea193ed1f8c98e7dfe5f9b05f5c7471d240d089460205445b1553bb039a55f8c`
defines the unchanged default, complete 0–79 CSV and explicit `79,0,40` cases.
Native Vulkan/CUDA access was used, with fresh isolated home/cache directories.

| Commands / receipt label | Observed result |
|---|---|
| `cmake --build build/clang-viewer-cxx23 -j 4`, `build` | **0**, full viewer/CLI/diagnostic/core build, **31.71 s** |
| `ctest --test-dir build/clang-viewer-cxx23 --output-on-failure`, `ctest` | **262/262**, **103.42 s** CTest total; detailed `ctest-detail.log` includes **133 worker cases** on Python 3.14.8 |
| `build/clang-viewer-cxx23/we_core_tests 'calibrated diagnostic*'`, `focused-cpp` | **2 cases / 174 assertions** |
| `.venv-train/bin/python tests/gpu/reference_input_tests.py`, `input-tests` | **18/18**, CPU admission fixtures, no GPU execution |
| `python3 R/reject_selection.py`, `rejections` | **Six expected exit-2** native CLI rejections before SDL/GPU access; invalid driver selected, no fixture/native result published |
| `python3 R/run_matrix.py`, `native-default` / `compare-default` | Both **0**, three passing views, **19.22 / 10.10 s** |
| Same matrix, `native-all-80` / `compare-all-80` | Both **0**, **80/80**, **60.25 / 78.79 s**, complete count, **81 submitted native frames / zero Vulkan errors** |
| Same matrix, `native-reordered` / `compare-reordered` | Both **0**, three passing views, **10.09 / 7.73 s**; frame 79 admission origin |
| `python3 R/run_viewer.py` | **0**, full saved scene, actual **340 frames / 648,332 resident splats / zero Vulkan errors**, **9.90 s**; scene/binary/screenshot-bound `viewer-command.json` |
| `clang-tidy -p build/clang-viewer-cxx23 tests/gpu/reference.cpp tests/unit/fit_view_selection_tests.cpp`, `tidy` | **0**, 23.1.1, no first-party diagnostics or new suppressions |
| `python3 R/run_checks.py after`; docs / boundaries / diff | All **0**; `preservation-after.json`, `docs`, `boundaries`, `diff` receipts/logs |

Linux native **RTX 4060 Laptop / driver 615.78.08**, not H_R/H_V/H_M. Reference
environment: Python 3.10.22, gsplat 1.5.3+pt24cu124, torch 2.4.1+cu124, CUDA 12.4.
No compiler/test/render failure occurred. Optional PPM conversion initially used
system Python without Pillow; the existing training interpreter converted it
successfully, without installation. CPU checks and GPU work overlapped; these
durations are not FPS, throughput, P1/P2/P3 or N1 measurements. GCC suites,
formatting, sanitizers and coverage were not rerun for this evidence-only slice.

## Comparison and preservation

All **80** views at **718×1280** retain **62.859553–63.386695 dB** PSNR,
**0.999530852–0.999643683** SSIM and maximum **one RGB8 level** error. The fixed
diagnostic limits remain ≥45 dB / ≤8 levels. `R/per-camera.csv` preserves every
frame, score and native hash. `cross-compiler-readbacks.json` and
`cross-compiler-reports.json` verify the [preceding GCC results](2026-10-15-all-camera-reference.md):
default/all-80 fixtures, binary splat rows and readbacks match; all three complete
comparison reports also match byte for byte. This does not compare captured
ground truth or qualify arbitrary views, Q3 novelty or Q6 packing.

The inspected Source-view screenshot is **1428×1746**, the actual capture extent,
with scale/coverage/gravity/walk limitations visible. The command's `--frames 114`
target yielded the **340** frames reported above; no frame-rate inference is made.
Sampled renderer-owned allocation is **388,398,240 bytes** for the 80-camera
diagnostic, **460,840,416 bytes** for the viewer; neither is whole-device peak VRAM.
Viewer framebuffer allocation is **79,785,216 bytes**. No control/latency test or
equivalence with the preceding differently sized viewer screenshot is claimed.

The cut-off happened after the native matrix completed; retained receipts were
checked before continuing, without rerunning the successful matrix. Before/after
SHA-256/size checks preserve **2,088 protected files / 697 preceding evidence
files / 94 src/include files**. Both completed scenes and failed sample_2 stages
are unchanged. No decode, solve, optimizer/checkpoint load, installation, download,
upload or push occurred. M1's prescribed ≤250k consented fixture, reference/native
platform hardware and rights remain open. [Progress](../progress.md) owns next work.
