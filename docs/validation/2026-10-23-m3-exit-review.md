# M3 local software exit review — 2026-10-23 (Asia/Hong_Kong)

**M3: IN_PROGRESS. The local development exit is not complete.** Review of
[plan §8.3](../technical_plan.md#83-m3-local-development-exit-and-deferred-reconstruction-qualification)
and [ADR 020](../adr/020-m3-development-exit.md) finds a remaining software gap:
keyframe selection is still uniform timestamp sampling; planned frame-quality
selection/preflight is absent. This gap is independent of unavailable sample_1
measurements, release quality targets and later platform qualification. A0–A6
remain NOT_STARTED; M4 has not begun.

Cwd: repository root; reviewed parent `eb626d3` plus the selected dependency
refresh's one-line default-manifest change. No reconstruction or media processing
runs for this review. `artifacts/validation/m3-exit-review-20261023/review-inputs.sha256`
pins the inspected worker, pose solver, preflight implementation, plan and AGENTS;
receipt SHA-256:
`a8c9584d2b45953f7c6877cef0324cff45363b08901b838969dd87579c880711`.

| §8.3 item | Observations and result |
|---|---|
| 1. Planned software workflow | **IN_PROGRESS.** Metadata/storage/request preflight and explicit sequential/exhaustive-guided pose recipes exist. Retained recovery attempts preserve genuine disconnected failures. Dense depth, 63-view fusion, captured appearance/GLB and scale/collision contracts have implementation evidence. However, `workers/worker.py` selects only by `instant >= next_time`, emits `selection: timestamp_sampling_v1`, and explicitly reports blur/exposure/motion/cut selection pending. No quality scores, decision ledger or rejection policy supplies plan §5.1 step 2. Metadata and distinct PNG hashes do not implement that requirement. |
| 2. Failure/resume/publication | **VERIFIED in the tested local scope.** [Full-sequence fusion](2026-10-20-sequence-fusion.md), [fresh sequence adapter](2026-10-20-fresh-sequence.md) and [owned mesh jobs](2026-10-20-mesh-job.md) exercise real cancellation, preserved attempts, completed-stage/cache reuse, exact replay and immutable publication. Pose disconnect/stale-input and transactional CPU tests preserve valid prior output. Appearance restart remains explicitly distinct from optimizer-checkpoint resume; no PLY is admitted as an optimizer checkpoint. Crash/power-loss and release-wide recovery remain unqualified. |
| 3. Native handoff and synthetic collision | **VERIFIED locally.** [Current native evidence](2026-10-23-step-native.md) reopens the retained 39,373-vertex/49,213-triangle real captured-color result with zero Vulkan errors. Source-bound synthetic floor/wall/unknown, ramps and selected-step checks retain conservative blocking, correction/stop and failure preservation. Twelve latest native step assertions pass; short/mixed-schema startup rejects. Real output stays fragmented/arbitrary and walking unavailable; all 5,184 real space cells remain unknown. |
| 4. Local build/check evidence | **VERIFIED for recorded profiles.** [Controller integration](2026-10-23-step-controller.md) records GNU C++26 486/486 and explicit C++23 461/461 CTest, 51 sanitizer cases/30,451 assertions. The [fresh inventory/tooling report](2026-10-23-m3-dependency-refresh.md) adds 65 audit contract cases, pinned Black, current loader/import/owner receipts and exact LLVM 23.1.1 scoped format/tidy. Existing checks are scoped source-identical evidence, not newly rerun C++ builds. |
| 5. Unqualified targets | **VERIFIED as an explicit mapping below; no full gate closes.** Original thresholds/corpus/reference devices stay unchanged. Item 1 prevents the local exit even though these limitations are properly disclosed. |

## Full gate mapping

Statuses retain the current [requirements matrix](../requirements.md).

| Gate | Status / evidence boundary |
|---|---|
| Q1 | BLOCKED: retained sample_2 79/90 versus 81 required; no sealed 20-clip result. Software quality-selection gap also remains. |
| Q2 | NOT_STARTED: no prescribed 30k seven-scene benchmark. |
| Q3 | BLOCKED: local temporal holdouts/metrics exist; release video-quality corpus is unqualified. |
| Q4 | NOT_STARTED: pose conventions/imports do not supply independent pose ground truth. |
| Q5 | NOT_STARTED: no independent scale/surface GT; partial depth/mesh and synthetic alignment do not substitute. |
| Q6 | IN_PROGRESS: local packing/reference comparisons exist; prescribed corpus/device modes remain open. |
| P1 | IN_PROGRESS: separate local stage observations are not the prescribed complete hybrid H_R timing. |
| P2/P3 | BLOCKED: reference NVIDIA/AMD/Apple and native portable measurements remain M4/M6 work. |
| P4 | IN_PROGRESS: local allocation/admission/lifetime limits do not certify every prescribed pressure workload. |
| P5 | NOT_STARTED: asynchronous initial loading does not certify coarse-first streaming. |
| P6 | IN_PROGRESS: local update/recovery tests do not complete prescribed reference-device qualification. |
| N1 | IN_PROGRESS: native/compositor input checks do not certify physical input-to-display latency. |
| N2 | NOT_STARTED: no measured certified walk region or 100-path corpus; synthetic steps grant no real walking. |
| E1 | IN_PROGRESS: contract/style/sanitizer evidence is narrower than full coverage/fuzz/release correctness. |
| E2 | IN_PROGRESS: local/native CPU portability and selected inventory do not complete native package/build/signing qualification. All 257 source/rights gaps remain explicit. |

Next is bounded versioned frame-quality preflight/selection, integrated with the
shared decode/job path and exercised on retained development inputs. Preserve
existing timestamp-sampling records and immutable images/jobs; no retrospective
reselection or corpus tuning. Record scores, selection/rejection reasons, source
PTS/image/calibration identities, recipe/threshold pins and explicit unsupported
cases. Revisit item 1 after that software slice and its local validation. No new
sample_1 measurement is required for this development handoff.
