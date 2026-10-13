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

Historical original/fresh scene and checkpoint results retain their recorded scope
in Git documentation. On 2026-10-13 the user explicitly requested **all old
artifacts be deleted** and all three videos reconstructed anew. Those prior raw
jobs, scenes, readbacks and migration inventories are no longer available.

## Current fresh reconstruction runs

Sources: `data/local/sample_1.mp4`, `sample_2.mp4`, `sample_3.mp4`.
The renamed first source is explicitly being reconstructed rather than reused.
Each video now has one folder: `artifacts/sample_1/`, `sample_2/`, `sample_3/`.
`request.json` and `source.json` identify inputs/config; stage directories hold
owned processing state; `logs/reconstruction.log` records progress and
`logs/result.json` appears on terminal exit. The original session interruption cancelled all three coordinators before fitting.
The earlier sequential queue stopped after falsely rejecting sample_1 and
cancelling sample_2; sample_3 remained pending. [Model selection investigation](validation/2026-10-13-model-selection.md)
found a complete 63-frame model alongside a redundant four-frame subset.
The worker now selects a unique strict superset, preserving genuine disconnected
and ambiguous-model rejection. Retained-model recovery completed in a separate
`pose-recovery/` job, and native replay passed: 63 observations/poses, one calibration.
The new sequential recovery queue uses `request-recovery.json` per video:
sample_1 completed its 7k fit: **295,952 splats**, validated/native-viewed with zero
Vulkan errors. All 204 original pose files and 18 completed fit files remain
unchanged. Its viewing metadata now preserves capture orientation on an explicit
Source view reset; canonical world/poses/SH and unknown gravity are unchanged.

Sample_2's later stop was cancellation (exit 130); the original trigger is not
established. Remaining jobs now run sequentially in an independent, resource-
bounded user service. A custom-folder stale-lock bug was corrected. Measured CPU
matching memory pressure required single-pair matching, retaining all features
and frames. Current requests are `request-bounded.json`, with two extraction/map
threads and new `pose-bounded/` outputs. Both bounded solves have now exited 2
with POSE_DISCONNECTED: sample_2 has 52/64 frames across separate models;
sample_3 failed the same single-model check. No scene was published for either.
Sample_2 seed/retained-model recovery did not join its models. The match graph
has only 57 connected images, below the 58 required for 90% of 64. Original
database/models are retained; no fit was launched and both services are inactive.
Recapture with overlapping textured views or a separately qualified pose
approach is needed; unchanged retries cannot pass from these matches.
See [pose continuation evidence](validation/2026-10-13-pose-continuation.md).
See [service/orientation evidence](validation/2026-10-13-service-orientation.md).
See [fresh reset evidence](validation/2026-10-13-fresh-video-reset.md).

```sh
# Inspect the existing runs before starting any duplicate:
tail -n 10 artifacts/sample_1/logs/reconstruction.log
tail -n 10 artifacts/sample_2/logs/reconstruction.log
tail -n 10 artifacts/sample_3/logs/reconstruction.log
```

Future videos: `python3 tools/new_video_job.py data/local/VIDEO.mp4` creates
`artifacts/VIDEO/request.json` without starting processing. Existing folders are
refused; `--name VIDEO_retry_2` selects a separate attempt. The full SHA-256 is
stored in `source.json`, keeping folder names readable. All source data and
artifacts remain ignored; [layout protocol](protocols/artifacts.md).

## Next executable work and blockers

Next executable task: resume checked retained metadata/current-storage Import
review for M1. For these videos, use the working sample_1 view below; preserve
sample_2/3's failed stages until replacement footage or a qualified pose recovery
is available. Do not restart identical failed solves.
Do not launch duplicates or rerun sample_1's completed fit. Current sample_1 view:
`build/dev/we_viewer --scene artifacts/sample_1/scene.wexplore --source-view --max-splats 1000000`.
Source view is a viewing reference, not measured gravity or floor/collision.

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
