# M3 local development exit — 23 October 2026

**VERIFIED: M3 local software development exit on Linux/NVIDIA**, under
[plan §8.3](../technical_plan.md#83-m3-local-development-exit-and-deferred-reconstruction-qualification)
and [ADR 020](../adr/020-m3-development-exit.md). This completes the checklist
left open by the [earlier review](2026-10-23-m3-exit-review.md); that report's
observations remain historical. No full Q/P/N/E gate closes. A0 is next;
A1's real completed-asset/PBR pilot remains mandatory before expansion or M4.

Repository root is the cwd; reviewed implementation `a37b20a`. Ignored evidence
root `R=artifacts/validation/m3-development-exit-20261023`.
`review-inputs.json` pins inspected specification, instructions, implementation
and reports, SHA-256
`d4e356cd06cf6e55db6f45c4fd1f7554e196b48029f599795bfc837a967962e9`.
`review-checks.json` records source/data comparisons, SHA-256
`cac440b3fa2c3b8874ac7c6011df5c935ebe05e1e232059761bac075f4ef80df`.

## Five-item review

| §8.3 item | Evidence and scoped result |
|---|---|
| 1. Software workflow | **VERIFIED locally.** Inspection, storage/request preflight, explicit quality selection/rejection and source-bound original images are implemented. [Persistent selection](2026-10-23-frame-tracking.md) selects 24/63 real candidates and publishes 24/24 poses after one documented exhaustive-guided recovery. [Calibration preflight](2026-10-23-calibration-preflight.md) conservatively refuses ambiguous image-scale changes. [Dense sequence](2026-10-20-depth-sequence.md), [fusion](2026-10-20-sequence-fusion.md), [mesh jobs](2026-10-20-mesh-job.md), [world-frame alignment](2026-10-20-world-frame.md) and [native collision checks](2026-10-23-step-native.md) exercise the remaining workflow/contracts. Captured-color native/GLB output exists; scale, geometry and walking limitations remain explicit. |
| 2. Preservation and recovery | **VERIFIED for defined local contracts.** Owned geometry/postprocessing tests exercise cancellation, retained failures, exact replay and completed-stage reuse. The v2 failed sequential solve and successful single recovery remain separate. V3 refuses before RGB completion/pose solving and preserves the prior results. Native admission rejects stale/foreign/rehash-altered histories. Appearance stage restart is explicitly distinct from optimizer-checkpoint resume; no PLY masquerades as an optimizer checkpoint. Power-loss/release-wide recovery remains unqualified. |
| 3. Native handoff and collision | **VERIFIED locally.** The retained real bundle reopens with 39,373 vertices/49,213 triangles and zero Vulkan errors. Source-bound synthetic floor/barrier/unknown, ramps and selected-step fixtures exercise blocking, correction, pause/stop and failure preservation. Twelve native step assertions pass. All nine step implementation/test source hashes still match those checks. Real output remains fragmented, arbitrary-scale and unwalkable; all 5,184 real cells remain unknown. |
| 4. Current checks | **VERIFIED for tested profiles.** Latest actual C++26/C++23 CTest results are 492/492 and 467/467; Python 3.12 frame suite 38 cases; native ASan/UBSan/LSan seven cases/18,567 assertions. Exact LLVM 23.1.1 scoped style, pinned Black, docs/boundaries/whitespace and current selected runtime checks pass. Older broader geometry/collision sanitizer and native evidence retain their original scope. |
| 5. Qualification boundaries | **VERIFIED as the mapping below.** Targets, workload scope, corpus denominators and reference devices are unchanged. The local software handoff is separate from all full gates. |

## Preflight and implementation limits

V3 reports **23 ambiguous scale observations / 34 insufficient-support intervals /
four assessed / two unassessed** on sample_1 and rejects it. That outcome is
retained, not counted as Q1 success, confirmed zoom, or evidence of fixed
intrinsics. All 63 underlying v2 decisions/thumbnails still match; the v2 map
is not retroactively certified by v3. Neither image-grid coverage nor coherent
image scale proves 3D parallax. Automatic multi-lens segmentation is unsupported;
the supported solver recipe assumes one lens and rejects suspected changes.

Inspection of pinned pycolmap 4.2.1 confirms sequential overlap 10 and mapper
initial triangulation minimum 16 degrees (`colmap-options.json`). Backend
geometric verification and canonical minimum-camera/track checks reject failed
initialization; incomparable components are not combined. Vocabulary retrieval
is disabled, and no larger/global-mapping qualification is claimed. The bounded
sequential/exhaustive recipes and their failed/successful outcomes are the local
software evidence; broader capture eligibility and numerical quality remain Q1/Q4.

Four of five mesh-job C++ inputs match the original mesh-job report. The later
world-frame change adds refusal of already aligned inputs; its own alignment
checks cover that change. No older receipt is relabeled as execution of current
binaries. This review reruns no solve, fit, dense, fusion, bake, native input or
unchanged stress test. All 378 protected media/result files plus four protected
instruction/legacy files remain unchanged.

## Full gates remain open

| Gate / retained status | Missing prescribed evidence |
|---|---|
| Q1 BLOCKED | Sealed 20-clip corpus; sample_2 remains 79/90 versus 81 required; v3 rejection reported |
| Q2 NOT_STARTED | Prescribed 30k seven-scene benchmark |
| Q3 BLOCKED | Release held-out corpus/targets; retained real failures remain |
| Q4 NOT_STARTED | Independent pose ground truth; current pose checks are narrower |
| Q5 NOT_STARTED | Independent scale/surface ground truth and measured geometry |
| Q6 IN_PROGRESS | Complete prescribed packing corpus/device modes |
| P1 IN_PROGRESS | Complete hybrid H_R timing, rather than separate local stages |
| P2/P3 BLOCKED | Prescribed native/reference NVIDIA/AMD/Apple measurements |
| P4 IN_PROGRESS | All specified memory-pressure workloads |
| P5 NOT_STARTED | Coarse-first/LOD streaming, assigned later |
| P6 IN_PROGRESS | Complete reference-device asynchronous-update qualification |
| N1 IN_PROGRESS | Physical input-to-display latency |
| N2 NOT_STARTED | Measured certified region and 100-path corpus; no real walking enabled |
| E1 IN_PROGRESS | Full coverage/fuzz/release correctness |
| E2 IN_PROGRESS | Native packages/build/signing and dependency/rights closure |

Actual review commands `python3 tools/check_dependency_manifest.py`, then
`--local-evidence`, exit **0**; adding `--require-closure` exits **3** with all
**257 gaps / 86 components / seven profiles**, rights unqualified. Exact arguments
and exits are in `inventory.json`. Documentation/diff checks are recorded at finish.
Next: A0's additive schemas, dependency/provenance/resource contracts and synthetic
admission/recovery fixtures; no model provisioning or asset inference is implied.
