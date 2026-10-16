# Optional SH storage and fixed-view comparison — 2026-10-16 Asia/Hong_Kong

Scope/status: **M2/Q6 IN_PROGRESS**. Experimental GPU storage and local pairwise
distortion checks pass; **no Q6 quality-loss or millimetre gate is certified**.
Parent `f2ba74f`; implementation is the commit containing this report. Cwd:
repository root. Ignored evidence root **R**:
`artifacts/validation/m2-completion-20261016`. Native Linux Wayland/NVIDIA RTX
4060 Laptop, driver 615.78.08; GCC 16.2.1 Debug C++26 and Clang 23.1.1 C++23.

[ADR 014](../adr/014-gpu-sh-storage.md) keeps the default/canonical profile FP32.
The opt-in GPU profile stores SH as nearest-even binary16 pairs, retaining FP32
geometry, opacity, rotations, projection, SH evaluation and composition. The
record shrinks **256→160 bytes (37.5%)**. Profile/stride and SH conventions are
explicit metadata. Upload sizes, resident offsets and atomic replacements use
the chosen stride. Finite overflow fails with `INVALID_ARGUMENT`; the prior
scene/pixels survive and a valid full resync recovers. No model, scene schema,
source, fit or threshold changes.

`we_gpu_packing` freezes all 100 views before either profile renders. Both full
retained scenes use the documented `source_view_local_loop_v1`, 1280×720,
unchanged source-view FOV, no LOD reduction and 100 distinct image hashes per
profile. This narrow local path has no captured target images. Pinned offline
CPU metrics use display-encoded RGB [0,1], float64 PSNR/SSIM and float32 AlexNet
LPIPS with `normalize=True`; full images, no crop/resize/exposure correction or
excluded views. Scores aggregate per image then per scene.

| Full scene | Splats | Mean pairwise PSNR | Mean SSIM | Mean LPIPS | Max RGB8 difference |
|---|---:|---:|---:|---:|---:|
| sample_1 | 295,952 | 76.290512 dB | 0.999977805 | 0.000002316084 | 1 |
| sample_3 | 648,332 | 77.928769 dB | 0.999987855 | 0.000000865202 | 1 |

Sampled owned allocations after capture/drain are respectively
**246,941,312→218,529,920** and **344,217,568→281,977,696 bytes**; these are
sampled counts, not whole-run allocation peaks or FPS measurements. Native runs
have zero Vulkan errors and retain all splats. CPU metrics/builds and desktop
activity overlap; no warmup, timing percentiles or throughput claim is made.
The final bound runs exit **0** in 98.614/70.842 s. Their source, executable and
shader hashes remain unchanged through exit. An independent audit verifies all
400 final images against the scored bytes, all paths/scene/manifest hashes and
actual exits; the final comparison JSONs also match the scored reports exactly.

Actual commands (all exit **0**; R expanded as above):

```sh
cmake --preset dev
cmake --build --preset dev -j 4
ctest --preset dev --output-on-failure
cmake --build --preset compat-cxx23 -j 4
ctest --preset compat-cxx23 --output-on-failure
cmake --build build/m2-vma-clang -j 4 --target we_gpu_resident_reuse we_gpu_packing
python3 artifacts/validation/m2-completion-20261016/run_packing_receipt.py
python3 artifacts/validation/m2-completion-20261016/audit_packing.py
```

The receipt runner invokes `build/dev/we_gpu_packing` on each saved scene and
new outputs `R/packing-sample_1-bound` / `R/packing-sample_3-bound`.
`packing-source-identity.json` binds all 12 implementation/test files, SHA-256
`16073db8e173c4bc01b836ed70dd867111d7003e000a7f4c967829f1ede5cd2d`.
Launch/exit receipt hashes:
`bd82f4d29f95dbedb06b175531961f0f23e2ea5b35fab11132642e9d26660157` /
`cf730fddf93a2d0bce889af22062c89798433c2716a328486e8a1d4b8e67a105`.

Metric commands use `.venv-metrics/bin/python tools/evaluate_packing.py` with:

```text
--comparison R/packing-sample1/comparison.json
--metric-manifest workers/identity/metric-evaluation-cpu-20261014.json
--requirements workers/metrics/evaluation-environment.txt
--ssim-implementation workers/gsplat/losses.py
--alexnet-weights artifacts/validation/metric-identity-20261014/torch-cache/hub/checkpoints/alexnet-owt-7be5be79.pth
--output R/packing-sample1-metrics-final.json
```

The second command replaces `sample1` with `sample3`. Both exit **0**, score
100/100 images, recheck environment/backbone/implementation bytes and disable
network access. Final metric receipt SHA-256s:
`c8696b9b50781180c8273cef296f388425216d0e20211bf6e6b9ee93a18f7567` /
`5f3814e3541095aff65fefb7944f7f1d4ff951f765219e49cba7fe0c64027754`.
They bind both image hashes per view, source/helpers and the unchanged metric
identity. `packing-audit.json` retains path/comparison hashes and every audit
result. Preliminary observations remain separate from these final receipts.

CPU CTest passes **273/273 C++26 / 255/255 C++23**, 109.50/120.54 s.
Final focused conversion tests pass **3 cases / 63,587 assertions** in both
profiles, including exhaustive finite-half round trips, signed zero/subnormals,
ties and overflow. Six new evaluator admission checks and all **139 worker
checks** pass on Python 3.10/3.14. Native GCC and independent Clang each pass
**48 synthetic phases** covering both storage profiles, copy accounting,
replacement/deletion offsets, transforms, empty scenes, rebasing and failure/
repair. Final timed FP32 smoke passes 51 frames, exact overlap pixel 130,3,68,
resize/fault recovery and zero Vulkan errors; Wayland still denies minimize.

Initial tidy findings are retained in `packing-tidy.log`: unsigned-shift operands,
a swappable test helper, optional access and Catch's combined flag analysis.
Explicit unsigned operands and equivalent checked test expressions resolve them;
format/tidy 23.1.1 pass without disabling warnings. A smoke invocation with the
option in the capture-path position saved three files at root; these were moved
to `R/smoke-misplaced-captures`, and the corrected explicit-path timed run passes.
All **2,088 protected files** match their prior inventory. Documentation,
boundaries and diff checks pass. No provisioning, download or reconstruction ran.

Remaining: Q6 needs ground-truth-bound before/after quality scores on its fixed
100-view path, qualified scale/corpus and other platforms. Packed storage also
lacks a dedicated long lifetime qualification. Fresh runtime byte receipts remain
due after rebuilding; the historical receipt's current-byte check stays stale.
