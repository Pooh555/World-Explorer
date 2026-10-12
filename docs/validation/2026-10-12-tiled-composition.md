# Bounded tile composition — 2026-10-12, Asia/Hong_Kong

Scope: M1/M2, E1 and P4 development checks; **IN_PROGRESS**. Pinned exclusive
termination is fixed on local NVIDIA/Arch. No complete Q/P/N/E or milestone gate
is certified. Specification/runbook unchanged; M7–M8 remain deferred.

Identity: parent `20fd9663609dda3e7c925e37816a8354b5d13262`; final source inventory
SHA-256 `8bb8ea81510ca08c855cd840219ef88db803e25d0f940c4fed78e2e07e578892`.
Cwd: repository root. Local raw root `R = artifacts/validation/tiled-composition-20261012`;
`R/identity.json` records source, binaries, eight SPIR-V modules and capture hashes.
GCC 16.2.1/C++26; Clang 23.1.1/libstdc++/explicit C++23; CMake 4.4.4;
SDL 3.4.18/Vulkan SDK 1.4.363. Native RTX 4060 Laptop/driver 615.78.08;
this is **not H_R/H_V**. CUDA reference: gsplat `1.5.3+pt24cu124`,
Torch `2.4.1+cu124`; comparison JSONs preserve package/script hashes.

## Behavior and comparisons

[ADR 007](../adr/007-tiled-batch-compositing.md) replaces quad blending with stable
front-to-back 16×16 compute groups and fixed 256-splat shared batches. Tile
compaction preserves depth order. Exclusive cutoff retains preceding transmittance
for background. FP32 pixel scratch uses actual allocation charges, checked storage/
dispatch/shared limits and timeline/idle retirement. FP32/SDR/UI presentation stays;
FP32 attachment blending support is no longer required. The overlap index remains
pending; this implementation scans sorted splats per active tile.

Native command: `build/dev/we_gpu_reference <directory> [flag]`.
Reference command: `.venv-train/bin/python tests/gpu/compare_reference.py <directory>`.
Both exit **0** for every row. Native validation errors: **0**. Each view is 1280×720;
no crops, trimming, exposure fitting, trainer changes or tolerance changes.

| Synthetic fixture / directory under R | Flag | Splats / views | PSNR dB range | Max color levels |
|---|---|---|---|---|
| `termination-final` | `--termination` | 2 / 7 | 65.23–72.05 | 1 |
| `compositing-final` | `--compositing` | 27 / 7 | 64.37–69.00 | 1 |
| `original-final` | none | 18 / 4 | 63.01–65.14 | 1 |
| `batches` | `--batches` | 1,027 / 7 | 65.09–72.57 | 1 |

All **25 views pass** unchanged diagnostic thresholds: PSNR ≥45 dB and maximum
error ≤8 levels. Larger fixture checks shared-memory reuse, sparse compaction and
cross-batch termination; JSON capacity explicitly increases from 1 to 4 MiB
(its actual size is 1,342,580 bytes). Each directory retains float references,
quantized images, error heatmaps, `native.json` and `comparison.json`;
`R/native-<case>-final.log` / `reference-<case>-final.log` cover the first three,
`R/native-batches.log` / `reference-batches.log` the fourth.

Fixture JSON SHA-256s (first three byte-identical to retained earlier inputs):

```text
termination da8ad6437ad004a0cfdcb34de6fe11f630b03bba35b155882aa167118e2be150
composition c7288fec1546bde062eeb1317846ac386c8dd98ae977a09bb3c145d4608f2066
original    970273807404e7ac50e984ce8557386f4aa599a9022627df7a24178ca19d2e6f
batches     3ef41c83db49c37bc89929b942c889083b7f024f12e2cd3236a175bc4480227a
```

## Build, lifetime and real scene

[DEV commands](checks-2026-10-12.md): build/CTest **0**, **91/91**.
`cmake --build build/clang-viewer-cxx23 -j 4` and
`ctest --test-dir build/clang-viewer-cxx23 --output-on-failure`: **0**, **91/91**.
Final build logs: `build-{dev,clang}-batches.log`; tests: `tests-{dev,clang}-final.log`.
Five changed C++ files pass clang-format 23.1.1; scoped clang-tidy 23.1.1 checks
renderer/device/swapchain/smoke/reference with no unsuppressed diagnostics
(`tidy.log`, `tidy-final.log`). Boundaries, docs and diff whitespace checks exit 0.

`build/dev/we_gpu_smoke R/smoke.ppm` and
`build/clang-viewer-cxx23/we_gpu_smoke R/smoke-clang.ppm`: **0**, 51 frames each,
overlap RGB **130,3,68**, observed 640×360→512×512 resize/restore, pause/resume,
replace/delete, **0** validation errors. Insufficient framebuffer and pixel-scratch
budgets fail before allocation; scratch accounting is checked. Logs: `smoke.log`,
`smoke-clang.log`. Final framebuffer allocation: **12,582,912 bytes**.

`build/dev/we_viewer --scene artifacts/sample2.wexplore --source-view --max-splats 500000 --frames 1 --screenshot R/real-view.ppm --settings R/real-view.json`:
**0**, full **304,867 splats**, 54 submitted frames, **0** validation errors.
Native 1428×868 screenshot inspected through lossless PNG conversion; room and
arbitrary-scale/no-walk UI visible. Recorded current owned allocations:
**321,421,584 bytes**, including **61,415,424** framebuffer bytes; not peak total
VRAM or a performance measurement. Original solver/fit/cache untouched; private
capture hash remains `51263d9991dcbc2136d0809ecba1a8ba1b261896f3b222b7afba19784a153ef6`.

## Failures and remaining scope

Before correction, rerunning the [retained cutoff fixture](2026-10-12-compositing.md)
exits **2**, five views fail at 12–15 levels; original images/failure records remain.
Reproduction log: previous raw root `reference-termination-reproduced.log`.
Initial shader build rejected reserved identifier `active`; renamed it without
changing arithmetic (`R/build-dev-initial-failed.log`). A premature pre-rebuild
native run remains explicitly separate in `R/pre-rebuild-termination` and its log.
Tidy found a shared-size constant's intermediate signed multiplication; explicit
wide arithmetic fixes it (`tidy-device-final.log` retains the finding).

Calibrated full real-scene parity, overlap indexing, P2/P3 traces, full peak memory,
two-hour lifetime stress, native AMD/Apple/Windows viewer/release and Q6 remain
unverified. No new ASan/TSan/libc++ or hosted-platform pass is claimed. Full real
fresh owned solve/fit chaining and corpus/reference hardware remain pending.
