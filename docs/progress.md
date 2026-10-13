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

Historical [fit admission](validation/2026-10-12-fit-calibration.md),
[stage chaining](validation/2026-10-12-stage-chain.md),
[controls/handoff](validation/2026-10-12-viewer-controls.md),
[camera calibration](validation/2026-10-12-render-reference.md) and
[synthetic provenance](validation/2026-10-12-synthetic-labels.md) retain their
original checks and limits. Full real fresh solve/fit chaining remains unverified.

[Indexed tiles/residency](validation/2026-10-13-viewer-regression.md) fixes full
304,867-splat 4K/readback/fullscreen/restoration under the unchanged 512 MiB budget.
[Calibrated comparison](validation/2026-10-13-real-reference.md) checks three
original fit views at 1280×688 with shared floating-origin inputs. Absolute-world
float32/all-camera parity and Q6 packing remain unverified.

Latest [stage profiling and sort reduction](validation/2026-10-13-stage-timing.md)
adds opt-in bounded GPU timestamps and frame-ID/extent-checked CPU/GPU samples.
Measured tile IDs need four radix passes rather than eight; stable output and all
contributors are preserved. Across three short local repetitions, unprofiled median
completed render throughput rises **36.64→45.80 frames/s at 4K** and
**49.67→64.12 at fullscreen**; 720p stays ~59.4. Tile-index GPU p50 falls
**10.39→6.01 ms at 4K**. These are not presented FPS or P2/P3 certification.
**93/93 GCC C++26 and Clang C++23 CPU/worker checks**, native timing/lifecycle/budget
smoke and 35 real/synthetic comparisons pass; maximum one color level.
All reference captures and twelve quiet-run 4K screenshots are byte-identical;
**329 protected files are unchanged**. Host count waits remain; query driver memory
is outside reported owned-buffer allocations. Native timestamp fallback on an
unsupported device, other platforms and full gates remain unqualified.

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

Next executable task: reuse previously accepted tile rectangles as candidate
count plans for the same extent. Recount every candidate under current reference/
residency limits before emission; split on overflow and preserve the existing
single-tile rejection. The 4K profile still counts eleven regions for six accepted
batches. Compare using `build/dev/we_gpu_viewer_regression artifacts/sample2.wexplore artifacts/validation/tile-plans-20261013/baseline.json --timings`
(create the ignored parent directory first), full scene and unchanged budgets.
Retain `--termination` and `--batches-bounded` reference cases and pinned
`tests/gpu/compare_reference.py` comparisons. Diagnostic commands and exact scope
are in [latest evidence](validation/2026-10-13-stage-timing.md). `--source-view`
remains an upright spectator reset, independent of capture poses.

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
