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

[Renderer tile plans](validation/2026-10-13-tile-plans.md) retain full-scene 4K
under 512 MiB, reference parity and bounded recount/cache failure behavior.
[Local timing](validation/2026-10-13-stage-timing.md) remains noisy development
evidence. [Original real comparisons](validation/2026-10-13-real-reference.md)
retain calibrated/shared-origin scope; all-camera parity and Q6 remain pending.

[Fresh owned chain](validation/2026-10-13-fresh-chain.md): inspection/decode →
**63 RGB leases/connected poses → 7k fit → 297,489 splats → native viewer**.
Step-1,105 checkpoint cancellation/resume, protected overlap guards, completed
reuse and byte-identical publication pass. Active commands total 2,896.992 s,
not P1 certification; temporal holdout quality remains unqualified.
[Typed processing status](validation/2026-10-13-progress-status.md) keeps actual
backend timing/steps separate from unknown ETA and terminal/handoff state.

[Owned-thread Cancel/reopen](validation/2026-10-13-viewer-cancel.md) checks seven
boundaries, pending-admission repair and exact prior-pixel preservation.
Latest [Import request review](validation/2026-10-13-request-review.md): bounded
background summary of actual actions/limits, checksum-pinned launch, stale review
invalidation and no preview worker execution. **114/114 GCC C++26 / Clang C++23
checks**; native 1428×868/1024×640 dialogs and reviewed 297,489-splat handoff pass
with zero Vulkan errors. **670 protected files** are unchanged; no solver/optimizer
rerun. Synthetic tests/native diagnostics do not certify full gates.

The original **304,867-splat** result, its step-4,323 cancellation/resume, all
successful jobs and decode pointer remain preserved. RTX 4060 Laptop/driver
615.78.08 is **not H_R/H_V**. Both full results exceed M1's prescribed ≤250k fixture;
no splats are trimmed. Scale is arbitrary; gravity unknown; geometry/walk
unavailable. Independent solutions cannot reuse each other's fits.

## Usable commands and protected data

Run from repository root; all media/jobs/screenshots stay local and ignored.

```sh
build/dev/we_viewer --scene artifacts/sample2-fresh-chain-20261013/scene.wexplore --source-view --max-splats 1000000
build/dev/we_process reconstruct --request artifacts/sample2-fresh-chain-20261013/adopt.json
```

The reconstruction command adopts checked retained metadata and completed
poses/fit without optimization; the fresh adopted result is independent of the
original retained scene. Worker source changed in the flush repair: older
owned automatic requests/probes require compatible worker identities or separate
fresh jobs; do not rewrite owners/cache/checkpoints. Additional replay/export/map/
preparation/adoption commands are indexed in [validation](validation/README.md).

Private source: `data/local/sample.mp4`, 20.948333 s, 3840×2160 H.264/30 FPS;
SHA-256 `51263d9991dcbc2136d0809ecba1a8ba1b261896f3b222b7afba19784a153ef6`.
`artifacts/sample2-job/decode.json` still selects the old capture's 92-frame stage.
Do not rewrite it or rerun successful original COLMAP/gsplat. For retained replay,
explicitly select `artifacts/sample2-job/stages/decode-36c925841001fd1027759ac7dcab6451304305b9987c5e1da3e8ab1f5eaa9c6b`.

## User-requested video runs

[New local runs](validation/2026-10-13-new-videos.md): renamed `sample_1.mp4`
is identical to the original; completed outputs were preserved. `sample_2.avi`
failed with three disconnected COLMAP models (exit 2); its failed solve is retained.
`sample_3.mp4` is running in `artifacts/new-videos-20261013-sample_3`: 92 decoded
frames, native RGB journal complete, poses started. Check `run.log` and
`run-result.json` before retrying; do not start a duplicate or rerun completed fits.
On success validate/open `scene.wexplore` with `--source-view --max-splats 2000000`.
No new reconstruction gate or viewer pass is claimed.

## Next executable work and blockers

Next executable task: extend Import review with optional checked retained video
metadata/current-storage results. When `inspection.action = adopt_completed`, use
`preflight_video()` on the background thread to show actual duration/dimensions,
source identity and storage checks; keep configuration-only and checked-metadata
states explicit. Automatic inspection requests still require running inspection;
do not start workers or decode during review. Cover cancelled/mismatched metadata
and inadequate storage while keeping appearance active and launch revalidation.
Exercise `artifacts/sample2-fresh-chain-20261013/adopt.json` with fresh ignored
settings/readbacks. Preserve successful jobs/owners/checkpoints and stale decode.

Reference hardware, prescribed fixture/platform qualification and complete gates
remain pending. Do not weaken thresholds or silently trim either full scene.
Timestamp sampling and blur/cut/lens/coverage diagnostics retain their recorded
limits. `--source-view` remains an independent spectator reset. Unknown/inadequate
storage fails; metadata inspection still applies the decoded-storage estimate.

Other native devices/platforms, corpus/reference performance, geometry/collision,
coverage/fuzzing and full gates remain pending; [requirements](requirements.md)
tracks scope. Latest Windows CI stops before CMake on Chocolatey FFmpeg HTTP 504;
[pinned 7.1.1 archive/retry/cache repair](validation/2026-10-13-windows-ffmpeg.md)
passes local workflow/archive/PowerShell checks, hosted rerun pending. Earlier
macOS CPU pass and Windows 77/78 (worker only) remain historical;
[flush repair](validation/2026-10-12-windows-flush.md) records local C++23 79/79 and
Python 3.12/3.14 9/9. Native viewer/release qualification is separate. Sandbox ICD
failure is not GPU absence. Local commits are authorized; pushes/uploads are not.
Follow the [documentation protocol](protocols/documentation.md).
