# Fullscreen residency and indexed tiles — 2026-10-13 Asia/Hong_Kong

Scope: immediate real-scene fullscreen/lag regression; M1/M2, P2–P4/P6, E1.
Implemented and locally checked; no milestone, quality/performance gate or other
native platform is certified. Cwd: repository root. Raw evidence root `R` is
`artifacts/validation/viewer-regression-20261013` (local/ignored). In commands,
set `R=artifacts/validation/viewer-regression-20261013`; `<case>` denotes the
explicit mode mapping in the table.

## Cause and correction

Parent `02bc1a8` scans globally sorted splats for every tile, eagerly reserves
maximum-count scratch, and duplicates completed FP32 pixel buffers in extra FP32
swapchain attachments. Baseline `before.json/log` exits 2 at 4K:
`floating framebuffer exceeds remaining residency budget`.

[ADR 008](../adr/008-indexed-tile-residency.md) implements GPU overlap count/scan,
stable reference emission/sorting/ranges, parallel bucket scans, resident-count
scratch and direct FP32-to-SDR presentation with UI in subpass 0. Saturating count
readback precedes emission. Disjoint tile rectangles bound each reference list;
no splats or pixels are dropped. Actual allocation sizes govern admission, with
SDR readback headroom. Completed count timelines allow both cached frame pools to
be trimmed when histogram growth changes their shares.

## Real-scene measurements

Full **304,867 splats**, no LOD/trimming, max splats 1,000,000, unchanged 512 MiB
budget, FIFO, Wayland, RTX 4060 Laptop / driver 615.78.08, Arch Linux.
Eight warmup + 24 measured tiny-yaw frames per phase; final completion is awaited.
No training workload ran concurrently; comparisons ran separately. Desktop/compositor activity and thermal
state are uncontrolled. Rates are **GPU-completed render frames**, not presented
FPS. Memory is the maximum tracked owned-buffer count sampled after draws,
not transient peak/process RSS/total device VRAM.

| Case | Pixels | Completed frames/s | CPU draw p50/p95/p99 ms | Owned bytes | Tile references |
|---|---|---:|---|---:|---:|
| Baseline | 1280×720 | 5.88 | 156.93/157.10/157.16 | 361,269,136 | Unrecorded |
| Final window | 1280×720 | 59.30 | 16.64/16.83/17.02 | 228,405,632 | 2,440,103 |
| Final 4K | 3840×2160 | 35.85 | 27.60/28.84/28.87 | 489,622,464 | 13,748,984 |
| Final fullscreen | 2880×1800 | 46.31 | 21.26/22.16/22.57 | 474,294,528 | 9,369,155 |
| Final restored | 1428×1746 | 58.72 | 16.59/17.31/17.64 | 364,235,136 | 5,057,711 |

`after-counted.json/log` exits 0; each phase confirms 24 submitted render frames: actual fullscreen flag observed; restored flag
cleared and renderer extent matches window-manager-selected pixels. Initial 720p
and hidden 4K dimensions are strict. Real 4K screenshot readback succeeds within
budget. All phases retain the full count and report **zero Vulkan/sync errors**.
Earlier passing `after-final.json` recorded 59.39/26.04/37.86 frames/s
(window/4K/fullscreen); it predates the cached-pool review. `after-pools.json`
recorded 58.89/16.58/37.43 and restored to 707×427. Retain this substantial short-run
variation; counter validation is a harness-only addition. None of these runs satisfies P2/P3's workload, duration, repetitions or hardware.
Whole-frame references exceed 8M at 4K/fullscreen; each emitted batch stays bounded.

## Checks and commands

GCC 16.2.1 C++26 / Clang 23.1.1 explicit C++23 with libstdc++; CMake 4.4.4,
SDL 3.4.18, Vulkan headers 1.4.363. Commands below exit 0 unless noted above.

| Command(s) | Result / raw evidence in R |
|---|---|
| `cmake --build --preset dev -j 4`; `cmake --build build/clang-viewer-cxx23 -j 4` | Full native builds; final `build-harness-verified.log`, `build-clang-harness-verified.log`; pool builds retained |
| `ctest --preset dev --output-on-failure`; `ctest --test-dir build/clang-viewer-cxx23 --output-on-failure` | **91/91 each**, `ctest-dev.log`, `ctest-clang.log`; CPU/worker sources unchanged by final pool review |
| `build/dev/we_gpu_viewer_regression artifacts/sample2.wexplore $R/after-counted.json` | Four real phases above, `after-counted.log`; baseline used `$R/before.json` |
| `build/dev/we_gpu_smoke $R/smoke-pools.ppm`; `build/clang-viewer-cxx23/we_gpu_smoke $R/smoke-clang-pools.ppm` | Overlap pixel 130,3,68; pause/resume, replace/delete, observed resize/restore, allocation rejection and single-tile overflow rejection before emission; 51 submitted frames, zero validation errors; matching `.log` |
| `build/dev/we_gpu_reference $R/<case> [mode]`; `.venv-train/bin/python tests/gpu/compare_reference.py $R/<case>` | Cases `original-final` (no mode), `compositing-final` (`--compositing`), `termination-final` (`--termination`), `batches-final` (`--batches`), `batches-bounded` and final `batches-pools` (`--batches-bounded`); `<case>.log`, `<case>-compare.log`, `<case>/comparison.json` |
| `build/dev/we_viewer --scene artifacts/sample2.wexplore --source-view --max-splats 1000000 --frames 12 --screenshot $R/viewer-final.ppm --settings $R/viewer-settings.json` | Real GUI 1428×868, 304,867 splats, zero errors; overlay/screenshot inspected; `viewer-final.log`; before final pool-only review |
| `clang-tidy -p build/dev` on six changed `.cpp` files; final smoke/renderer rechecks | No remaining first-party diagnostics; `tidy-verification.log` retains corrected move warning, `tidy-smoke-final.log`, `tidy-pool-verified.log`, `tidy-harness-verified.log` |
| `clang-format --dry-run --Werror` on 11 changed C++ files; `python3 tools/check_boundaries.py`; `python3 tools/check_docs.py`; `git diff --check` | Formatting 23.1.1, boundaries, documentation and whitespace pass; `format.log`, `final-checks.log` |

Pinned local gsplat 1.5.3+pt24cu124 / Torch 2.4.1+cu124 / CUDA 12.4 comparisons
pass **32 views** at unchanged PSNR ≥45 dB / maximum eight color levels.
Observed minimum PSNR: original 63.01, compositing 64.37, termination 65.23,
batches 65.09 dB; maximum **one level** throughout. Bounded mode uses identical
1,027-splat fixtures/views with a 2,048-reference cap; all seven PPMs are
byte-identical to normal batching, including after the final pool review.
These are synthetic diagnostics, not Q6 or calibrated full-real-scene parity.

## Identities and retained failures

`R/final-identity.json` hashes changed source, harnesses, binaries, SPIR-V,
comparison script and unchanged specification/runbook; SHA-256
`1a8bf3f64466e0b4370455a122881bbb6b0e6e31a42b419cd275ab620a844580`.
Baseline inventory `before-identity.json`:
`d4610a10c48f42614a6bdf0cb837bd272b9ab35f2dbeedc9b8559165038885e2`.
Final report SHA-256: `62ed4766192858a8930c2007f1974943f45522d06e3312f87eb8bec0d7d24e28`.
Real scene content: `35ed89b5abb2ac9eb98b110f1bcd4dab53d1aed70c133623e764ffeffcba01d0`;
source/fit/pose assets were not rewritten or retrained.

Fixture JSON hashes remain byte-identical to [prior evidence](2026-10-12-tiled-composition.md):

| Fixture | SHA-256 |
|---|---|
| Original | `970273807404e7ac50e984ce8557386f4aa599a9022627df7a24178ca19d2e6f` |
| Compositing | `c7288fec1546bde062eeb1317846ac386c8dd98ae977a09bb3c145d4608f2066` |
| Termination | `da8ad6437ad004a0cfdcb34de6fe11f630b03bba35b155882aa167118e2be150` |
| Batches, including bounded | `3ef41c83db49c37bc89929b942c889083b7f024f12e2cd3236a175bc4480227a` |

Intermediate JSON/logs remain: `after-first` rejects >8M references (exit 2); `after-bands`
adds row batching but exits 0 without verifying fullscreen flags, so is not
fullscreen evidence; `after-headroom`,
`after-rects`, `after-settled` fail actual fullscreen assertions (exit 2); `after-mapped`
observes fullscreen but fails a wrongly assumed restored size. The corrected
harness commits visible buffers before mapping, then checks actual SDL flags/pixel
extents: [SDL Wayland contract](https://wiki.libsdl.org/SDL3/README-wayland).
Retained build/lint failures expose an unused device diagnostics member, one
outdated helper invocation, convertible adjacent helper arguments and escaping
harness exceptions; code fixes preserve warnings and failure handling.

Remaining: host count synchronization, high-resolution sorting/overdraw latency,
full calibrated real parity, long lifetime/device-loss stress, native AMD/Apple/
Windows viewer qualification and prescribed reference-hardware performance.
M1 remains IN_PROGRESS; M7–M8 remain deferred. Next work is in [progress](../progress.md).
