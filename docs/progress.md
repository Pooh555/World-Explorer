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

Documentation [consolidation](validation/2026-10-12-documentation.md) changes no
software qualification. Latest code slice: `f115d90`. **79/79 CPU/worker tests in six local profiles**;
format, boundaries and targeted tidy pass. Synthetic complete fresh-chain
publication/restart passes. Real automatic decode selects **63/628 frames**, checks
inventory, reuses its cache across processes and cancels before pose invocation
(CLI 130). No RGB journal, pose backend request, map or scene is published by that
probe; 18 prior manifests/video hashes remain unchanged.
[Exact scope](validation/2026-10-12-stage-chain.md).

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
build/dev/we_process reconstruct --request artifacts/sample2-inspection-pipeline-20261012/inspect-and-adopt.json
python3 artifacts/validation/stage-chain-real/verify.py
```

The reconstruction command adopts original completed poses/fit after automatic
metadata inspection; it does not repeat optimization. The probe intentionally
cancels before solving. Additional replay/export/map/preparation/adoption commands
are indexed in [validation](validation/README.md).

Private source: `data/local/sample.mp4`, 20.948333 s, 3840×2160 H.264/30 FPS;
SHA-256 `51263d9991dcbc2136d0809ecba1a8ba1b261896f3b222b7afba19784a153ef6`.
`artifacts/sample2-job/decode.json` still selects the old capture's 92-frame stage.
Do not rewrite it or rerun successful original COLMAP/gsplat. For retained replay,
explicitly select `artifacts/sample2-job/stages/decode-36c925841001fd1027759ac7dcab6451304305b9987c5e1da3e8ab1f5eaa9c6b`.

## Next executable work and blockers

Validate fresh chaining's final real pose/fit boundaries using checked retained
decode and native recovery outputs: exercise admission, journal/pose/map/fit
dependency rejection and scene publication while preserving distinct solutions.
The owned fresh path is available for an explicitly selected fresh capture/job;
**full real fresh solve/fit chaining remains unverified**.

Finish the prescribed **≤250k fixture and reference renderer parity** on available
native hardware. Label synthetic/reference cases; do not silently trim the
304,867-splat scene or substitute it for the required fixture. Selection remains
timestamp sampling; blur/cut/lens/coverage qualification is pending. Python JSONL
diagnostics remain transitional. Decoded-storage estimation also applies to
metadata inspection; unknown/inadequate space fails explicitly.

Native Windows/macOS control/viewer/release, Apple Silicon/AMD, corpus/reference
performance, geometry/collision, coverage/fuzzing and complete Q/P/N/E gates remain
pending; [requirements](requirements.md) tracks each scope. Latest supplied hosted
CPU results are macOS 78/79 and Windows 74/78.
[Path/runtime repairs](validation/2026-10-12-native-runtime.md) reproduce both causes
locally; GCC C++23 and Clang/libc++ C++23 pass 79/79. Native rerun after the user’s
next push remains pending; native viewer/release scope is separate. Sandbox ICD
failure is not GPU absence. Local commits are
authorized; remote pushes/uploads are not. Follow the
[documentation protocol](protocols/documentation.md) for subsequent handoffs.
