# Documentation map

Read by task; do not load the entire directory for routine work.

| Need | Source |
|---|---|
| Resume development | [Progress](progress.md) → [requirements](requirements.md) → relevant code/evidence |
| Agent execution rules | [Runbook](execution_prompt.md), repository [AGENTS.md](../AGENTS.md) |
| Product, thresholds, architecture, roadmap | [Technical plan](technical_plan.md); controlling specification |
| Write/update documentation | [Documentation protocol](protocols/documentation.md) |
| Implement an interface | [Capture](protocols/capture.md), [poses](protocols/poses.md), [maps](protocols/maps.md), [scenes](protocols/scene.md), [workers](protocols/worker.md), [jobs](protocols/jobs.md), [viewer settings/camera/handoff](protocols/viewer.md) |
| Organize outputs / add a local video | [Artifact layout and request helper](protocols/artifacts.md) |
| Audit local worker/metric identities | [Installed identity receipts](protocols/worker_identity.md), [isolated CPU metric profile](../workers/metrics/README.md) |
| Check selected dependency provenance gaps | [Source/archive/notice inventory](protocols/dependency_sources.md) |
| Capture/check selected Linux ELF dependencies | [Runtime dependency receipts](protocols/runtime_dependencies.md) |
| Check actual backend imports / owned viewer mappings | [Observed Linux mappings](protocols/backend_bindings.md) |
| Find historical results | [Validation index](validation/README.md); reports preserve their original scope |
| Understand a consequential choice | [Decision records](adr/) |
| Build/use the application | Repository [README](../README.md) |
| Measure CPU contract coverage | [Coverage protocol](protocols/coverage.md), reviewed [scope](../tools/coverage_scope.json) |
| Run bounded synthetic parser/update fuzzing | [Fuzzing protocol](protocols/fuzzing.md), isolated Clang sanitizer preset |

Current state belongs in progress/requirements; historical measurements belong in
validation reports. Protocols describe implemented behavior, not planned capability.
