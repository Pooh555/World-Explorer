# Captured-domain composition — 2026-10-12, Asia/Hong_Kong

Scope: M1/M2/P4/E1 **IN_PROGRESS**. Premature per-layer UNORM clamping is corrected
with FP32 splat accumulation → SDR clamp → UI. Bright SH energy stays in the
training color domain; no exposure/gamma correction. Buffer/image allocation
requirements, staging/replacement overlap and framebuffer residency are bounded.
Unsupported FP32 blending fails explicitly; [ADR 006](../adr/006-fp32-capture-compositing.md)
records the unqualified portable-device implications. Exclusive termination still
fails a separate fixture; no milestone or Q/P/N/E gate is certified.

## Identity and environment

All commands use cwd `/home/Pooh555/programming/World-Explorer`. Parent
`b13833a9abc20119b102779eead23878a2338b42`; dirty source inventory SHA-256
`9056b1b3b1dc97dc69ac0e7cccb8a53085f855b8c3e0549c8605a6a121d1c77c`.
Raw prefix **R**: ignored `artifacts/validation/compositing-20261012/`.
`before-identity.json`/`after-identity.json` retain source, binary and shader hashes;
comparison JSONs retain reference/metric/package hashes and per-view results.

Arch Linux, GCC 16.2.1 C++26; independent Clang 23.1.1/libstdc++ C++23 viewer;
SDL3/system Vulkan SDK 1.4.363, glslc 2026.4, format/tidy 23.1.1. Native RTX 4060
Laptop / driver 615.78.08 is **not H_R/H_V**. Reference environment unchanged:
Python 3.10.22, gsplat 1.5.3+pt24cu124, PyTorch 2.4.1+cu124/CUDA 12.4.
No downloads/training/LPIPS. Other platforms, sanitizer and reference-performance
results are not inferred from these runs.

## Commands and passing observations

```sh
cmake --build --preset dev -j 4
ctest --preset dev --output-on-failure
cmake --preset compat-cxx23 -B build/clang-viewer-cxx23 -DWE_BUILD_VIEWER=ON -DCMAKE_CXX_COMPILER=clang++
cmake --build build/clang-viewer-cxx23 -j 4
ctest --test-dir build/clang-viewer-cxx23 --output-on-failure
build/dev/we_gpu_reference artifacts/validation/compositing-20261012/after-synchronized --compositing
.venv-train/bin/python tests/gpu/compare_reference.py artifacts/validation/compositing-20261012/after-synchronized
build/dev/we_gpu_reference artifacts/validation/compositing-20261012/original
.venv-train/bin/python tests/gpu/compare_reference.py artifacts/validation/compositing-20261012/original
build/dev/we_gpu_smoke artifacts/validation/compositing-20261012/smoke-hidden.ppm
build/clang-viewer-cxx23/we_gpu_smoke artifacts/validation/compositing-20261012/smoke-clang.ppm
```

All exit 0; GPU commands use native driver access. Output directories must be new.
Both viewer/core profiles pass **91/91**. R logs: `build-resize.log`,
`tests-dev-verified.log`, `configure-clang-viewer.log`, `build-clang-final.log`,
`tests-clang-verified.log`. Targeted tidy on renderer/device/swapchain, viewer main
and both GPU harnesses has no unsuppressed warnings (`tidy-verified.log`). Changed
first-party C++ passes `clang-format --dry-run --Werror`; boundaries, documentation
links and `git diff --check` exit 0.

Stress fixture: **27 synthetic splats**, bright rear/dark front layers, 24-layer
opacity stack; seven 1280×720 cameras include orbit and close-up. Before/after
fixture JSON is identical, SHA-256
`c7288fec1546bde062eeb1317846ac386c8dd98ae977a09bb3c145d4608f2066`.
Original 18-splat/four-view fixture is unchanged from v3, SHA-256
`970273807404e7ac50e984ce8557386f4aa599a9022627df7a24178ca19d2e6f`.
Full-frame display-encoded RGB comparison, no crop; fixed PSNR ≥45 dB / maximum
eight 8-bit levels, unchanged from [earlier evidence](2026-10-12-render-reference.md).

| Stress view | Before PSNR / max levels | FP32 PSNR / max levels |
|---|---:|---:|
| Centered | 24.9450 / 201 | 67.8840 / 1 |
| Off-center | 25.0693 / 201 | 68.0101 / 1 |
| Rolled | 25.0694 / 201 | 67.9868 / 4 |
| Translated | 25.4024 / 202 | 68.5047 / 6 |
| Orbit left | 25.0154 / 196 | 68.2267 / 6 |
| Orbit right | 28.2573 / 176 | 69.0943 / 5 |
| Close-up | 21.6575 / 194 | 63.8831 / 3 |

All seven pass; SSIM 0.999611–0.999881. Original four pass at 63.0099–65.1404 dB,
max one level. Float references, PNGs, ×32 heatmaps and hashes remain in each
directory; centered native image inspected. Both native smoke profiles report RGB
**130,3,68**, 51 frames, zero errors; actual **512×512 → 640×360 → 512×512** extents
are asserted in a hidden window. Replace/delete, pause/resume, allocation accounting
and rejected 1 MiB budget/partial cleanup pass; final framebuffer bytes **12,582,912**.
The earlier visible-window smoke remains logged separately; requested resize alone
is no longer treated as observed resize.

## Preserved failures and real viewer

Baseline stress comparison exits 2 (all seven fail, `reference-before.log`). First
FP32 runs (`native-after.log`, `native-fixed.log`) each detect eight store/layout
hazards. `NONE` does not avoid stores for a written attachment; the final dependency
covers fragment input reads **and color-output store/discard writes**, as required
by [Vulkan semantics](https://docs.vulkan.org/refpages/latest/refpages/source/VkAttachmentStoreOp.html).
Associated premature comparison attempts fail for missing fixture metadata; no pass
is inferred. Typed flag and multiplication tidy warnings were corrected, not disabled.

```sh
build/dev/we_gpu_reference artifacts/validation/compositing-20261012/termination --termination
.venv-train/bin/python tests/gpu/compare_reference.py artifacts/validation/compositing-20261012/termination
```

Native exits 0/zero validation errors; comparison exits **2**. Two normalized-color
splats with opacities 0.95/1 expose pinned exclusive `next_T <= 1e-4` behavior.
Fixture SHA-256 `da8ad6437ad004a0cfdcb34de6fe11f630b03bba35b155882aa167118e2be150`.
Centered/off-center/rolled max **12**, translated **14**, close-up **15** fail;
orbit views max one pass. PSNR 60.76–72.08 dB hides the local mismatch. All seven
views/failures and thresholds remain; no trainer patch or waived comparison.

```sh
build/dev/we_viewer --scene artifacts/sample2.wexplore --source-view --max-splats 500000 --frames 1 --screenshot artifacts/validation/compositing-20261012/real-view.ppm --settings artifacts/validation/compositing-20261012/real-view.json
```

Exit 0: **304,867 splats**, 228 frames, zero validation errors, **301,197,072** owned
allocation bytes including **80,855,040** framebuffer bytes. Screenshot inspected
after lossless PNG conversion; geometry/walk unavailable. This is spectator
inspection, not calibrated real parity, FPS or peak VRAM. No original job/optimizer
rerun; authorized capture SHA remains `51263d9991dcbc2136d0809ecba1a8ba1b261896f3b222b7afba19784a153ef6`.
Next: bounded front-to-back termination, then calibrated real comparison and actual
portable/reference qualification. Technical plan/runbook/private assets unchanged.
