# Current handoff

Updated 2026-10-16 (Asia/Hong_Kong). **M1: VERIFIED for the Linux development
exit. M2: IN_PROGRESS. No full Q/P/N/E gate is certified. M7–M8:
DEFERRED_BY_USER.**

[User-approved plan v1.1](adr/012-m1-development-exit.md) changes M1 to a
Linux development exit with hosted native CPU CI. Physical Windows/macOS viewer,
GPU/display/input/HiDPI and the original ≤250k / 720p ≥30 FPS reference-device
checks move to M4. Full Q/P/N/E and release requirements remain unchanged.
The [completed M1 checklist review](validation/2026-10-15-m1-exit-review.md)
binds the consented real room chain, full 250k synthetic fixture, native fly and
replace/delete evidence, selected identity/license audit, local timings and a
successful four-job hosted CPU run at reviewed commit `788db85`. Remaining
hardware, corpus, benchmark and distribution obligations keep their later gate
statuses open.

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
retain calibrated/shared-origin scope. [All 80 replacement sample_3 cameras](validation/2026-10-15-all-camera-reference.md)
now pass locally; arbitrary-view robustness, packing Q6 and platform qualification
remain pending.

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

## Current saved reconstruction runs

Sources remain under `data/local/`; each video has one folder under `artifacts/`.
Read-only inspection on 2026-10-14 found newer state than the preceding handoff.
[Current-state evidence](validation/2026-10-14-dependency-closure.md) records exact
source/result/scene identities and structural validation, without starting jobs.

| Video | Current saved state |
|---|---|
| sample_1 | Completed 7k fit, **295,952 splats**; saved terminal/validation exits 0 and current native CLI bundle validation exit 0. Its previously inspected Source view remains available |
| sample_2 | Saved terminal exit 2, **POSE_DISCONNECTED**, no scene. Retain failed stages; unchanged retries cannot meet registration from the retained match graph |
| sample_3 | Replacement source SHA `4448a1d9eb93f8b7fc787f9ea037d282128438a2512fad8eb8df8544f53906da`. Saved **80 registered poses → 7k fit → 648,332 splats**, terminal/validation and native bundle validation exits 0. [Current full-scene checks](validation/2026-10-15-sample3-evidence.md): native viewer 126 frames / zero Vulkan errors; [all 80 calibrated comparisons](validation/2026-10-15-all-camera-reference.md) pass, maximum one RGB8 level; 16/16 offline holdout scores reproduce exactly, mean **20.8708 dB / 0.815687 SSIM / 0.253078 LPIPS**, Q3 remains blocked |

The preceding sample_3 disconnected/recovery records describe the older source
and attempt; they do not describe this completed replacement run. Historical
sample_1 recovery, sample_2 graph failure and the former sample_3 failures remain
in [model selection](validation/2026-10-13-model-selection.md),
[pose continuation](validation/2026-10-13-pose-continuation.md),
[service/orientation](validation/2026-10-13-service-orientation.md) and
[fresh reset](validation/2026-10-13-fresh-video-reset.md) evidence.
`request.json` / `source.json`, stage directories and `logs/result.json` own the
actual selected inputs and terminal results. Both completed scenes retain arbitrary
scale; geometry and walking are unavailable. No source, failed stage or completed
fit was restored, deleted or rerun to reconcile documentation.

```sh
# Inspect saved terminal state before considering another attempt:
cat artifacts/sample_1/logs/result.json
cat artifacts/sample_2/logs/result.json
cat artifacts/sample_3/logs/result.json
```

Future videos: `python3 tools/new_video_job.py data/local/VIDEO.mp4` creates
`artifacts/VIDEO/request.json` without starting processing. Existing folders are
refused; `--name VIDEO_retry_2` selects a separate attempt. Full SHA-256 stays in
`source.json`; all source data/artifacts remain ignored;
[layout protocol](protocols/artifacts.md).

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
format/tidy/docs and **2,393 unchanged protected files** pass. Its then-pending
native Windows rerun is covered by the later [hosted CPU confirmation](validation/2026-10-15-native-ci.md);
no reconstruction or environment change.

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
protected files unchanged**. The later [hosted CPU confirmation](validation/2026-10-15-native-ci.md)
covers its then-pending Windows rerun; no workflow,
recipe, reconstruction or environment changes. This does not certify Windows
viewer/release behavior or a full gate.

[Bounded PLY fuzzing](validation/2026-10-14-ply-fuzz.md): isolated opt-in Clang
C++26 libFuzzer + ASan/UBSan target, ten synthetic SH0–3/LF/CRLF/numeric/header
seeds and input/header/scene budgets. Its **60-second campaign executes 281,683
inputs** (101 accepted), **367 MiB peak RSS**, with zero retained crashes or
reported sanitizer errors. **257/257 dev / 239/239 C++23 CPU CTest**, format/tidy/
docs, rejected incompatible profiles and **2,393 unchanged protected files**
pass. All 94 tracked src/include files retain parent bytes. Mutation length reached
8,221 bytes of the 65,536-byte maximum; this bounded local result does not certify
all parser behavior, other platforms or a full gate. [Protocol](protocols/fuzzing.md)
gives reproducible campaign and crash-retention commands.

[Bounded scene transaction fuzzing](validation/2026-10-14-transaction-fuzz.md):
second isolated Clang C++26 target, 44 synthetic bytecode seeds and bounded
commit/replay/receipt/snapshot budgets. A native **60-second campaign executes
30,251 inputs**, **308 MiB peak RSS**, zero crashes/reported ASan/UBSan/LeakSanitizer
errors; caller aliases, immutable history, failed admission, corrected/repeated
operations and replay limits checked. **257/257 dev / 239/239 C++23 CPU checks**,
format/tidy/docs and **2,393 protected / 94 src/include files unchanged**.
Sandbox LeakSanitizer ptrace failure is retained separately; native runs keep
leak detection. No production/reconstruction/environment changes or gate pass.

[Bounded scene bundle fuzzing](validation/2026-10-14-bundle-fuzz.md): third
isolated Clang C++26 target, 196 synthetic seeds and explicit input/file/scene
budgets, checksum-repaired schema/binary mutations and caller-tree/snapshot
preservation. Native **55,915 inputs / 61 s**, **339 MiB peak RSS**, zero
crashes/reported ASan/UBSan/LeakSanitizer errors. **257/257 dev / 239/239 C++23
CPU checks**, format/tidy/docs and **2,393 protected / 94 src/include files
unchanged**. Sandbox LSan ptrace failure is retained separately; native runs keep
leak detection. No production/reconstruction/environment changes or gate pass.

[Concurrent renderer/TSan](validation/2026-10-14-render-tsan.md): three new
synthetic cases check four-producer receipt sharing, consumption, cancellation,
backpressure/caller preservation, corrected operations and retired residency.
Native final TSan **3 cases / 5,308 assertions**, ten further repetitions and
**242/242 CPU CTest** pass without reported races/suppressions. GCC C++26 dev
**260/260** / explicit C++23 CPU **242/242**, format/tidy/docs and **2,393 protected /
94 src/include files unchanged**. Test-only changes; no reconstruction, environment
or GPU rerun. This does not certify GPU lifetime, other platforms or full gates.

[Selected dependency source/notice audit](validation/2026-10-14-dependency-closure.md):
**82 components / seven profiles**, all four local native development archive pins
checked, **119 installed notice files** across 56 worker packages and exact
Triton 3.0.0 tag/source-notice trace. The offline checker validates tracked/local
bindings and rejects incomplete/stale evidence; strict closure correctly exits 3.
Original worker wheel/source bindings, embedded NVIDIA/LLVM/AMD terms, system
runtime closure, release-vcpkg resolution and model/dataset rights remain open.
System FFmpeg is GPL-enabled, differing from the proposed LGPL release profile.
**11 new / 79 worker cases** pass on Python 3.10/3.12/3.14; **260/260 dev /
242/242 explicit C++23 CPU CTest**. No production sources or environments changed,
no gate upgraded; current saved reconstruction state is reconciled above.

[Selected Linux runtime audit](validation/2026-10-14-runtime-dependencies.md):
**15 roots / 213 ELF files / 123 system packages / 96 system notices**, with
three local interpreter metadata/notice associations and exact worker ELF/RECORD
checks. A corrected loader SONAME observer resolves all selected-context edges;
initial false missing-loader results are retained. The manifest checker validates
complete graphs and rehashes current ELF/notice/interpreter bytes. **13 new / 92
worker cases** pass on Python 3.10/3.12/3.14; **260/260 dev / 242/242 C++23 CPU
CTest**. All **2,088 protected / 94 src/include files** unchanged. Source/rights,
actual import order and lazy driver/plugins remain unqualified; no gate upgrade,
installation, solver/fit or GPU/UI rerun.

[Actual backend/viewer mappings](validation/2026-10-15-backend-bindings.md): five
isolated observations / **208 ELF files**, **137** beyond the loader graph, no
changed overlap. Native training imports the prebuilt gsplat extension and sees
one CUDA device without initializing PyTorch's CUDA context; native empty viewer
completes **900 frames / zero Vulkan errors**. Sandbox visibility failures remain
separate. Source/raw/current-byte checks, **23 new / 115 worker cases** on three
interpreters, **260 dev / 242 C++23 CPU checks** and **2,088 protected / 94
src/include files unchanged** pass. No solve/fit/checkpoint/model/environment
changes, rights closure or full gate upgrade.

[Newly mapped owner/notice associations](validation/2026-10-15-binding-owners.md):
**137 unchanged ELF files / ten worker distributions + six system packages /
30 installed notices**, zero unattributed targets. Current RECORD/metadata/notice
checks and repeated read-only package queries pass; **15 new / 132 worker cases**
on three interpreters and **260 dev / 242 C++23 CPU checks** pass. All **2,088
protected / 94 src/include files** remain unchanged; no environment provisioning,
reconstruction or GPU run. Archive/bundled-term/rights gaps remain. The full local
manifest check now fails **2**: system snappy **1.2.2-3→1.3.1-1** removed the old
library path and zlib **1:1.3.2-3→1:1.3.2-4** changed recorded bytes. Old receipts
retain their historical identities; new associations do not conceal this drift.

[Runtime refresh](validation/2026-10-15-runtime-refresh.md) restores the active
full local check after actual fresh captures: **15 roots / 213 loader ELF files /
zero unresolved edges**, **five contexts / 208 mapped ELF files**, **137 unchanged
owner targets / 30 notices**. Only snappy/zlib differ; all root pins, earlier
summaries/raw receipts and **2,088 protected / 94 src/include files** are unchanged.
Native empty viewer completes **900 frames / zero Vulkan errors**; training sees
one CUDA device without PyTorch CUDA-context initialization. **260 dev / 242
C++23 CPU checks**, including **133 worker cases** each, pass. Old manifest still
fails **2**; active `python3 tools/check_dependency_manifest.py --local-evidence`
passes **0**, strict closure remains **3**. No source/environment/fit changes or
gate upgrade.

[Replacement sample_3 verification](validation/2026-10-15-sample3-evidence.md)
binds current source/fit/scene bytes to fresh native and offline checks. Saved
reference readbacks lacked a comparison report; saved holdout's metric receipt
is absent, although its metric identity matches the current pinned receipt.
Fresh ignored outputs close those scoped provenance gaps: three **718×1280**
full-scene comparisons pass at **63.08–63.13 dB / maximum one color level**,
reference fixture/readbacks remain byte-identical, and all **16** CPU holdout
metrics reproduce exactly. Current Source-view **126 frames / 648,332 resident
splats / zero Vulkan errors**; screenshot inspected at **1428×868**. PSNR/LPIPS
miss Q3 goals; independent inspection/observed regions remain absent. All
**2,088 protected / 94 src/include files** unchanged; no refitting, solve,
installation or download. No full gate or performance qualification change.

[Explicit calibrated selection](validation/2026-10-15-all-camera-reference.md)
adds matching bounded `--fit-indices` options, preserving the default three views
and unchanged **≥45 dB / ≤8 RGB8-level** diagnostic limits. All **80** retained
sample_3 cameras pass at **62.8596–63.3867 dB / maximum one level**; the final
binary's readbacks match the compared export exactly. Default three views remain
byte-identical; reordered `79,0,40` and four synthetic default views also pass.
**2 C++ cases / 174 assertions**, **18 Python admission cases**, six pre-GPU CLI
rejections, **262 dev / 244 explicit C++23 CPU checks**, format/tidy/docs/boundaries
pass. **2,088 protected / 54 preceding evidence / 94 src/include files** unchanged.
No solve, fit, recipe or environment change; Q3/Q6/full gates remain unqualified.

[Clang C++23 native qualification](validation/2026-10-15-clang-camera-reference.md)
passes on the same local device: explicit C++23/libstdc++, viewer enabled,
**262/262** CPU checks, **2 cases / 174 assertions**, **18** Python admission
cases and six pre-GPU CLI rejections. All **80**, default and reordered reference
reports match GCC byte for byte. Native viewer **340 frames / 648,332 splats /
zero Vulkan errors**, actual **1428×1746** Source-view capture inspected. No
performance/other-platform/full-gate qualification follows. **2,088 protected /
697 preceding evidence / 94 src/include files** unchanged; no implementation,
environment, scene or fit changes. The cut-off left completed receipts; these
were checked and retained without repeating the native matrix.

[Synthetic clipping-boundary evidence](validation/2026-10-15-clipping-boundaries.md)
checks exact near/far equality and immediate float neighbors in a separate
six-splat profile. Both planes include equality; all twelve independent
native/reference presence checks pass at **78.8331 dB / maximum one RGB8 level**.
The existing **32 synthetic / three full-scene real** comparisons also pass;
default fixtures/readbacks are unchanged. **1 C++ case / 27 assertions**, **26
Python cases**, **263 dev / 245 C++23 CPU checks** and format/tidy/docs/boundaries
pass. **2,088 protected / 1,206 preceding evidence / 94 src/include files**
unchanged. No production renderer/shader, recipe, environment or fit changes;
the diagnostic remains synthetic and no full gate is upgraded.

[Synthetic floating-origin evidence](validation/2026-10-15-floating-origin.md):
12 new comparisons at zero/positive/negative billion-unit offsets pass at
**≥72.9237 dB / maximum one RGB8 level**. Native and both reference image forms
are byte-identical across offsets; a 16,384-unit excursion/return exercises
accepted-scene rebasing. **2 C++ cases / 222 assertions**, **32 Python cases**,
**265 dev / 247 C++23 CPU checks** pass. Default/clipping/full sample_3 regressions,
format/tidy/docs/boundaries and preservation of **2,088 protected / 1,531 preceding
evidence / 94 production files** pass. No renderer/shader, fit or environment change;
no full gate upgrade.

[Hosted native CPU confirmation](validation/2026-10-15-native-ci.md) closes the
Windows rerun prerequisite: current code commit **dd43a8a** passes Windows
**246/246 CTest / 240.64 s**, including worker integration. GitHub's same-commit
job/step metadata also confirms successful Linux GCC/Clang and macOS C++23 CPU
builds/tests/docs/boundaries. Windows has one fewer case because an existing
closed-stdin descriptor test is POSIX-only. Other hosted counts, internal Python
skips, exact toolchain versions and macOS architecture are not inferred. All jobs
have the viewer disabled; no native viewer/release/full-gate upgrade.

[Frozen portable viewer fixture](validation/2026-10-15-viewer-fixture.md) now
provides the ≤250k viewer input without modifying either capture: four chunks,
**250,000 SH3 splats**, MIT notice, fixed camera and hashed assets. Five CPU
generations across GCC C++26/C++23 are byte-identical. Native 1280×720 full-count
readback and actual GUI loading pass with zero Vulkan errors; **267 dev / 249
C++23 CPU checks**, two cases / 54 assertions, five CLI rejects and static checks
pass. **2,088 protected / 1,936 prior evidence / 94 production files** unchanged.
This is a synthetic load fixture; Windows/macOS fixture execution remains
IMPLEMENTED_UNVERIFIED, and no FPS, reconstruction or full gate is certified.

M2 swapchain recovery is **IN_PROGRESS**. The [staged-resize check](validation/2026-10-15-m2-swapchain-recovery.md)
confirms a bounded native scratch-budget rejection preserves the previous scene
revision and renderable swapchain; native 51-frame synthetic smoke, tile plans and
full sample_1 windowed/4K/fullscreen/restore pass with zero Vulkan errors. The
same-scene first full-scene attempt exposed transient scratch duplication at
fullscreen and is retained as failing evidence. The subsequent
[post-create recovery](validation/2026-10-15-m2-postcreate-recovery.md) corrects
the Vulkan rule: calling `vkCreateSwapchainKHR` with `oldSwapchain` retires the
old presentation even on failure. An injected native post-create fault now clears
retired handles; the next draw recreates presentation from the retained scene.
Normal/timed smoke and full sample_1 windowed/4K/fullscreen/restore pass with
zero Vulkan errors. This Wayland compositor denied minimization, so the
minimized-frame skip remains IMPLEMENTED_UNVERIFIED.

The [device-loss recreation check](validation/2026-10-15-m2-device-loss.md)
maps `VK_ERROR_DEVICE_LOST` to a typed error. The viewer recreates its renderer
and Vulkan overlay, replaying the last admitted immutable scene through the
normal update path. Native synthetic-loss smoke preserves revision 1 and exact
overlap RGB8 `130,3,68`; the full 295,952-splat sample_1 viewer becomes visible
again and writes a 1428×1746 readback byte-identical to its native baseline,
with zero Vulkan errors.
The Debug fault is synthetic; physical device loss remains unverified. Release
C++23 builds without the hook and passes native smoke. M2 stays IN_PROGRESS.

The [two-hour native lifetime stress](validation/2026-10-15-m2-lifetime-stress.md)
passes on the retained 295,952-splat sample_1 scene: 7,203.746 seconds, 480
visible updates, 359 resizes, 13 byte-identical full-scene readbacks, 270,414
submitted frames and zero Vulkan validation errors. Tracked owned allocations
peak at 369,408,864 bytes under the 512 MiB budget; process HWM is 499,478,528
bytes. The saved scene manifest is unchanged. Empty alternate revisions are
synthetic, and this one native profile does not certify the full M2 gate. The
local X11 path also fails to report a minimized window after the compositor
request; accepted minimize/restore remains unverified.

[VMA buffer ownership](validation/2026-10-15-m2-vma.md) is implemented inside
`render_vulkan` with one allocator per device and dedicated, requirement-charged
buffers. The development archive is SHA-256 pinned to VMA 3.1.0, matching the
immutable vcpkg port's source SHA-512. GCC C++26 build/267 CPU and worker
checks, GCC and Clang C++23 viewer builds, native Debug/Release smoke, a
60-second full-scene update/resize pilot and full-scene 4K/fullscreen/restore
pass. The [VMA two-hour completion audit](validation/2026-10-16-m2-vma-lifetime.md)
also passes: 7,201.865 seconds, 480 visible updates, 359 resizes, 13 matching
readbacks, 298,300 submitted frames and zero Vulkan errors. Tracked peak owned
bytes remain 369,408,864; process HWM is 499,757,056 bytes. The release-vcpkg
profile remains unqualified. The [runtime refresh](validation/2026-10-16-vma-runtime-refresh.md)
recaptures both rebuilt executables and the empty native viewer: the complete
local inventory check passes, while strict source/rights closure still exits 3.

[Windows cancellation clock repair](validation/2026-10-16-windows-cancel-deadline.md):
the supplied native failure counted Python startup and fixture preparation
inside the cancellation bound. A delayed-ready replay reproduces the old false
failure; the observer now times from the first stop request while retaining six
seconds. Production cancellation and worker deadlines are unchanged. Native
Windows rerun remains pending. Local **267/267 C++26 / 249/249 C++23 CPU checks**,
the four-mode regression and format/tidy pass.

[Native P6 update replay](validation/2026-10-16-m2-p6-update-replay.md) now
measures the full saved 295,952-splat scene with synthetic marker replacement/
eviction at a scheduled 10 updates/s. On local C++23 Release/NVIDIA, all 100
revisions become visible with zero Vulkan errors and zero interactive device-idle
waits, but require **18.048 s**: enqueue p95 **196.268 ms**, GPU upload p95
**6.494 ms**, and visible rate **5.541/s**. P6 fails this local diagnostic.
Capacity retries now reject before full validation, and validation/hash run
outside the sink mutex; exact SHA batching preserves the saved scene hash.
**269/269 C++26 / 251/251 C++23 CPU checks**, focused TSan and timed native
smoke pass. The release-vcpkg and H_V gate remain unqualified.

[Prepared admission and staged uploads](validation/2026-10-16-m2-p6-staged-uploads.md)
now pass **two** local 100-update replays on that same saved scene plus a synthetic
marker: 15 MiB/frame staging, visible **10.011/10.008 updates/s**, enqueue p95
**0.015/0.010 ms**, per-frame GPU upload maximum **1.795/1.890 ms**, zero
interactive idle waits and Vulkan errors. The 60-second full-scene VMA pilot,
native smoke and windowed/4K/fullscreen/restore pass; 270/270 C++26 and 252/252
C++23 CPU checks plus focused TSan pass. This is local development evidence,
not the H_V/corpus P6 gate. The [staged-upload completion audit](validation/2026-10-16-m2-staged-lifetime.md)
now checks the finished 7,200.518-second run: 480 visible updates, 359 resizes,
13 identical readbacks, 313,366 frames and zero reported Vulkan errors. Its
original launch-time binary digest/process exit were not retained; the audit
does not reconstruct them from the successful completion artifacts.

[Accepted X11 minimize/restore](validation/2026-10-16-m2-minimize.md) now passes
on native Debug C++26 and Release C++23 using a separate rootful Xwayland display
and an owned test window manager. Three minimized draws submit no frames and
preserve an ACCEPTED update; restore publishes it with identical pixels and zero
Vulkan errors. Strict mode rejects denied minimize; ordinary Wayland smoke still
reports denial. The test no longer mistakes `SDL_SyncWindow=false` for absence
of an already observed minimized state. **270/270 dev CPU checks**, format/tidy
and **2,088 unchanged protected files** pass. This synthetic controlled-X11
result leaves physical-desktop/Wayland qualification open.

[Resident chunk reuse](validation/2026-10-16-m2-resident-reuse.md) addresses the
rebuilt replay's **2.090 ms** upload-frame miss by copying unchanged immutable
chunks from GPU memory. Atomic visibility, the 15 MiB frame cap and retirement
remain intact. Final **100/600-revision** local replays sustain 10 Hz, enqueue
p95 ≤0.008283 ms and upload maximum **0.194/0.229 ms**, with zero idle waits or
Vulkan errors. GCC/Clang native **24-phase** synthetic comparisons each match CPU
packing exactly across replacements, offsets, transforms and origin changes;
the full real 4K regression and strict X11 smoke pass. **270 dev / 252 C++23 CPU
checks**, format/tidy and **2,088 protected files unchanged** pass.

[Resident-copy lifetime completion](validation/2026-10-16-m2-resident-lifetime.md)
now passes the bound exit/source/binary and independent event/readback audit:
**7,200.746 s**, **480 updates**, **359 resizes**, **13 identical captures**,
**304,047 frames**, zero Vulkan errors and **520,936,256** peak tracked bytes.
Both host and resident copy totals match the full/full/empty revision sequence.

[Optional SH packing](validation/2026-10-16-m2-sh-packing.md) now compares both
GPU profiles along a frozen **100-view local path per full saved scene**.
The experimental profile reduces splat records **256→160 bytes**; default and
canonical storage remain FP32. All 400 final readbacks match the scored bytes,
with maximum one RGB8 level difference and mean pairwise PSNR **76.29/77.93 dB**.
GCC/Clang each pass 48 synthetic phases, including rejection/preservation/repair;
**273 dev / 255 C++23 CPU checks**, **139 worker checks** on Python 3.10/3.14,
format/tidy and **2,088 protected files unchanged** pass. This is pairwise
packing distortion, without ground-truth PSNR loss or LPIPS increase; Q6 remains
**IN_PROGRESS**.

[Captured-target packing quality](validation/2026-10-16-m2-packing-quality.md)
now pins and scores every retained calibrated camera: **63 sample_1 / 80 sample_3**,
both full scenes and identical captured targets/crops. Mean PSNR loss is
**0.00001164 / 0.00000528 dB**; LPIPS increase **0.000000740 / 0.000000189**.
All 143 individual comparisons meet the unchanged numeric limits. Training,
guard and holdout scores remain separate; Q3 still fails locally. The final
parser-clean binary reproduces all **286 readbacks** exactly, with zero Vulkan
errors. **273 dev / 255 C++23 CPU checks**, **149 worker checks** on three Python
versions, synthetic numerical/admission checks and format/tidy pass. This is
local captured-target evidence, not Q6's prescribed fixed 100-view corpus or
scale/platform qualification. Saved reconstructions and user edits are preserved.

[Renderer runtime refresh](validation/2026-10-16-packing-runtime-refresh.md)
now restores the full local inventory check: **15 loader roots / 213 ELF files**,
zero unresolved edges; fresh native empty viewer **900 frames / zero Vulkan
errors**. Its broader mapping observation retains **237 ELF files**, with
**160** beyond the loader baseline; fresh owner associations cover **31 groups /
41 installed notices**. Four retained worker-import receipts are rechecked,
not rerun. The new selected inventory passes default/local checks; strict closure
still exits 3 for **253 profile source/rights gaps**, with owner terms separately
unqualified. Historical receipts, all reconstructions and user edits are retained.

Next executable step: extend `we_gpu_lifetime_stress` with an explicit experimental
`--fp16-sh` option, retaining the full/full/empty resident-copy sequence and using
the selected stride for byte accounting. Run a 60-second native packed pilot,
then bind source/binary/shader/scene identities and actual exit for a 7,200-second
packed update/resize stress; do not infer its result from the FP32 lifetime run.
Q6 still needs the prescribed fixed 100-view captured-target fixture and qualified
scale/corpus/platforms. Preserve saved scenes, failures and both frozen comparisons;
do not rerun reconstruction.
Physical loss, desktop/Wayland minimize, release-vcpkg, reference Q/P/N/E and
other-scene/platform qualification remain open.

[Physical Windows/macOS qualification](adr/012-m1-development-exit.md) is scheduled
for M4, including the unchanged frozen bundle/hash, native viewers, GPU/display/
input/HiDPI and 720p ≥30 FPS on H_V/H_M. H_R P1 and other reference measurements
stay with their assigned later gates. These remain unverified release obligations,
not M1 hardware prerequisites. Preserve both completed scenes and all failures;
no refitting, solver reruns, downloads or unchanged sample_2 retries. The
[revision record](validation/2026-10-15-m1-exit-policy.md) retains authorized
spec/runbook hash changes and the current preservation baseline.

[Windows loader-path roundtrip repair](validation/2026-10-15-windows-runtime-roundtrip.md):
latest supplied C++23 CPU run **240/241**, Python 3.14.7 worker suite seven errors /
one failure. The previous fixture adapter missed capture's Windows `Path` string
conversion; contained translation now handles the drive-less virtual namespace
and mutation cleanup uses `finally`. Full-capture replay reproduces all eight
reported error/failure records and passes seven repaired cases. **16 runtime /
133 worker checks** on three interpreters and **260 dev / 242 C++23 CPU CTest**
pass locally; **2,088 protected / 94 src/include files** unchanged. Production,
runtime pins and saved reconstructions are unchanged. The later
[hosted Windows CPU run](validation/2026-10-15-native-ci.md) passes **246/246**;
the earlier local replay retains its narrower qualification.

For these videos, use the working sample_1 view below; preserve
sample_2's failed stages until replacement footage or a qualified pose recovery
is available, and both completed scenes. Do not restart identical failed solves or
completed fits. Current sample_1 view:
`build/dev/we_viewer --scene artifacts/sample_1/scene.wexplore --source-view --max-splats 1000000`.
Source view is a viewing reference, not measured gravity or floor/collision.

Reference hardware, prescribed fixture/platform qualification and complete gates
remain pending. Do not weaken thresholds or silently trim either full scene.
Timestamp sampling and blur/cut/lens/coverage diagnostics retain their recorded
limits. `--source-view` remains an independent spectator reset. Unknown/inadequate
storage fails; metadata inspection still applies the decoded-storage estimate.

Other native devices/platforms, corpus/reference performance, geometry/collision,
broader fuzzing and full gates remain pending; [requirements](requirements.md)
tracks scope. Earlier [supplied native CPU logs and metric observer repair](validation/2026-10-14-native-metric-paths.md)
show Windows **101/102** and macOS **102/103**: both fail the metric mutation
observer's unresolved/canonical path comparison. Portable alias replay reproduces
the exact failure; canonical observers and exactly-once mutation checks now pass:
**18 metric / 68 worker cases** on three interpreters, **103/103 C++23 CPU /
121/121 C++26 dev**, **2,393 protected files unchanged**. The later [hosted CPU confirmation](validation/2026-10-15-native-ci.md)
passes Windows worker integration and the macOS CPU job. No reconstruction or
environment changes. The [earlier audit observer repair](validation/2026-10-13-native-audit-paths.md)
retains its historical 12 audit / 37 worker / 119 dev scope. Named SQLite recovery
cases pass in the latest supplied logs. The earlier [SQLite closure repair](validation/2026-10-13-windows-sqlite.md)
retains local 102/102 / 25-case scope; prior Windows 100/101 cleanup failure,
macOS CPU pass and Windows 77/78 (worker only) remain historical;
[flush repair](validation/2026-10-12-windows-flush.md) records local C++23 79/79 and
Python 3.12/3.14 9/9. Native viewer/release qualification is separate. Sandbox ICD
failure is not GPU absence. Local commits are authorized; pushes/uploads are not.
Follow the [documentation protocol](protocols/documentation.md).
