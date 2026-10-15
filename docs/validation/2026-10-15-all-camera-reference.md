# Explicit calibrated camera selection — 2026-10-15 Asia/Hong_Kong

Scope/status: **M1/E1 IN_PROGRESS**. All **80** original registered sample_3
cameras pass full-scene Vulkan/gsplat comparison with maximum **one RGB8 level**
error. This extends the previous [three-camera evidence](2026-10-15-sample3-evidence.md);
it certifies no milestone, quality/performance gate or other platform. Cwd:
repository root; parent **1eba811**; implementation is the commit containing
this report. Raw ignored/local root **R**:
`artifacts/validation/all-camera-reference-20261015/`.

## Change and input identity

The [diagnostic contract](../protocols/viewer.md#calibrated-reference-camera-selection)
adds matching `--fit-indices` options to native export and pinned comparison.
Canonical bounded CSV rejects malformed, duplicate and out-of-range indices;
explicit order controls the admission origin. Native calibration checks precede
SDL initialization. Comparison requires every requested camera, with checked
count/order/names, original fit correspondence and the complete untrimmed payload.
Default first/middle/last output is unchanged. Only diagnostic/test code changed;
no production renderer, shader, recipe, threshold or source instruction changed.

| Bound identity | SHA-256 |
|---|---|
| Source | `4448a1d9eb93f8b7fc787f9ea037d282128438a2512fad8eb8df8544f53906da` |
| Fit configuration identity | `3b7da295d475bf838651ea38d9279a30c32e88314f25d71d4120665913bf0ed6` |
| Uncompressed canonical scene content | `14787aa730211790d4b8310fb61093f2125740bc399497cacc59ff8b842a6cfe` |
| Final native diagnostic binary | `c166e486321928330060e90f13bb67074f76ad0b4b795ae0d9e5bdb1140fb249` |
| Comparator source | `3d704a03e973f169d30136c8da18e4934afa116e9b889b5fab92c359ce1e3222` |
| R/all-80/comparison.json | `ad3643dac80a253c2bb728fea7cd4f88a83de0099a77ce99eb6d10e7b41ec182` |

`R/implementation-identity.json` retains all five diagnostic/test file hashes and
both tested core binaries. The fit remains **7k steps / 648,332 splats**, SH3,
captured display-encoded radiance, arbitrary scale, geometry/walk unavailable.

## Commands and actual results

`R/run_checks.py` SHA
`34cf1b6fae0083e505f30f08da34078abe6010327534a034e1c63a41daf8fb3d`
records exact argv/cwd/exit/elapsed time, selected source hashes and executable
bytes in `LABEL-command.json`, with `LABEL.log`. Native receipts also record
isolated home/cache and selected display variables. GPU runs use native access.
The full 0–79 CSV is retained in `native-all-checked-command.json` and
`compare-all-command.json`; no camera was excluded after observing results.

| Command / receipt label | Actual result |
|---|---|
| `cmake --build --preset dev -j 4`, `build-dev-checked` | **0**, existing GCC **16.2.1 C++26** viewer/CLI/diagnostic/core build |
| `cmake --build --preset compat-cxx23 -j 4`, `build-compat-checked` | **0**, explicit **C++23 CPU**, viewer off |
| `build/dev/we_core_tests 'calibrated diagnostic*'`, `focused-cpp-checked` | **2 cases / 174 assertions**, including all 120 indices and malformed/duplicate/overflow/embedded-NUL boundaries |
| `.venv-train/bin/python tests/gpu/reference_input_tests.py`, `input-tests-checked` | **18/18**, CPU, fabricated admission fixtures; selection/order/count/type/path/camera/origin correspondence |
| `python3 R/reject_selection.py`, `native-rejections` | **Six expected exit-2 CLI rejections**; invalid SDL driver proves selection/mode rejection precedes GPU access; no fixture/native result published |
| `ctest --preset dev`, `ctest-dev-checked` | **262/262**, 90.40 s |
| `ctest --preset compat-cxx23`, `ctest-compat-checked` | **244/244**, 89.58 s |
| `build/dev/we_gpu_reference R/all-80-checked --real artifacts/sample_3/scene.wexplore artifacts/sample_3/fit --fit-indices ...`, `native-all-checked` | **0**, 80 cameras, **81 submitted frames**, full splat count, zero Vulkan errors, 50.71 s |
| `.venv-train/bin/python tests/gpu/compare_reference.py R/all-80 --real --fit-indices ...`, `compare-all` | **0**, **80/80**, 92.19 s; complete per-camera JSON/CSV and float/native/reference/error images retained |
| Default three views, `native-default` / `compare-default` | Both **0**, three passing views, fixture/readbacks byte-identical to retained data |
| Explicit `79,0,40`, `native-reordered` / `compare-reordered` | Both **0**, three passing views with frame 79 as admission origin, maximum one RGB8 level |
| Unchanged synthetic default, `native-synthetic` / `compare-synthetic` | Both **0**, four passing diagnostic views, zero Vulkan errors; synthetic, not reconstruction evidence |
| Format / tidy / docs / boundaries / diff | **0**, `format-checked`, `tidy-checked`, `docs`, `boundaries`, `diff` logs |

Each CTest profile includes **133 worker cases** on Python 3.14.8; detailed
`ctest-{dev,compat-cxx23}-detail.log` retained. Reference profile: Python 3.10.22,
gsplat 1.5.3+pt24cu124, torch 2.4.1+cu124, CUDA 12.4; Linux native RTX 4060 Laptop,
driver **615.78.08**, not H_R/H_V/H_M. Format/tidy **23.1.1**, no final first-party
diagnostics or warning suppressions added. Initial tidy found unchecked optional
calibration access; explicit guards repair it. A Catch macro enum-cast diagnostic
is avoided with equivalent direct `has_value()` assertions; no check is disabled.
Both initial tidy logs remain. No sanitizer/coverage/other-platform rerun claimed.

The initial native 80-view export preceded the optional-guard repair. The final
binary repeated all 80 views into `all-80-checked/`; **every readback, fixture and
binary chunk matches** the compared export (`R/native-equivalence.json`). The
comparator source is unchanged between these exports. No numerical input or
tolerance adjustment was made to obtain agreement.

| All 80 calibrated views, 718×1280 | Observed range |
|---|---|
| PSNR | **62.859553–63.386695 dB**, minimum index 37 / `000038.png` |
| Gaussian SSIM | **0.999530852–0.999643683**, minimum index 31 / `000032.png` |
| Maximum absolute RGB8 error | **1 level** in every view; fixed limit remains **8** |

`R/per-camera.csv` and `all-80/comparison.json` report every selected frame and
native hash. Final sampled renderer-owned allocation **388,398,240 bytes** is
not whole-device peak VRAM. CPU suites and GPU diagnostics overlapped; elapsed
times are command durations, not throughput/FPS or P-gate evidence. Shared-origin
FP32 comparison still does not establish arbitrary-view/cutoff/large-world
robustness, independent-capture Q3 quality or Q6's packed 100-view path. Existing
16-frame holdout scores and Q3 failure remain unchanged.

Before/after checks preserve **2,088 protected files**, **54 preceding evidence
files** and all **94 tracked src/include files**. Both completed scenes and every
failed stage remain intact. No decode, pose solve, optimization/checkpoint load,
installation, download, upload or push occurred. M1's ≤250k consented room fixture,
reference hardware, native Windows/macOS viewer checks and rights qualification
remain open. [Progress](../progress.md) owns the next executable step.
