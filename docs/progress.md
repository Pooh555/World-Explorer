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

[Read-only holdout evaluation](validation/2026-10-14-holdout-evaluation.md) now
scores **13/13** retained sample_1 PNG pairs: mean **17.5284 dB / 0.787073 SSIM /
0.382562 LPIPS**, below Q3's numeric goals. These clamped/rounded RGB8 renders are
separate from the unchanged fitter FP32 scores; no independent inspection path or
observed mask is available. All frames, fit/source/metric hashes and explicit
crop/resize are checked before scoring and publication. **13 image checks / 67
worker checks** pass on three interpreters; **121/121** dev CTest, independent
CPU parity and synthetic numerical checks pass. **2,393 protected files** remain
unchanged; no fit/recipe/checkpoint/environment changes or reconstruction rerun.

[Scoped CPU contract coverage](validation/2026-10-14-cpu-coverage.md): isolated
Clang C++26 instrumentation measures **86.62% line / 70.08% branch** of the
non-GPU core and **71.71% package/update branch** coverage. The unchanged 90%
package/update target is unmet; strict checking correctly exits 1. Unknown scale
and operation enums now fail admission without changing snapshots/retry receipts.
**117/117 coverage CPU / 135/135 dev CTest**, five report-integrity checks,
13 focused native ASan/UBSan/LeakSanitizer cases and format/tidy/docs pass.
**2,393 protected files unchanged**; no reconstruction/environment/GPU rerun.
The reviewed scope includes compiled platform code and all seven CPU libraries;
this does not qualify native UI, inactive platform branches or full E1/M1.

[Worker envelope/completion validation](validation/2026-10-14-worker-envelope.md)
adds 16 synthetic cases and repairs early-return Catch section discovery (one →
14 actual assertions). Native manifest admission checks exact error and completion
observer suppression. Worker branches reach **97.20%**, core **87.04% line /
71.61% branch**, whole package/update **74.25% branch**; its 90% goal remains
unmet. **133/133 coverage CPU / 151/151 dev CTest**, 17 focused native
ASan/UBSan/LeakSanitizer cases, format/tidy/docs pass; **2,393 protected files
unchanged**. Production/worker/schema/recipe/environment/scope are unchanged.
No user reconstruction/GPU rerun or full gate certification.

[Durable-map admission](validation/2026-10-14-map-admission.md) adds ten synthetic
cases and rejects revision-zero checkpoints retaining an initial canonical
dependency. Rehashed manifest/receipt/record failures, independent record budgets,
occupied roots and asset obstructions preserve caller files, committed snapshots
and retry receipts. **143/143 coverage CPU / 161/161 dev CTest**, ten focused native
ASan/UBSan/LeakSanitizer cases, format/tidy/docs pass. Core reaches **87.54% line /
73.18% branch**, package/update **76.83% branch**; its 90% target remains unmet.
**2,393 protected files unchanged**; no reconstruction/environment/GPU rerun.

[Canonical producer admission](validation/2026-10-14-canonical-admission.md) adds
13 synthetic cases with **1,271 assertions**: malformed native/file sources,
independent record budgets, exact byte admission, pre-cancelled imports and
acknowledged-retry preservation. Complete unreferenced dependencies remain
recoverable after map admission failure. **156/156 coverage CPU / 174/174 dev
CTest**, 13 focused native ASan/UBSan/LeakSanitizer cases and format/tidy/docs pass.
Core reaches **87.93% line / 74.35% branch**, package/update **78.77% branch**;
its 90% target remains unmet. Production/schema/environment/scope are unchanged;
**2,393 protected files unchanged**; no reconstruction/GPU rerun.

[Windows map observer repair](validation/2026-10-14-windows-map-observer.md):
supplied Windows CPU CTest **148/155**, seven failures in the preservation helper
opening the held `writer.lock`. The observer now inventories its exact regular
entry while hashing every other data file; unrelated lock filenames stay checked.
Production locking/admission and coverage scope are unchanged. Local **175/175
C++26 dev / 157/157 explicit C++23 CPU checks**, 14 focused cases / 1,612 assertions,
format/tidy/docs and **2,393 unchanged protected files** pass. Native Windows CI
rerun is pending for this commit; no reconstruction or environment change.

[Durable transaction admission](validation/2026-10-14-map-transactions.md): **12
synthetic cases / 4,168 assertions** verify rejected records/budgets/revisions,
same-writer correction/retry, reopened receipts, scoped correction invalidation
and failed reset preservation. Map-store branches rise **77.88%→95.19%**; core
**88.15% line / 75.09% branch**, package/update **79.97% branch** (90% unmet).
**169/169 coverage CPU / 187/187 dev / 169/169 explicit C++23 CPU CTest**, 12 native
sanitizer cases and format/tidy/docs pass. **2,393 protected files unchanged**;
production/schema/recipe/environment/scope unchanged, no reconstruction/GPU rerun.

[Completed-pose replay admission](validation/2026-10-14-replay-admission.md): **12
synthetic cases / 8,867 assertions**, replay **178/178 mapped branch outcomes**,
core **88.39% line / 76.42% branch**, package/update **82.13% branch** (90% unmet).
The initial dev run passes 198/199; its cancellation observer can unregister
before invocation. Retaining the callback through cancellation/join repairs that
test without changing production; a latch probe reproduces the race and all seven
boundaries pass 20 consecutive runs. **181/181 coverage CPU / 199/199 dev /
181/181 explicit C++23 CPU CTest**, 12 native replay sanitizer cases, format/tidy/
docs and **2,393 unchanged protected files** pass. No production/schema/recipe/
environment/scope change or reconstruction/GPU rerun.

[Completed-pose file admission](validation/2026-10-14-pose-import-admission.md):
**14 synthetic cases / 36,151 assertions** check rehashed invalid metadata,
caller-file/retained-dataset preservation, explicit repair, PINHOLE import and
unchanged registration limits. Importer branches rise **62.38%→93.33%**; core
**88.72% line / 77.80% branch**, package/update **84.43% branch** (90% unmet).
**195/195 coverage CPU / 213/213 dev / 195/195 explicit C++23 CPU CTest**, 14 native
sanitizer cases and format/tidy/docs pass. **2,393 protected files unchanged**;
production/schema/recipe/environment/scope unchanged, no reconstruction/GPU rerun.

[Decoded-frame source admission](validation/2026-10-14-decoded-frame-admission.md):
**15 synthetic cases / 24,056 assertions** check rehashed stages, exact limits,
caller-file preservation, cancellation/read/seek retry and retained RGB leases.
Decoded-source branches rise **65.13%→95.80%**; core **89.11% line / 79.27% branch**,
package/update **86.87% branch** (90% unmet). **210/210 coverage CPU / 228/228 dev /
210/210 explicit C++23 CPU CTest**, 15 native sanitizer cases and format/tidy/docs
pass. **2,393 protected files unchanged**; no production/schema/recipe/environment/
scope changes or reconstruction/GPU rerun. Legacy source-prefix and timestamp
limits remain explicit; synthetic header diagnostics do not certify decoding.

[Fresh batch-pose admission](validation/2026-10-14-batch-pose-admission.md): **16
synthetic cases / 20,633 assertions** verify ordered admission, exact memory/disk
limits, retry/lease/file preservation, owned resume and isolated worker events.
Three path regressions reproduce acceptance of dangling RGB output links and a
linked input directory; non-following entry checks, canonical directory checks,
exclusive staging and owned rollback repair them. Batch branches **92.75%**;
core **89.57% line / 80.66% branch**, package/update **89.13% branch** (90% unmet).
**226/226 coverage CPU / 244/244 dev / 226/226 explicit C++23 CPU CTest**, 16 native
sanitizer cases and format/tidy/docs pass. **2,393 protected files unchanged**;
only the batch adapter's production source changes. No reconstruction/GPU rerun
or recipe/schema/environment/scope changes; synthetic completion is diagnostic.

[Scene bundle/PLY admission](validation/2026-10-14-bundle-admission.md): **13
synthetic cases / 8,467 assertions** check rehashed packages, exact budgets,
failed publication/repair, caller files, retained snapshots and SH degrees 0–3.
LF/CRLF regressions expose undercounted PLY headers; raw-byte/remaining-budget
reads repair the limit, including carriage-return-heavy lines. Bundle/PLY branches
**94.59%**; core **90.03% line / 81.65% branch**, package/update **90.78% branch**.
The strict CPU target now passes the unchanged goals. **239/239 coverage CPU /
257/257 dev / 239/239 explicit C++23 CPU CTest**, 13 native sanitizer cases and
format/tidy/docs pass. **2,393 protected files unchanged**; only `bundle.cpp`
changes in production. No reconstruction/GPU rerun or recipe/schema/environment/
scope changes. CPU coverage alone does not qualify E1 or complete M1.

[Windows batch launch-marker repair](validation/2026-10-14-windows-batch-marker.md):
supplied `windows-2025` C++23 CPU run passes **236/238**, failing two diagnostic
marker comparisons (ten assertions) because text-mode writes translate LF to
CRLF. A portable CRLF-write shim reproduces every failure; binary marker append
repairs it while retaining exact byte/one-launch checks. **2 cases / 6,077
assertions**, **257/257 dev / 239/239 C++23 CPU CTest**, focused native sanitizers
and format/tidy/docs pass locally. **87 production source hashes and 2,393
protected files unchanged**. Native Windows rerun remains pending; no workflow,
recipe, reconstruction or environment changes. This does not certify Windows
viewer/release behavior or a full gate.

Next executable task: M1/E1 bounded parser fuzzing (§7.3). There are no existing
fuzz targets. Add an opt-in isolated Clang CPU libFuzzer + ASan/UBSan target for
`load_gsplat_ply`, with small synthetic SH0–3/LF/CRLF seeds, bounded input/header/
scene budgets and a dedicated ignored scratch/corpus directory under `build/`
or `artifacts/`. The local Clang 23 runtime resolves to
`/usr/lib/clang/23/lib/linux/libclang_rt.fuzzer-x86_64.a`; it has not been exercised.
Keep this separate from normal builds, coverage and TSan. Run a fixed-duration
native campaign, record actual executions/limits/source and seed identities,
retain crashes, and promote any failure into a deterministic regression before
repairing it. Do not claim fuzz verification from configuration alone or change
coverage/quality thresholds. Never feed private media or saved jobs to the target.
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
tracks scope. Earlier [supplied native CPU logs and metric observer repair](validation/2026-10-14-native-metric-paths.md)
show Windows **101/102** and macOS **102/103**: both fail the metric mutation
observer's unresolved/canonical path comparison. Portable alias replay reproduces
the exact failure; canonical observers and exactly-once mutation checks now pass:
**18 metric / 68 worker cases** on three interpreters, **103/103 C++23 CPU /
121/121 C++26 dev**, **2,393 protected files unchanged**. The latest supplied Windows worker integration
passes; macOS hosted confirmation remains pending. No reconstruction or environment
changes. The [earlier audit observer repair](validation/2026-10-13-native-audit-paths.md)
retains its historical 12 audit / 37 worker / 119 dev scope. Named SQLite recovery
cases pass in the latest supplied logs. The earlier [SQLite closure repair](validation/2026-10-13-windows-sqlite.md)
retains local 102/102 / 25-case scope; prior Windows 100/101 cleanup failure,
macOS CPU pass and Windows 77/78 (worker only) remain historical;
[flush repair](validation/2026-10-12-windows-flush.md) records local C++23 79/79 and
Python 3.12/3.14 9/9. Native viewer/release qualification is separate. Sandbox ICD
failure is not GPU absence. Local commits are authorized; pushes/uploads are not.
Follow the [documentation protocol](protocols/documentation.md).
