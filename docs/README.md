# Documentation map

Read by task; do not load the entire directory for routine work.

| Need | Source |
|---|---|
| Resume development | [Progress](progress.md) → [requirements](requirements.md) → relevant code/evidence |
| Inspect experimental keyframe selection | [Candidate selection](protocols/frame_selection.md); bounded diagnostics, checked ledger, explicit opt-in and unqualified thresholds |
| Agent execution rules | [Runbook](execution_prompt.md), repository [AGENTS.md](../AGENTS.md) |
| Product, thresholds, architecture, roadmap | [Technical plan](technical_plan.md); controlling specification |
| M3 development exit and deferred Q5/N2 evidence | [Plan §8.3](technical_plan.md#83-m3-local-development-exit-and-deferred-reconstruction-qualification), [ADR 020](adr/020-m3-development-exit.md); no measurement required for internet-sourced `sample_1`, measured geometry/walking gates remain open |
| Planned reusable objects, completed environments and PBR assets | [Plan §11](technical_plan.md#11-offline-asset-production-track), [assets-before-platforms decision](adr/019-assets-before-platforms.md); A0 contracts IN_PROGRESS after M3/before M4; A1–A6 product work NOT_STARTED |
| Asset-pipeline candidate evidence, licenses and connection experiments | [Dated research assessment](research/offline_asset_pipeline.md); external facts only, no installed recipe or qualification |
| Write/update documentation | [Documentation protocol](protocols/documentation.md) |
| Implement an interface | [Capture](protocols/capture.md), [poses](protocols/poses.md), [maps](protocols/maps.md), [scenes](protocols/scene.md), [depth](protocols/depth.md), [dense inputs](protocols/dense_inputs.md), [workers](protocols/worker.md), [jobs](protocols/jobs.md), [viewer settings/camera/handoff](protocols/viewer.md) |
| Inspect initial A0 contracts | [Dependency graph](protocols/asset_dependencies.md), [byte receipts](protocols/asset_receipts.md), [recipe locks](protocols/asset_recipes.md), [component evidence](protocols/asset_component_evidence.md), [installed trees](protocols/asset_installed_components.md), [region provenance](protocols/asset_region_provenance.md), [typed definitions/instances](protocols/asset_definitions.md), [strict metadata I/O/history](protocols/asset_metadata_io.md), [object observations](protocols/object_observations.md), [capture/evaluation selections](protocols/capture_selections.md), [declared manual-action journal](protocols/manual_actions.md), [action-bound draft replacement](protocols/draft_mutations.md), [typed object-mask edits](protocols/object_mask_edits.md), [draft attempt outcomes](protocols/draft_attempts.md), [generated static definition edits](protocols/asset_definition_edits.md), [sealed evaluation metadata](protocols/evaluation_corpus.md); bounded CPU admission/publication, selected bytes/trees, region lineage and immutable metadata; runtime/rights/real-support qualification pending |
| Organize outputs / add a local video | [Artifact layout and request helper](protocols/artifacts.md) |
| Qualify the isolated local dense backend | [Dense development experiments](../workers/dense/README.md), [separate resized inputs](protocols/dense_inputs.md#separate-resized-profile); partial producer qualification; owned fresh-depth integration is listed below |
| Export checked depth for visibility/fusion preparation | [Pose-admitted visibility receipt](protocols/depth.md#pose-admitted-visibility-export); immutable depth/RGB cache, no fused geometry |
| Inspect partial observed geometry | [Native mesh runtime and request](protocols/scene.md#observed-mesh-runtime); normal-color Vulkan view, unknown gaps, no measured scale/collision |
| Save/reopen native mesh snapshots | [Schema-2 bundle persistence](protocols/scene.md#native-mesh-bundle-persistence); exact raw/proxy geometry and provenance, compatible splat-only bytes |
| Audit color support before texturing | [Captured-color projection audit](protocols/mesh_color.md); pinned single-cache or complete sequence depth/RGB, full candidate/rejection/owner/seam ledgers, diagnostic atlas only |
| Bake and view captured mesh color | [Bounded native atlas](protocols/mesh_color.md#bounded-native-baked-appearance); checked linear RGB/UV/validity, legacy schema-3 and sequence schema-4 bundles, unlit native viewing |
| Export/import captured appearance | [Restricted unlit GLB](protocols/gltf.md); legacy/sequence ownership, full geometry/pins, declared filtering/quantization losses, bounded owned-resource import |
| Audit components before mesh cleanup | [Read-only component/support analysis](protocols/fusion.md#read-only-component-and-support-analysis); complete areas and directional TSDF stencils, no automatic removal |
| Simplify admitted observed meshes | [Certified local cleanup](protocols/mesh_cleanup.md); immutable proxy certificates, full-surface error bounds and native raw/proxy viewing |
| Run/admit bounded observed-depth fusion | [CPU TSDF and native interchange contract](protocols/fusion.md), [component selection](research/observed_fusion.md); pose-bound partial arbitrary-scale geometry; separate native runtime view, no collision |
| Resume retained-cache geometry processing | [Owned Linux geometry job](protocols/fusion.md#owned-retained-cache-geometry-job-linux-development-slice); exact cache/pose ownership, partial TSDF stage and cancellation; shared solver/GUI ownership is implemented below |
| Produce owned fresh CUDA depth | [Clean profile and depth owner](protocols/dense_inputs.md#owned-cuda-depth-production), [shared fresh geometry job](protocols/fusion.md#fresh-depth-through-the-shared-geometry-job); nine-view fresh solve and complete sequence-depth reuse, full room/geometry gates open |
| Admit complete depth sequences | [Bounded temporal production](protocols/dense_inputs.md#bounded-full-sequence-depth), [native cache unions](protocols/depth.md#native-sequence-admission); preserved batch recovery, complete frame accounting and streaming consumer, separate from full-sequence fusion |
| Inspect synthetic space support | [Collision-support profile](protocols/collision_support.md); pinned explicit cells/floor tiles, conservative envelope probes and an ordinary native viewer diagnostic; no measured walking |
| Cook synthetic collision triangles | [Canonical collision input](protocols/collision_cook.md), [Jolt version review](research/jolt_dependency.md); immutable source/support-bound floor and explicit wall-face triangles, portable digest and private static shape/ray query |
| Exercise synthetic capsule movement | [Kinematic controller](protocols/kinematic_capsule.md), [inclined support](protocols/collision_ramps.md) and [application session](protocols/capsule_session.md); shared pinned CLI/native controls, fixed 120 Hz, capped catch-up, Jolt wall sliding, unknown/unsupported blocking and terminal correction/stop; locally exercised flat/inclined and selected-step synthetic diagnostics; no measured walking |
| Inspect synthetic step geometry and clearance | [V4 step admission, certificates and proposals](protocols/collision_steps.md); source pinned floors/riser, conservative lift/cross/descent and capsule/corner sweep proofs, owned bounded controller transitions and raw Jolt replay; synthetic diagnostics only, no measured walking |
| Inspect observation-backed space | [Depth-space diagnostic](protocols/depth_space.md); full retained depth/RGB/pose admission, complete-cell evidence, immutable typed reload/stale-context rejection and shared native viewing; no measured collision |
| Admit explicit scale/up declarations | [World-frame alignment](protocols/world_frame.md); source-bound mesh anchors, schema-5 native bundles, atomic epoch changes; synthetic validation only, no walking |
| Run owned cleanup/bake/export | [Mesh postprocessing jobs](protocols/mesh_jobs.md); pinned native inputs, immutable attempts, explicit saved-bake reuse and shared GUI/CLI handoff |
| Run retained-cache geometry from GUI/CLI | [Unified retained-cache job](protocols/fusion.md#unified-retained-cache-solver-and-viewer-job); owned solver/reuse, native adoption and scene handoff; fresh depth uses the shared adapter above |
| Adopt completed geometry jobs natively | [Native terminal adoption](protocols/fusion.md#native-terminal-adoption-and-scene-publication); checked conversion, pose admission, retained attempts and ordinary scene bundles |
| Audit local worker/metric identities | [Installed identity receipts](protocols/worker_identity.md), [isolated CPU metric profile](../workers/metrics/README.md) |
| Check selected dependency provenance gaps | [Source/archive/notice inventory](protocols/dependency_sources.md) |
| Capture/check selected Linux ELF dependencies | [Runtime dependency receipts](protocols/runtime_dependencies.md) |
| Check actual backend imports / owned viewer mappings | [Observed Linux mappings](protocols/backend_bindings.md) |
| Check newly mapped owners and installed notices | [Installed owner associations](protocols/binding_owners.md) |
| Find historical results | [Validation index](validation/README.md); reports preserve their original scope |
| Understand a consequential choice | [Decision records](adr/) |
| Build/use the application | Repository [README](../README.md) |
| Measure CPU contract coverage | [Coverage protocol](protocols/coverage.md), reviewed [scope](../tools/coverage_scope.json) |
| Run bounded synthetic parser/update fuzzing | [Fuzzing protocol](protocols/fuzzing.md), isolated Clang sanitizer preset |

Current state belongs in progress/requirements; historical measurements belong in
validation reports. Protocols describe implemented behavior, not planned capability.
Research supplements own dated candidate facts; the technical plan owns decisions,
contracts, milestones and acceptance targets.
