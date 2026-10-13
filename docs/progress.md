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

[Historical evidence](validation/README.md) retains admission, chaining, controls,
calibration, provenance and toolchain scopes. [Renderer tile plans](validation/2026-10-13-tile-plans.md)
recount bounded candidates and preserve full-scene 4K under 512 MiB: **11→6** counts
for six batches, fullscreen **3→2** for two. Thirty-five reference views and twelve
4K captures remain identical. Sparse fragmentation, host waits and CPU/query
metadata outside owned-buffer metrics remain. [Stage timing](validation/2026-10-13-stage-timing.md)
retains noisy local rates, not FPS certification. [Original real comparison](validation/2026-10-13-real-reference.md)
checks three 1280×688 views with shared floating origin; absolute-world/all-camera
parity and Q6 packing remain unverified.

Latest [fresh owned chain](validation/2026-10-13-fresh-chain.md): automatic
inspection/decode → **63 native RGB leases → 63/63 connected poses → 7k fit →
297,489-splat scene → native viewer**. Cancellation at step 1,105, compatible
resume, solver/optimizer skips on completed reuse and byte-identical publication
pass. A repaired fresh-chain checkpoint guard permits only canonical
`fit/checkpoint.pt` for resume and retains other overlap protections.
**99/99 GCC C++26 / Clang C++23 checks**, three new calibrated comparisons
(maximum one level), native original-to-fresh GUI handoff and all **329 protected
files** pass. Temporal holdout quality remains unqualified (15.388–19.981 dB).
Active initial/resume time is 2,896.992 s on this local profile, not P1 certification.

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

## Next executable work and blockers

Next executable task: propagate checked reconstruction stage/progress events from
`src/application/commands.cpp` / `reconstruction_job.cpp` into
`apps/we_viewer/job_state.cpp` and the processing status. The real run's stages and
fit steps currently reach the diagnostic log while the main status stays generic.
Use bounded typed state and actual backend values; keep ETA unknown unless
measured. Cover cancellation, terminal failure and pending-scene handoff in tests,
then exercise `build/dev/we_viewer --reconstruction-request artifacts/sample2-fresh-chain-20261013/reuse.json --source-view --max-splats 1000000` with fresh ignored
capture/settings paths. Reuse completed stages; do not rerun the solver/optimizer
or rewrite owners/checkpoints. If worker sources change, use the checked
`adopt.json` for retained opening rather than rewriting automatic ownership.

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
