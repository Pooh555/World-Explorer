# M2 development exit review — 2026-10-16 Asia/Hong_Kong

Scope/status: **M2 VERIFIED for the plan §8.2 Linux/NVIDIA development exit**.
All six local items pass the review below. Full Q/P/N/E, reference hardware,
physical input/display/recovery and other native platforms remain open at M4–M6.
Cwd: repository root; reviewed source `4fd46ec`. This slice changes documentation
only. Ignored evidence root **R**: `artifacts/validation/m2-exit-review-20261016`.

| §8.2 item | Reviewed evidence and result |
|---|---|
| 1. Reference parity | [All 80 sample_3 cameras](2026-10-15-all-camera-reference.md), full **648,332** splats, maximum one RGB8 level; [synthetic clipping](2026-10-15-clipping-boundaries.md) and [floating origin](2026-10-15-floating-origin.md) pass. Review found no retained reference comparison for the current **295,952**-splat sample_1. A new native export/reference comparison checks **all 63** calibrated cameras: **63/63 pass**, PSNR minimum **62.975917 dB**, maximum **two RGB8 levels**, zero Vulkan errors. Fixed limits remain ≥45 dB / ≤8 levels. |
| 2. Revisions and budgets | CPU retry/resync/failed-admission contracts and [48 native storage/reuse phases per compiler](2026-10-16-m2-sh-packing.md) cover immutable replace/delete, partial uploads, offsets, rebasing and rejection. [2M SH3 capacity](2026-10-16-m2-scale-fixture.md): all 2M resident at 1080p, 1,561,248 references, four compiler/storage runs, peak tracked FP32/packed **1,090,486,384 / 709,618,112 bytes** under 2 GiB; 512 MiB rejection preserves the prior scene/pixels. VMA buffer accounting includes staging, excludes swapchain/driver allocations; sampled RSS/HWM is not a complete device/process peak. |
| 3. Local P6 replay | [Final 60-second Release replay](2026-10-16-m2-resident-reuse.md): **600** replacement/eviction revisions at **10.002008/s**, enqueue p95 **0.008282 ms**, upload p95/max **0.188736/0.228992 ms**, **zero interactive idle waits/errors**. Independent review recomputes upload p95/max from all 3,000 raw samples. Original 2.090304 ms miss remains recorded. Normal desktop activity; no H_V/presented-FPS certification. |
| 4. Storage/quality | [Packing/rejection parity](2026-10-16-m2-sh-packing.md): default FP32 and optional FP32 geometry/binary16 SH, 100 frozen local views per complete scene, maximum one RGB8 level. [All captured-target views](2026-10-16-m2-packing-quality.md): 63/80 views meet unchanged ≤0.2 dB loss / ≤0.01 LPIPS increase individually; scene means lose **0.000011640/0.000005279 dB**. Overflow preserves prior pixels; full resync recovers. Arbitrary scale remains explicit; these paths do not certify the prescribed corpus/metric Q6. |
| 5. Input and recovery | [Buffered input](2026-10-16-m2-input-latency.md): four compiler/repetition runs each pass **134** same-poll-frame assertions; visible event→state p95 **7.70–8.09 ms**, startup maximum **587.407 ms** retained. [HiDPI checks](2026-10-14-native-input.md), injected [device loss](2026-10-15-m2-device-loss.md), [swapchain recovery](2026-10-15-m2-postcreate-recovery.md), [accepted isolated X11 minimize](2026-10-16-m2-minimize.md) and [acquisition retry repair](2026-10-16-m2-acquire-timeout.md) retain updates and matching restored pixels. Ordinary Wayland minimize was denied; physical loss/scanout remain unverified. |
| 6. Lifetime and tooling | [FP32 resident lifetime](2026-10-16-m2-resident-lifetime.md): **7,200.746 s / 480 updates / 359 resizes / 13 identical captures / zero errors**. [ADR 015](../adr/015-packed-lifetime-development-acceptance.md) accepts the packed prefix **4,019.936 s / 267 updates / seven captures / 66 zero-error samples**, original exit 2 and missing two-hour completion retained. No long rerun. Applicable GCC C++26/Clang C++23 native/static checks and **280/280** dev CPU checks are in the scale report; earlier [TSan](2026-10-14-render-tsan.md), CPU sanitizer/coverage evidence retains its original scope. No new sanitizer pass is inferred. |

## Identity and actual checks

Native comparison: GCC **16.2.1 C++26 Debug**, RTX 4060 Laptop, driver
**615.78.08**, Vulkan validation; existing gsplat **1.5.3+pt24cu124**, torch
**2.4.1+cu124**, CUDA **12.4**, Python **3.10.22**. The script
`python3 R/reference.py` exits **0**, binding exact argv/cwd/source/binary/shader
and input-manifest hashes before/after both children. Native/reference child
exits **0/0**, durations **17.967/42.401 s**; no optimizer/checkpoint load.
All 63 views remain included. `R/sample_1-reference/comparison.json` SHA-256:
`7ce349236dc31551cd2050e3f02d6654bb52f4700c95475b3e2ca5d8f75dc067`.
These are command durations, without performance warmup or repetitions.

`python3 R/audit.py` exits **0**: **2,142** hashed files, **1,509** protected
non-document files unchanged; frozen fixtures, saved runs, failed stages and
sources retained. It rechecks native scale assets/readbacks, 400 fixed-path
images, captured-target request bindings/scores, input reports, minimize images,
acquisition exit receipts and completed FP32 lifetime journal/readbacks.
`R/audit.json` SHA-256:
`8aa51b2cf2ef1e9407381848bebc0f2ae2d5cee7f8360ddc413b9f71383b9c4b`.
Its initial harness incorrectly compared the replay's update list to an integer;
correcting that observer to use the list length passes; initial log is retained.

Historical binaries are not relabeled as current. The lifetime launch's seven
source hashes match `13e94bf`; later changes add packed stride/encoding and
acquisition deferral, covered by their subsequent native regressions. Since
`0aba7f6`, renderer/shader production differences are solely acquisition deferral
and its Debug hook; projection/composition shaders and scene/update libraries are
unchanged. The latest scale/input identities still match current binaries; the
input receipt's CMake difference adds only scale diagnostics/tests. The two
spec/runbook differences from the older preservation inventory are approved
ADR 016 edits. No existing user diff was present at entry.

`python3 tools/check_dependency_manifest.py --manifest
third_party/dependency-audit-input-20261016.json --local-evidence` exits **0**:
84 components/seven profiles, 213 loader files, 208 mapped files, **253 open
source/rights gaps**, no unresolved selected loader edges (`R/runtime.log`).
Documentation/whitespace checks pass before commit. No rebuild is required for
this documentation-only closure; unchanged native executables are used directly.
No solve, fitting, provisioning, download, long stress, upload or push occurred.
M3 may proceed; release qualification is not certified by this review.
