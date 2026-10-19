# Documentation map

Read by task; do not load the entire directory for routine work.

| Need | Source |
|---|---|
| Resume development | [Progress](progress.md) → [requirements](requirements.md) → relevant code/evidence |
| Agent execution rules | [Runbook](execution_prompt.md), repository [AGENTS.md](../AGENTS.md) |
| Product, thresholds, architecture, roadmap | [Technical plan](technical_plan.md); controlling specification |
| Planned reusable objects, completed environments and PBR assets | [Plan §11](technical_plan.md#11-offline-asset-production-track), [assets-before-platforms decision](adr/019-assets-before-platforms.md); A0–A5 after M3/before M4, all new capabilities NOT_STARTED |
| Asset-pipeline candidate evidence, licenses and connection experiments | [Dated research assessment](research/offline_asset_pipeline.md); external facts only, no installed recipe or qualification |
| Write/update documentation | [Documentation protocol](protocols/documentation.md) |
| Implement an interface | [Capture](protocols/capture.md), [poses](protocols/poses.md), [maps](protocols/maps.md), [scenes](protocols/scene.md), [depth](protocols/depth.md), [dense inputs](protocols/dense_inputs.md), [workers](protocols/worker.md), [jobs](protocols/jobs.md), [viewer settings/camera/handoff](protocols/viewer.md) |
| Organize outputs / add a local video | [Artifact layout and request helper](protocols/artifacts.md) |
| Qualify the isolated local dense backend | [Dense development experiments](../workers/dense/README.md), [separate resized inputs](protocols/dense_inputs.md#separate-resized-profile); partial output, production job/fusion/geometry remain open |
| Export checked depth for visibility/fusion preparation | [Pose-admitted visibility receipt](protocols/depth.md#pose-admitted-visibility-export); immutable depth/RGB cache, no fused geometry |
| Inspect partial observed geometry | [Native mesh runtime and request](protocols/scene.md#observed-mesh-runtime); normal-color Vulkan view, unknown gaps, no measured scale/collision |
| Save/reopen native mesh snapshots | [Schema-2 bundle persistence](protocols/scene.md#native-mesh-bundle-persistence); exact raw/proxy geometry and provenance, compatible splat-only bytes |
| Audit color support before texturing | [Captured-color projection audit](protocols/mesh_color.md); pinned depth/RGB, full sample/rejection/owner/seam ledgers, diagnostic atlas only |
| Audit components before mesh cleanup | [Read-only component/support analysis](protocols/fusion.md#read-only-component-and-support-analysis); complete areas and directional TSDF stencils, no automatic removal |
| Simplify admitted observed meshes | [Certified local cleanup](protocols/mesh_cleanup.md); immutable proxy certificates, full-surface error bounds and native raw/proxy viewing |
| Run/admit bounded observed-depth fusion | [CPU TSDF and native interchange contract](protocols/fusion.md), [component selection](research/observed_fusion.md); pose-bound partial arbitrary-scale geometry; separate native runtime view, no collision |
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
