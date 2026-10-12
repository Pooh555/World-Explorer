# Current handoff

Updated 2026-10-12 (Asia/Hong_Kong). **M1: IN_PROGRESS. No M1–M6 milestone or
Q/P/N/E requirement is fully certified. M7–M8: DEFERRED_BY_USER.**

## Implemented and checked

Native SDL3/ImGui viewer/CLI, Vulkan anisotropic projection/radix/quad rendering,
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

[Fixed-camera comparison](validation/2026-10-12-render-reference.md): explicit
calibration/roll and framebuffer admission checks; corrected gsplat covariance
margin. The identical **18-splat synthetic fixture**, four 1280×720 views, reaches
**53.90–54.81 dB PSNR**, maximum 1–4 color levels, zero Vulkan errors. **90/90 dev,
87/87 explicit C++23 and libc++ tests** pass; format/tidy/boundaries pass. These are
local correctness diagnostics, not performance, Q6 or real reconstruction gates.

Latest [synthetic provenance checks](validation/2026-10-12-synthetic-labels.md):
**91/91 dev tests** pass. Imported synthetic scenes keep visible labels with panels
hidden; canonical/legacy fixtures and the retained real scene classify correctly.

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
build/dev/we_viewer --scene artifacts/sample2.wexplore --source-view --max-splats 500000
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

Next executable task: extend the fixed-camera suite to saturation, high-opacity
overlap/early termination and rotating/close-up views; preserve existing fixtures
and tolerances. Then add calibrated comparison of the retained real scene without
trimming its 304,867 splats. `--source-view` remains an upright spectator reset;
it cannot substitute for calibrated parity or drive spectator navigation.

Reference-hardware/native-platform qualification and the real owned fresh RGB/
solver/fit chain remain pending. Do not weaken gates, silently trim the retained
304,867-splat scene, rerun original successful COLMAP/gsplat or rewrite its stale
decode pointer. Timestamp-only selection, blur/cut/lens/coverage diagnostics and
transitional Python JSONL remain as recorded. Unknown/inadequate storage fails;
metadata inspection still applies the decoded-storage estimate.

Native Windows/macOS control/viewer/release, Apple Silicon/AMD, corpus/reference
performance, geometry/collision, coverage/fuzzing and complete Q/P/N/E gates remain
pending; [requirements](requirements.md) tracks each scope. The user reports macOS
CPU CI passed; supplied Windows evidence is 77/78, with only worker integration
failing. [Stage flush repair](validation/2026-10-12-windows-flush.md) passes local
C++23 79/79 and Python 3.12/3.14 9/9. Windows confirmation awaits the next hosted
run. Native viewer/release scope is separate. Sandbox ICD failure is not GPU
absence. Local commits are authorized; remote pushes/uploads are not. Follow the
[documentation protocol](protocols/documentation.md) for subsequent handoffs.
