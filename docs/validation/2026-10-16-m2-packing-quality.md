# Captured-target packing quality — 2026-10-16 Asia/Hong_Kong

Scope/status: **M2/Q6 IN_PROGRESS**. Both complete retained scenes meet the
unchanged ≤0.2 dB PSNR loss / ≤0.01 LPIPS increase numerically at every available
calibrated camera. These **63/80** cameras are not the prescribed fixed 100-view
corpus. Arbitrary scale, platform/reference qualification and packed lifetime
remain open. Cwd: repository root; parent `0aba7f6`; implementation is the commit
containing this report. Ignored evidence root **R**:
`artifacts/validation/m2-packing-quality-20261016`.

The [calibrated workflow](../protocols/viewer.md#captured-target-packing-quality)
adds diagnostic-only `--fp16-sh` selection and a prepare/evaluate tool. Requests
bind the original fit/scene/payload, every original calibrated camera in order,
both native exports, captured targets, common crop and metric/helper bytes.
Mutation, missing/reordered views, wrong calibration/profile, trimmed payloads,
unsafe paths and validation errors fail admission. Inputs are rechecked before
atomic publication. Production renderer, shaders, canonical storage, worker
environments and reconstruction recipes are unchanged.

Native profile: Linux Wayland, NVIDIA RTX 4060 Laptop / driver **615.78.08**,
GCC **16.2.1 C++26 Debug**. Both profiles retain FP32 geometry and the full
**295,952 / 648,332 splats**, using identical camera/clipping/background settings.
Images are respectively **1280×689 / 718×1280**. Targets are the original
undistorted captured RGB8 PNGs, resized with the recorded fitter bicubic rule.
Crop margin is predetermined **0**, no masks/exclusions or exposure/color fitting.
Pinned offline CPU metrics use float64 PSNR/Gaussian SSIM and float32 AlexNet
LPIPS with `normalize=True`; all package/implementation/weight bytes are checked.

| Scene / all available views | FP32 mean PSNR | Packed mean PSNR | PSNR loss | LPIPS increase |
|---|---:|---:|---:|---:|
| sample_1 / 63 | 29.038993631 | 29.038981992 | **0.000011640 dB** | **0.000000739854** |
| sample_3 / 80 | 33.075038914 | 33.075033635 | **0.000005279 dB** | **0.000000189478** |

Scores aggregate per image then per scene. All **143** individual comparisons
meet both numeric limits; worst individual PSNR loss is **0.000903123 dB** and
LPIPS increase **0.000047908**. Training/guard/holdout counts remain **48/2/13**
and **62/2/16**. FP32 holdout means are **17.544666 dB / 0.787576 SSIM /
0.381047 LPIPS**, and **20.904983 dB / 0.816441 SSIM / 0.251514 LPIPS**:
Q3 still fails locally. These native-background RGB8 scores differ from earlier
saved gsplat renders; no historical measurement or fit is replaced. All-camera
means include training observations and cannot certify independent view quality.

Actual commands/results (R expanded to the path above):

| Commands | Result / retained evidence |
|---|---|
| `cmake --build --preset dev -j 4`; `ctest --preset dev --output-on-failure` | **0; 273/273**, 77.28 s; `build-dev*.log`, `ctest-dev.log` |
| `cmake --build --preset compat-cxx23 -j 4`; `ctest --preset compat-cxx23 --output-on-failure` | **0; 255/255**, explicit C++23 CPU; `build-cxx23.log`, `ctest-cxx23.log` |
| `cmake --build build/m2-vma-clang -j 4 --target we_gpu_reference` | **0**, Clang 23.1.1 C++23 native target built, not run; `build-clang.log` |
| `python3 R/run_native_final.py`; `python3 R/audit_native.py` | **0**, four native commands/exits with all original indices, zero Vulkan errors; **286 final readbacks and fixtures identical** to scored exports |
| `python3 tools/evaluate_packing_quality.py prepare …`; `.venv-metrics/bin/python tools/evaluate_packing_quality.py evaluate …` | Both scenes **0**, all 63/80 views scored; exact argv in `quality-commands.json`, inputs in `sample_N-request.json`, outputs in `sample_N-quality.json/log` |
| `python3`, `.venv-worker/bin/python`, `.venv-train/bin/python` with `-m unittest discover -s tests/integration -p 'worker*_test.py' -v` | **149/149 each**, Python **3.14.8 / 3.12.15 / 3.10.22**; `workers314-final.log`, `workers312.log`, `workers310.log` |
| `.venv-train/bin/python tests/gpu/reference_input_tests.py` | **32/32**, `reference-input-tests-train.log` |
| `.venv-metrics/bin/python R/check_numeric_final.py` | **0**, seven synthetic cameras: exact MSE and **6.020599913 dB** loss, common crop and expected numeric rejection; `numeric-final/` |

Ten new synthetic admission/delta cases include infinite-PSNR handling without
invented numeric loss. Three CLI rejection probes exit expected **2** before GPU
access. Format/tidy **23.1.1**, boundaries, docs and diff checks pass. Initial
tidy flags a tainted argv index; checked span access repairs it. Final bound
captures reproduce every earlier scored image. The sandbox probe cannot open
SDL; native execution succeeds. System Python lacks the CUDA reference test's
NumPy dependency; its existing training environment passes. The initial numeric
harness passes a relative weight path and is rejected by checkpoint containment;
canonicalizing that harness argument repairs it without changing metric guards.
Failing logs remain. Concurrent CPU checks/metrics/desktop work make durations
command observations, not performance evidence.

Final launch/exit SHA-256:
`e66c7bb57fdeccb5ad1eca7a20d1489bae9d95e7b694f0bc16110666feec23f5` /
`b93f54741a2776db27c63716f90e49df25504fa8b38cf26c0b835ef51d5afe7c`.
They bind unchanged native source/binary/shader bytes through exit. Quality receipt
SHA-256s: sample_1
`6ae485ec02f9f3356015115598d19e5312480a083470669b9ea8d00ae20f67be`;
sample_3 `8eca08110375aafe1f216acb1aa67a667d6e86d050ae4a9570b6d23395fb9fd9`.
Requests bind source/fit/scene/target identities and evaluator/helper hashes.

Preservation checks retain **1,052** unchanged historical protected files outside
the user-replaced sample_2 and a fresh **1,382-file** inventory covering all three
current runs, sources, instructions and existing user reports. No solve, fit,
optimizer checkpoint load, provisioning, download or upload ran. Existing user
documentation edits remain separate. The current dependency byte check still
exits **2** for previously rebuilt executables (`dependency-current.log`);
fresh loader/native-viewer receipts are next. Full Q/P/N/E certification remains
open; neither available counts nor thresholds were altered to claim Q6.
