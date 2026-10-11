# Requirement and evidence matrix

The original targets in `technical_plan.md` §3 and §8 remain controlling; no
thresholds or platform requirements have been waived. Status on 2026-10-11:

| Requirement | Implementation / checks | Status | Evidence |
|---|---|---|---|
| M1 vertical slice | Real COLMAP 63/63 → 7k gsplat → checked scene → native viewer; shared GUI/CLI continuation command | IN_PROGRESS | Explicit recipe/resume/reuse and checked publication; automatic pose chaining, prescribed fixture, contracts and native reference/platform qualification pending |
| M1 four contracts | capture/pose interfaces, map/scene revision code, update sink | IN_PROGRESS | CPU tests pass; concrete adapters and FlatBuffers pending |
| M2 renderer / updates | Initial GPU projection/radix/quad code | IN_PROGRESS | Parity, lifetime stress and performance pending |
| M3 offline geometry / walk | Worker cancellation/stages started; no supported geometry | NOT_STARTED | Full milestone unverified |
| M4 platforms / Brush | Native source portability intended | BLOCKED | Windows, Apple Silicon and AMD runners unavailable |
| M5 larger scenes | Bounded initial admission only | NOT_STARTED | No measured LOD or W3/W4 |
| M6 release candidate | No release certification | NOT_STARTED | Depends on M1–M5 |
| Q1 completion ≥18/20 | One user-authorized sample available | BLOCKED | Sealed consented 20-video corpus absent |
| Q2 30k appearance | Pinned gsplat adapter; no benchmark run | NOT_STARTED | Dataset/metric/model locks and rights pending |
| Q3 held-out video quality | Development temporal holdout measured; no training RGB leakage | BLOCKED | Independent inspection capture and metric/backbone lock absent; no Q3 pass |
| Q4 pose accuracy | COLMAP pose adapter | NOT_STARTED | Fixed GT subset absent |
| Q5 measured geometry | No geometry pipeline yet | NOT_STARTED | Independent geometry GT absent |
| Q6 packing fidelity | Uncompressed canonical storage | NOT_STARTED | 100-view render comparison pending |
| P1 complete offline time | Pose stages 357.989 s; actual 7k fit/compile measured separately | IN_PROGRESS | No continuous whole-video reference H_R timing/certification |
| P2/P3 viewer performance | Native GPU validation smoke passes | BLOCKED | Reference hardware and statistical measurements absent |
| P4 bounded memory | Input/admission/retirement budget tests pass | IN_PROGRESS | Peak process/device/training measurements pending |
| P5 coarse-first load | Initial asynchronous load | NOT_STARTED | LOD not implemented |
| P6 asynchronous updates | Replay, retry, retirement tests and native replace/delete pass | IN_PROGRESS | Staged upload/latency/stress pending |
| N1 fly controls | Camera + gated SDL input; CPU tests pass | IN_PROGRESS | Full native input/HiDPI/latency inspection pending |
| N2 walking | Disabled until independent scale/geometry | NOT_STARTED | No invented collision |
| E1 correctness/coverage | Contract/worker/coordinator tests; native sync validation including render pause/resume; targeted lint | IN_PROGRESS | Current profile counts in coordinator evidence; coverage, fuzzing and full gate fixtures pending |
| E2 build/delivery | Local GCC/Clang C++26 and explicit C++23 builds; pinned dev archives; corrected native CPU CI toolchains/SDK discovery | IN_PROGRESS | CI repair locally checked; hosted rerun, native viewer/release profile and complete distribution-hash qualification pending |
| L1 / M7–M8 | Only future-compatible contracts in Phase 1 | DEFERRED_BY_USER | No live reconstruction implementation |

Each gate requires the measurement scope prescribed by the specification. A local
synthetic smoke test does not satisfy quality, performance, capture completion or
another operating system's native qualification.

Commands and narrow local results: [foundation evidence](validation/2026-10-11-foundation.md).
Current fit and checks: [capture evidence](validation/2026-10-11-capture.md).
Shared job command checks: [coordinator evidence](validation/2026-10-11-coordinator.md).
Native CPU workflow repair: [CI evidence](validation/2026-10-11-ci.md).
No Q/P/N/E requirement is fully certified; configured CI is not a native pass.
