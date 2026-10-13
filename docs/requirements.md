# Requirement and evidence matrix

The original targets in `technical_plan.md` §3 and §8 remain controlling; no
thresholds or platform requirements have been waived. Status on 2026-10-13:

| Requirement | Implementation / checks | Status | Evidence |
|---|---|---|---|
| M1 vertical slice | Real COLMAP 63/63 → 7k gsplat → checked scene → native viewer; shared GUI/CLI owned inspection/decode/fresh pose chain or retained adoption, with durable map/scene dependencies, typed processing status, owned Cancel/reopen and reviewed actions/limits | IN_PROGRESS | [Three full-scene original-fit camera comparisons](validation/2026-10-13-real-reference.md) pass locally; [Real owned RGB/solve/fit/publication, step-1,105 checkpoint resume and native background viewer handoff](validation/2026-10-13-fresh-chain.md) pass locally with 297,489 splats; [Typed status](validation/2026-10-13-progress-status.md) and [owned-thread Cancel/reopen with pending-admission repair](validation/2026-10-13-viewer-cancel.md) pass locally; [bounded Import review, checked-byte launch and native full reviewed handoff](validation/2026-10-13-request-review.md) pass locally; prescribed ≤250k fixture, full contracts/reference/platform qualification pending |
| M1 four contracts | Native frame source, checked pose/calibration import, completed replay and fresh batch estimator, map/scene revisions, update sink; verified FlatBuffers 2.0 worker START/CANCEL/events, canonical FlatBuffers pose assets and durable map checkpoints | IN_PROGRESS | [Earlier 84 CPU/worker checks in four profiles](validation/2026-10-12-fit-calibration.md); retained real intrinsic/dimension/journal/map/fit rejection and payload/viewer checks; [63 real RGB/frame identity pairs, connected poses, durable map and completed-stage reuse](validation/2026-10-13-fresh-chain.md) pass locally; native other-platform control and full contract qualification pending |
| M2 renderer / updates | GPU projection/overlap count/scan/emission/stable tile radix/ranges; bounded disjoint batches, calibrated views, exclusive termination and direct FP32-to-SDR presentation; tile-key width radix passes, recounted bounded rectangle plans and optional completed GPU stage timings | IN_PROGRESS | [Indexed viewer regression](validation/2026-10-13-viewer-regression.md): unchanged and forced-partition views pass; full real 4K/fullscreen works; [Three unpacked real camera comparisons](validation/2026-10-13-real-reference.md) pass with shared floating-origin inputs; [bounded timing/sort checks](validation/2026-10-13-stage-timing.md) and [recounted tile plans](validation/2026-10-13-tile-plans.md) pass locally; broader parity, lifetime stress, device-loss and performance qualification pending |
| M3 offline geometry / walk | Worker cancellation/stages started; no supported geometry | NOT_STARTED | Full milestone unverified |
| M4 platforms / Brush | Native source portability intended; hosted Windows CPU build succeeds | BLOCKED | Complete native viewer/device/release qualification on Windows, Apple Silicon and AMD unavailable; CPU CI is narrower |
| M5 larger scenes | Bounded initial admission only | NOT_STARTED | No measured LOD or W3/W4 |
| M6 release candidate | No release certification | NOT_STARTED | Depends on M1–M5 |
| Q1 completion ≥18/20 | One user-authorized sample available | BLOCKED | Sealed consented 20-video corpus absent |
| Q2 30k appearance | Pinned gsplat adapter; no benchmark run | NOT_STARTED | Dataset/metric/model locks and rights pending |
| Q3 held-out video quality | Development temporal holdout measured; no training RGB leakage | BLOCKED | Independent inspection capture and metric/backbone lock absent; no Q3 pass |
| Q4 pose accuracy | COLMAP pose adapter | NOT_STARTED | Fixed GT subset absent |
| Q5 measured geometry | No geometry pipeline yet | NOT_STARTED | Independent geometry GT absent |
| Q6 packing fidelity | Uncompressed canonical storage | NOT_STARTED | 100-view render comparison pending |
| P1 complete offline time | [Fresh owned pipeline](validation/2026-10-13-fresh-chain.md): source decode 172.210 s, pose stage 2,092.566 s, active initial/resume commands 2,896.992 s | IN_PROGRESS | One local development run with intentional cancellation/repair and concurrent CPU checks during resume; no prescribed whole-video H_R certification |
| P2/P3 viewer performance | [Local stage profiling](validation/2026-10-13-stage-timing.md): three short repetitions; 4K completed throughput median 36.64→45.80, fullscreen 49.67→64.12; 720p ~59.4 frames/s; [tile-plan trials](validation/2026-10-13-tile-plans.md) reduce 4K counts 11→6, with variable short throughput | BLOCKED | [Completed-frame stage timing](validation/2026-10-13-stage-timing.md) is not presented FPS or prescribed duration/workload; reference hardware qualification absent |
| P4 bounded memory | Input/admission/retirement budgets; actual resident-count scratch/pixel/reference allocation charges, count-before-emission, bounded geometry-only tile cache and constrained rejection | IN_PROGRESS | [Full-scene 4K screenshot/fullscreen/restoration under 512 MiB](validation/2026-10-13-viewer-regression.md); [tile-plan fragmentation, density and resize checks](validation/2026-10-13-tile-plans.md); [native journal budget, resumed-fit CUDA peaks and final viewer allocation](validation/2026-10-13-fresh-chain.md) observed; aggregate process/device/transient peaks and pathological stress pending |
| P5 coarse-first load | Initial asynchronous load | NOT_STARTED | LOD not implemented |
| P6 asynchronous updates | [Owned Cancel/reopen, old-pixel preservation and pending-admission rejection](validation/2026-10-13-viewer-cancel.md), replay, retry, retirement tests and native replace/delete/density-cache and [real background reconstruction handoff](validation/2026-10-13-fresh-chain.md) checks pass | IN_PROGRESS | Staged upload/latency/stress pending |
| N1 fly controls | Camera + gated SDL input; specified defaults, explicit remaps and active-key overlay | IN_PROGRESS | [Preference/handoff and native overlay checks](validation/2026-10-12-viewer-controls.md) pass; full native input/HiDPI/latency inspection pending |
| N2 walking | Disabled until independent scale/geometry | NOT_STARTED | No invented collision |
| E1 correctness/coverage | Contract/worker/coordinator tests; native sync validation, projection/color comparisons and provenance labels; targeted lint | IN_PROGRESS | [SQLite lifetime regression, local C++23 102/102 and Python 3.12/3.14 25/25](validation/2026-10-13-windows-sqlite.md), with Windows confirmation pending; [114 GCC C++26 / Clang C++23 checks, read-only action/limit review, changed-request rejection and native dialog/handoff](validation/2026-10-13-request-review.md); [107 GCC C++26 / Clang C++23 checks, seven owned-thread stop/reopen boundaries and native old-pixel preservation](validation/2026-10-13-viewer-cancel.md); [105 GCC C++26 / Clang C++23 checks, typed stage/step validation and viewer terminal/handoff tests](validation/2026-10-13-progress-status.md); [99 GCC C++26 / 99 Clang C++23 checks, checkpoint forwarding/protected-overlap regressions and full real chain with three new calibrated comparisons](validation/2026-10-13-fresh-chain.md); [earlier partition/lifetime checks and 35 comparisons](validation/2026-10-13-tile-plans.md); [earlier nine reference-admission checks and four native rejects](validation/2026-10-13-real-reference.md); historical libc++/sanitizer scopes separate; coverage, fuzzing and full gates pending |
| E2 build/delivery | Local GCC/Clang C++26 and explicit C++23 builds; libc++ CPU checks; pinned dev archives; corrected native CPU CI | IN_PROGRESS | User reports macOS passed; supplied Windows 77/78; [stage flush repair](validation/2026-10-12-windows-flush.md) passes local C++23 79/79 and Python 3.12/3.14 9/9; earlier [pinned FFmpeg archive/retry/cache repair](validation/2026-10-13-windows-ffmpeg.md) checked locally; latest supplied Windows builds and passes 100/101 with recovery database cleanup errors; [SQLite closure repair](validation/2026-10-13-windows-sqlite.md) passes local C++23 102/102 and Python 3.12/3.14 25/25; hosted rerun, native viewer/release and distribution-hash qualification pending |
| L1 / M7–M8 | Only future-compatible contracts in Phase 1 | DEFERRED_BY_USER | No live reconstruction implementation |

Each gate requires the measurement scope prescribed by the specification. A local
synthetic smoke test does not satisfy quality, performance, capture completion or
another operating system's native qualification.

The user subsequently requested [deletion of all old artifacts and three fresh
runs](validation/2026-10-13-fresh-video-reset.md). Prior validation documentation
retains historical observations; old private raw data and path mappings are no
longer available. New runs use the [simple per-video layout](protocols/artifacts.md).
No gate status changes from cleanup or starting a run.

[Model selection repair](validation/2026-10-13-model-selection.md) corrects a false
POSE_DISCONNECTED rejection for the original sample: a complete 63-frame model
was accompanied by a redundant four-frame subset. 15 worker/114 CPU checks and
real recovered pose replay pass; fresh sample_1 fitting/publication/native viewing
passed with 295,952 splats. No gate
thresholds were changed and genuinely separate/ambiguous models still fail.

[Service/orientation repair](validation/2026-10-13-service-orientation.md) preserves
all fitted sample_1 assets while correcting explicit source-view presentation up.
116 GCC C++26 / Clang C++23 checks and 18 worker checks pass. Remaining videos
use independent, bounded sequential supervision and single-pair CPU matching;
the bounded solves completed with disconnected-model rejection. [Retained
continuation](validation/2026-10-13-pose-continuation.md) reuses saved matches;
24 worker checks pass. Real recovery could not join sample_2 models; 57 connected
frames are below its required 58, and no fit/scene was published. Canonical world axes and unknown gravity/scale
remain explicit; no Q/P/N/E gate is newly certified.
