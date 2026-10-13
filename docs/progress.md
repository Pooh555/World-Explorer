# Current handoff

Updated 2026-10-14 (Asia/Hong_Kong). **M1: IN_PROGRESS. No M1–M6 milestone or
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

Latest [checked Import metadata/storage](validation/2026-10-13-import-metadata.md)
uses shared retained preflight and per-output current-space snapshots; automatic
inspection keeps metadata pending. **119/119 GCC C++26 / Clang C++23 checks**,
native default/compact dialogs and full 295,952-splat Cancel/reopen pass with zero
Vulkan errors; **2,393 protected files** unchanged. No solve/fit rerun or gate change.

The [worker identity audit](validation/2026-10-13-worker-identity.md) now checks
37 training / 3 pose distributions and 76 notice files against installed bytes.
Its [compact receipt](../workers/identity/local-development-20261013.json) retains
exact versions/hashes; training wheel archives, Triton notice and transitive rights
remain pending. Its initial metric absence is addressed by the separate CPU profile
below. No training environment,
recipe or checkpoint changes. Eleven new Python tests pass on 3.10/3.12/3.14;
**119/119 CTest in each existing profile**. The preceding 120-case report counted
the worker suite twice; retained logs and current references are corrected.

[Native input/live HiDPI](validation/2026-10-14-native-input.md): **18/18**
compositor-delivered control assertions and live 2→1.5→1→2 scale checks pass on
the full sample_1 scene with zero Vulkan errors. Bookmark FOV and live Import
resize clipping are repaired; **121/121** GCC C++26 / Clang C++23 checks,
format/tidy/docs and **2,393 unchanged protected files**. Temporary headless
scaling is not a physical-display or other-platform pass. Human/reference latency
and event-to-state timing remain unqualified; no solve/fit rerun.

[Offline metric identity/parity](validation/2026-10-14-metric-identity.md): the
user authorized separate `.venv-metrics` provisioning and official AlexNet weights.
TorchMetrics **1.8.2**, CPU torch **2.4.1**, torchvision **0.19.1**, all 16 packages,
implementation/SSIM/linear-weight/backbone bytes and policy are locked in the
[CPU receipt](../workers/identity/metric-evaluation-cpu-20261014.json). Independent
synthetic LPIPS and SSIM parity pass offline. **16 new checks / 53 worker checks**
pass each on Python 3.10/3.12/3.14; **121/121** dev CTest. Original 37-package
training inventories and **2,393 protected files** are unchanged. Dataset regions,
real corpus and rights/release qualification remain pending; no Q gate pass.

Next executable task: add a read-only image-pair evaluator using the pinned CPU
receipt and explicit dataset crop/mask/resolution. Inspect
`artifacts/sample_1/fit/input_manifest.json`, `result.json` and `references/`;
match the 13 temporal-holdout PNGs to checksum-verified retained undistorted source
images with the recorded bicubic size. Record quantized-render development scope
separately from the fitter's FP32 metrics; keep independent inspection-path Q3
blocked. Write only a new ignored evaluation report directory. Do not alter fits,
recipes, checkpoints or any protected capture, and do not rerun reconstruction.
For these videos, use the working sample_1 view below; preserve
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
tracks scope. Latest [supplied native CPU logs and observer repair](validation/2026-10-13-native-audit-paths.md)
show Windows **101/102** and macOS **102/103**: both fail the same Python audit
read observer. All named SQLite recovery cases now pass on both hosts. Canonical
observer targets plus a portable alias regression pass locally: 12 audit checks
on three interpreters, 37 worker checks, C++23 CPU 103/103 and C++26 dev 119/119.
Hosted reruns of this repair remain pending. The earlier [SQLite closure repair](validation/2026-10-13-windows-sqlite.md)
retains local 102/102 / 25-case scope; prior Windows 100/101 cleanup failure,
macOS CPU pass and Windows 77/78 (worker only) remain historical;
[flush repair](validation/2026-10-12-windows-flush.md) records local C++23 79/79 and
Python 3.12/3.14 9/9. Native viewer/release qualification is separate. Sandbox ICD
failure is not GPU absence. Local commits are authorized; pushes/uploads are not.
Follow the [documentation protocol](protocols/documentation.md).
