# Current handoff

Updated 2026-10-13 (Asia/Hong_Kong). **M1: IN_PROGRESS. No M1–M6 milestone or
Q/P/N/E requirement is fully certified. M7–M8: DEFERRED_BY_USER.**

## Implemented and checked

Native SDL3/ImGui viewer/CLI, Vulkan anisotropic projection/radix/tile rendering,
fly navigation, bounded asynchronous updates, transactional scene/map snapshots,
checked bundles, isolated FFmpeg/COLMAP/gsplat workers. GUI/CLI share commands.
Current contracts: [capture](protocols/capture.md), [poses](protocols/poses.md),
[maps](protocols/maps.md), [scenes](protocols/scene.md), [workers](protocols/worker.md),
[jobs](protocols/jobs.md). [Historical evidence](validation/README.md) retains
implementation sequence, failures, identities, timings and profile differences.

Earlier [fit-calibration admission](validation/2026-10-12-fit-calibration.md) checks
undistorted cameras, resize/intrinsics and independent map/journal dependencies
before publication. Four-profile evidence (including sanitizers) preserves **84
protected files** and identical original splat bytes.
Earlier [stage-chain evidence](validation/2026-10-12-stage-chain.md) covers synthetic
complete chaining and real 63/628-frame decode/cache/pre-solver cancellation;
**full real fresh solve/fit chaining remains unverified**.

Earlier [viewer controls/handoff checks](validation/2026-10-12-viewer-controls.md)
cover specified defaults, saved remaps, malformed keys, active overlays and
one-frame-minimum startup. Automatic exit waits for queued snapshot admission.

Earlier [fixed-camera](validation/2026-10-12-render-reference.md) and
[provenance](validation/2026-10-12-synthetic-labels.md) checks establish calibration,
roll, covariance margins and imported synthetic labels, including hidden panels.

Earlier [viewer regression repair](validation/2026-10-13-viewer-regression.md)
replaces per-tile global scans with budgeted GPU count/scan/emission/stable tile
sorting and ranges. Scratch grows by resident count; direct FP32-to-SDR presentation
removes duplicate swapchain attachments. Full 304,867-splat 4K screenshot,
native fullscreen and restoration fit the unchanged 512 MiB budget with zero
Vulkan errors. Short 720p throughput improves from ~6 to ~60 completed render
frames/s; fullscreen/4K remain slower. **91/91 GCC C++26 and Clang C++23 CPU/worker
checks**, native lifetime/budget/overflow checks, and **32 fixed reference views**
pass (maximum one color level). Forced small-budget partitioning is byte-identical.
Host count waits remain; no P2/P3, Q6 or reconstruction gate is certified.

Latest [full retained calibrated comparison](validation/2026-10-13-real-reference.md)
checks all **304,867 splats** at original fit resolution **1280×688**, using first,
middle and last saved fit cameras. Explicit bounded real input, fit/PLY identities,
exact affine inversion and shared floating-origin representation preserve the
original assets. Native projection now follows pinned affine/quaternion/covariance
operations. All three real and 32 unchanged synthetic views pass, maximum one
color level; GCC/Clang real captures match byte-for-byte. **91/91 CPU/worker checks
per profile**, nine reference-admission checks, four native rejection probes and
native lifecycle/budget smoke pass. **329 protected files are unchanged.** Initial
single-pixel cutoff failure remains recorded; absolute-world float32 arithmetic
and all-camera/Q6 parity are unverified.

Retained real result: replacement capture → **63/63 connected poses → 7,000-step
fit → 304,867 splats → checked native viewer**, with cancellation at step 4,323
and compatible optimizer/RNG resume. Native GPU smoke covers overlap,
replace/delete/resize, shutdown and render pause/resume with zero Vulkan/sync
errors. RTX 4060 Laptop/driver 615.78.08 is **not H_R/H_V**. Scale is arbitrary;
gravity unknown; geometry/walk unavailable. Independent recovery poses remain a
separate solution; their map cannot reuse the original fit.

## Usable commands and protected data

Run from repository root; all media/jobs/screenshots stay local and ignored.

```sh
build/dev/we_viewer --scene artifacts/sample2.wexplore --source-view --max-splats 1000000
build/dev/we_process reconstruct --request artifacts/sample2-fit-boundary-20261012/original.json
```

The reconstruction command adopts checked retained metadata and original completed
poses/fit without optimization. Worker source changed in the flush repair: older
owned automatic requests/probes require compatible worker identities or separate
fresh jobs; do not rewrite owners/cache/checkpoints. Additional replay/export/map/
preparation/adoption commands are indexed in [validation](validation/README.md).

Private source: `data/local/sample.mp4`, 20.948333 s, 3840×2160 H.264/30 FPS;
SHA-256 `51263d9991dcbc2136d0809ecba1a8ba1b261896f3b222b7afba19784a153ef6`.
`artifacts/sample2-job/decode.json` still selects the old capture's 92-frame stage.
Do not rewrite it or rerun successful original COLMAP/gsplat. For retained replay,
explicitly select `artifacts/sample2-job/stages/decode-36c925841001fd1027759ac7dcab6451304305b9987c5e1da3e8ab1f5eaa9c6b`.

## Next executable work and blockers

Next executable task: instrument CPU count-timeline waits and supported GPU
timestamp measurements for projection/count, reference sorting and compositing.
Then run `build/dev/we_gpu_viewer_regression artifacts/sample2.wexplore artifacts/validation/count-waits-20261013/baseline.json` (create the ignored parent
directory first) using the full scene and unchanged budgets. Separate count waits
from GPU work before selecting another optimization. `--source-view` remains an
upright spectator reset; calibrated comparison uses the diagnostic command in
[latest evidence](validation/2026-10-13-real-reference.md). Retain both
`--termination` and `--batches-bounded` cases; for each run
`build/dev/we_gpu_reference artifacts/validation/<new-output> <mode>`
and `.venv-train/bin/python tests/gpu/compare_reference.py artifacts/validation/<new-output>`.

Reference-hardware/native-platform qualification and the real owned fresh RGB/
solver/fit chain remain pending. Do not weaken gates, silently trim the retained
304,867-splat scene, rerun original successful COLMAP/gsplat or rewrite its stale
decode pointer. Timestamp-only selection, blur/cut/lens/coverage diagnostics and
transitional Python JSONL remain as recorded. Unknown/inadequate storage fails;
metadata inspection still applies the decoded-storage estimate.

Native Windows/macOS control/viewer/release, Apple Silicon/AMD, corpus/reference
performance, geometry/collision, coverage/fuzzing and complete Q/P/N/E gates remain
pending; [requirements](requirements.md) tracks each scope. Latest supplied Windows
CI stops before CMake/CTest: Chocolatey's FFmpeg lookup returns HTTP 504.
[Dependency setup repair](validation/2026-10-13-windows-ffmpeg.md) pins/caches the
7.1.1 publisher archive, retries downloads and verifies checksum/tool versions.
Local workflow/archive/PowerShell checks pass; the hosted Windows rerun is pending.
Earlier user reports macOS CPU CI passed; Windows evidence was 77/78, only worker
integration failing. [Stage flush repair](validation/2026-10-12-windows-flush.md)
passes local C++23 79/79 and Python 3.12/3.14 9/9; hosted confirmation remains pending.
Native viewer/release scope is separate. Sandbox ICD failure is not GPU
absence. Local commits are authorized; remote pushes/uploads are not. Follow the
[documentation protocol](protocols/documentation.md) for subsequent handoffs.
