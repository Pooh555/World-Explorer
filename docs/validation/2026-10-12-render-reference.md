# Fixed-camera rendering — 2026-10-12, Asia/Hong_Kong

Scope: M1/M2/E1 local correctness; **IN_PROGRESS**, no milestone/Q/P gate certified.
Explicit calibrated cameras preserve off-center intrinsics and roll independently
of spectator navigation. Invalid bases/clipping/float intrinsics and mismatched
framebuffer dimensions fail before update admission. Raster principal points now
convert canonical integer centers once, including covariance clipping. Corrected
the derivative margin from `0.3 * extent / focal` to pinned gsplat's
`0.3 * tan_fov = 0.15 * extent / focal`.

## Identity and scope

Cwd for every command: `/home/Pooh555/programming/World-Explorer`. Parent
`c25359e11a9b1c3e3656f8b4770105e0f84124ae`; dirty implementation inventory SHA-256
`c5b5558ac9c66fa539fd726fde6ecb4523b386a404003e932ae06c57b17d3732`.
Raw evidence prefix **R**: ignored `artifacts/validation/render-reference-20261012/`.
`after-identity.json` records source files, native/viewer binary and compiled shader
hashes; `before-v2-identity.json` preserves the failing version.

Fixture v2: **18 synthetic splats**, two chunks, degree-3 SH with all basis orders,
anisotropy, overlap, rotated/scaled chunk-local radiance, off-screen footprint,
near/far clipping; four **1280×720** cameras (centered, off-center, rolled,
translated). SHA-256 of identical before/after `fixture.json`:
`a4cc294d750a139b08c2e606874e415d702301f102571043758f279f8163d24b`.
Generated bundle has explicit synthetic provenance; no capture/training involved.

Arch Linux; GCC 16.2.1; Clang/libc++/format/tidy 23.1.1; glslc 2026.4,
Vulkan SDK 1.4.363; NVIDIA RTX 4060 Laptop / driver 615.78.08 (**not H_R/H_V**).
Reference: local Python 3.10.22, gsplat **1.5.3+pt24cu124**, PyTorch **2.4.1+cu124**,
CUDA 12.4; exact installed Python/native gsplat and SSIM hashes in
`after/comparison.json`. No downloads or LPIPS evaluation.

## Commands and observed results

[Dated commands](checks-2026-10-12.md): DEV **90/90**, COMPAT **87/87**, LIBCXX
**87/87**; builds and tests exit 0. R contains `build-after.log`, `tests-dev-final.log`,
`build-compat.log`, `tests-compat.log`, `build-libcxx.log`, `tests-libcxx.log`.
Three added CPU cases cover asymmetric projection/roll, invalid admission and
spectator resizing. Viewer-only tests remain excluded from CPU configurations.

```sh
build/dev/we_gpu_reference artifacts/validation/render-reference-20261012/after
.venv-train/bin/python tests/gpu/compare_reference.py artifacts/validation/render-reference-20261012/after
build/dev/we_gpu_smoke artifacts/validation/render-reference-20261012/smoke.ppm
clang-tidy -p build/dev src/render_core/camera.cpp src/render_vulkan/renderer.cpp tests/unit/render_camera_tests.cpp tests/gpu/reference.cpp
clang-format --dry-run --Werror include/world_explorer/render_core/camera.hpp src/render_core/camera.cpp src/render_vulkan/renderer.cpp tests/unit/render_camera_tests.cpp tests/gpu/reference.cpp
python3 tools/check_boundaries.py
python3 tools/check_docs.py
git diff --check
```

All exit 0. GPU commands used native driver access; output directory must be new.
`native-after.log` reports zero validation errors and checks wrong-extent rejection
preserves the pending update. `reference-final.log`/`after/comparison.json` compare
full-frame display-encoded RGB after 8-bit quantization; SSIM uses the existing
Gaussian-window implementation. Fixed diagnostic tolerances: PSNR ≥45 dB and
maximum eight color levels; these are **not Q2/Q3/Q6 thresholds**.

| View | Before PSNR / max levels | After PSNR / max levels | After SSIM |
|---|---:|---:|---:|
| Centered | 27.2155 / 32 | 54.7144 / 4 | 0.997832 |
| Off-center | 29.0279 / 28 | 54.8098 / 3 | 0.997775 |
| Rolled | 26.8308 / 35 | 54.7967 / 3 | 0.997691 |
| Translated | 38.5365 / 10 | 53.9023 / 1 | 0.997916 |

`before-v2/` retains all four failures (comparison exit 2); `after/` retains float
references, native/reference PNGs, ×32 absolute-error heatmaps and per-view hashes.
Off-center images/heatmap inspected. `smoke.log`: overlap RGB **130,4,68**, 51
frames, replace/delete/resize and pause/resume pass; zero Vulkan/sync errors.
Targeted tidy has no unsuppressed warnings (`tidy-corrected.log`).

Initial build logs retain missing enum/member names and narrowing diagnostics,
corrected without weakening warnings. Tidy's Catch `REQUIRE_FALSE` flag-enum false
positive was removed with an equivalent negated-value assertion. The initial mild
v1 edge fixture passed (47.88–49.39 dB, max 5–6); v2 strengthens that case before
the shader correction, with unchanged tolerances. The pinned packed wrapper rejects
a `[1,3]` background: reference rendering uses returned alpha to add the same
residual background, preserving packed/classic rasterization without patching it.

## Retained real viewer and remaining work

```sh
build/dev/we_viewer --scene artifacts/sample2.wexplore --source-view --max-splats 500000 --frames 1 --screenshot artifacts/validation/render-reference-20261012/real-view.ppm --settings artifacts/validation/render-reference-20261012/real-view.json
```

Exit 0; `real-view.log`: **304,867 splats**, 156 frames, 220,342,032 renderer-owned
allocated bytes, zero validation errors. Screenshot inspected after lossless PNG
conversion; this is an upright spectator inspection, not calibrated real parity,
FPS or peak-memory evidence. No original job/optimizer rerun; authorized video
SHA remains `51263d9991dcbc2136d0809ecba1a8ba1b261896f3b222b7afba19784a153ef6`.

Unverified: saturated radiance/UNORM intermediate rounding, early transmittance
termination, tie ordering/popping/close-up paths, real calibrated comparison,
Q6's packed 100 views, reference-hardware/native Windows/macOS performance and full
fresh reconstruction chain. Current sanitizer results remain historical; none were
rerun for this slice. Technical plan/runbook/private source assets unchanged.
