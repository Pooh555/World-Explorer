# Full retained scene camera comparison — 2026-10-13 Asia/Hong_Kong

Scope: M1/M2/E1 local calibrated renderer correctness, **IN_PROGRESS**. Three
original fit views of all **304,867 splats** pass pinned gsplat comparison at
1280×688, with maximum one color level and zero Vulkan validation errors.
No milestone, Q/P/N/E gate, reference hardware or other native OS is certified.
Cwd: repository root. Raw evidence root `R` is ignored/local
`artifacts/validation/real-reference-20261013`; set that path before commands below.

## Implementation and comparison scope

The diagnostic adds explicit `--real <bundle> <completed fit>` admission; synthetic
fixtures retain their labels, byte identities and fixed tolerances. It checks
scene/fit/capture/PLY provenance, original PLY checksum, complete count, supported
camera/world profile and bounds. Schema 2 exports hashed portable float32 rows
without embedding 304,867 splats in JSON. Reference admission checks exact payload
sizes, contained paths, finite values, transforms, original configuration identity
and first/middle/last camera correspondence. Nine CPU admission tests and four
native pre-GPU rejection probes exercise these boundaries.

Saved fit float32 rotation rows are only approximately orthonormal: derive camera
position with their actual inverse, preserving affine translation. Renderer push
rows carry camera-from-rebased-world translation; projection evaluates `R*p+t`.
Normalize Gaussian quaternions and form world covariance before transforming it
into camera covariance, following the installed pinned trainer's operations.

The viewer already uploads means relative to its admission camera, in double
precision before float32 conversion. Real reference input now declares that origin
and consumes the same rebased float32 scene representation with equivalent original
fit cameras. This is coordinate conversion, with no fitted image correction,
trimming, exposure adjustment or tolerance change. It does **not** establish
bitwise parity with the trainer's original absolute-world float32 arithmetic.
These are unpacked canonical renderer diagnostics, not Q6's packed 100-view gate
or capture-versus-ground-truth appearance measurements.

## Results and identities

Arch Linux, native RTX 4060 Laptop / driver 615.78.08 (**not H_R/H_V**).
GCC 16.2.1 C++26 and Clang 23.1.1 explicit C++23/libstdc++; format/tidy 23.1.1.
Local Python 3.10.22, gsplat 1.5.3+pt24cu124, Torch 2.4.1+cu124, CUDA 12.4;
installed wrapper/library hashes are in `real-checked/comparison.json`.
No training or downloads ran. Native renderer-owned allocation sampled after
draws: **225,227,264 bytes** under unchanged 512 MiB budget; not total/peak VRAM.

| Original fit frame | PSNR dB | SSIM | Maximum color levels |
|---|---:|---:|---:|
| 000001.png | 63.3244 | 0.999582 | 1 |
| 000032.png | 63.1929 | 0.999637 | 1 |
| 000063.png | 63.1650 | 0.999616 | 1 |

GCC and Clang final native PPMs are byte-identical to each other and `real-affine/`.
Native middle view visually inspected. All **32 synthetic views** pass unchanged
PSNR ≥45 dB / maximum eight levels: minimum original/compositing/termination/batches
PSNR 63.0098/64.3656/65.2258/65.0901 dB, maximum one level. All seven bounded batch
PPMs are byte-identical to normal batching; hashes match the
[previous fixture identities](2026-10-13-viewer-regression.md).

Parent `2348844`. Dirty implementation inventory `R/implementation-identity.json`
SHA-256 `6a8db08abca11b3512ca1d70de3e6762454641ea1aca0352ba1861ded7d11a7e`
records source/binaries/SPIR-V/scripts and protected-data inventory. **329 original
source/fit/job/bundle files remain byte-identical** (`protected-before/after.json`).
Video SHA-256 `51263d9991dcbc2136d0809ecba1a8ba1b261896f3b222b7afba19784a153ef6`;
fit identity `0975ef8365bc402e3533a97c97087a425eb71b8cc0644691c8137f413dc235cb`;
PLY SHA-256 `8067bacb83ae590974a3425f2625e9f6d8ddfc3a5e5621bf28ef2787d45ea81c`;
scene content `35ed89b5abb2ac9eb98b110f1bcd4dab53d1aed70c133623e764ffeffcba01d0`.
Specification/runbook/AGENTS unchanged; original successful solver/fit not rerun.

## Commands and failures

All final commands exit 0; GPU commands use native access. Output directories must
be new. `<case>` is original (no mode), compositing, termination, batches or
batches-bounded (corresponding `--<case>` mode).

| Commands | Result / logs in R |
|---|---|
| `cmake --build --preset dev -j 4`; `cmake --build build/clang-viewer-cxx23 -j 4` | Native builds, `build-checked-dev/clang.log` |
| `ctest --preset dev`; `ctest --test-dir build/clang-viewer-cxx23 --output-on-failure` | **91/91 each**, `ctest-dev-final/clang-final.log`; later refinements affect only standalone diagnostic admission |
| `build/dev/we_gpu_reference $R/real-checked --real artifacts/sample2.wexplore artifacts/sample2-fit-final`; same with Clang binary and `$R/real-clang-checked` | Full count/three cameras/zero errors, `native-real-checked/real-clang-checked.log` |
| `.venv-train/bin/python tests/gpu/compare_reference.py $R/real-checked --real` | Three passing views; `compare-real-checked.log`, per-view float/PNG/error images and `comparison.json` |
| `build/dev/we_gpu_reference $R/synthetic-<case> [mode]`; `.venv-train/bin/python tests/gpu/compare_reference.py $R/synthetic-<case>` | 32 passing views, `native/compare-<case>.log`, `synthetic-summary.json` |
| `build/dev/we_gpu_smoke $R/smoke-dev.ppm`; Clang binary with `$R/smoke-clang.ppm` | Each 51 frames, overlap 130,3,68; update/resize/pause/budget/overflow checks, zero errors; matching `.log` |
| `.venv-train/bin/python tests/gpu/reference_input_tests.py`; `python3 $R/admission-negative.py` | **9/9** CPU cases, `input-tests-checked.log`; four native pre-GPU rejects, `native-rejections.log` (probe needs a fresh root) |
| `clang-tidy -p build/dev tests/gpu/reference.cpp src/render_vulkan/renderer.cpp`; `clang-format --dry-run --Werror` on those files | No first-party diagnostics, `tidy-checked.log`, `format-checked.log` |
| `python3 tools/check_boundaries.py`; `python3 tools/check_docs.py`; `git diff --check` | Boundaries, links/documentation and whitespace pass |

Initial native sandbox SDL initialization exits 2; native access succeeds. CUDA
is unavailable inside the sandbox, not absent on the host. Initial build rejects
implicit JSON/string comparison; corrected with explicit extraction. Earlier tidy
finds integer multiplication widening; corrected without suppressions.

`real-before/` and `real-covariance/` comparison exits 2 retain a single middle-view
pixel (1267,294) with maximum **11** levels; covariance-only correction does not
resolve it. `inspect-pixel.log` identifies a near-opaque contributor with
`next_T` near 1e-4, exposing sensitivity to equivalent coordinate arithmetic.
Final affine/shared-origin input passes the unchanged views and limits. None of
these results establishes all-camera, large-coordinate or cutoff robustness.

Remaining: host count waits/high-resolution sorting profiles, larger parity scope,
reference/platform qualification, full fresh owned reconstruction chain and Q/P/N/E
gates. Current executable work is in [progress](../progress.md).
