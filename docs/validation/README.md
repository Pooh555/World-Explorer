# Validation index

Historical reports retain their original code/profile scope; later success does
not upgrade earlier qualification. Commands/logs, unique identities, failures and
limitations stay in each report. [Shared command definitions](checks-2026-10-12.md)
are recipes, not passes. All private artifacts remain local/ignored.

| Slice | Report: observed behavior / commands |
|---|---|
| Foundation | [2026-10-11](2026-10-11-foundation.md): build, GPU overlap/replace/delete/resize; failed original capture and successful replacement poses |
| Real appearance | [Capture](2026-10-11-capture.md): fit failures, cancellation/resume, scene/viewer; [manifest](2026-10-11-capture.json) |
| Shared continuation | [Coordinator](2026-10-11-coordinator.md): fit start/resume/reuse, GUI/GPU pause, shutdown; [manifest](2026-10-11-coordinator.json) |
| Windows stage flush | [Flush repair](2026-10-12-windows-flush.md): supplied Windows 77/78; writable fsync, nine Python checks, preservation regression; hosted rerun pending |
| Current native CPU repair | [Runtime/paths](2026-10-12-native-runtime.md): supplied macOS 78/79, Windows 74/78; alias assertion and missing FFmpeg repaired locally; hosted rerun pending |
| CPU CI repair | [Initial](2026-10-11-ci.md), [follow-up](2026-10-11-ci-followup.md): compiler/library/OpenSSL, byte JSON and Windows path assumptions; hosted vs local scope |
| Worker events | [Wire](2026-10-11-worker-wire.md): pinned FlatBuffers bindings, typed events; historical argv request transport |
| Native frames | [Replay](2026-10-11-frame-replay.md): 63 RGB frames, seek/reset/EOF, hashes |
| Retained poses | [Replay](2026-10-12-pose-replay.md): OPENCV import, capacity-one immutable map |
| Fresh batch poses | [Batch](2026-10-12-batch-poses.md): RGB admission, sequential failure, exhaustive-guided recovery; `replay-poses` |
| Canonical poses | [Assets](2026-10-12-pose-assets.md): `export-poses`/`inspect-poses`, independent roundtrip |
| Durable maps | [Persistence](2026-10-12-persistent-maps.md): `init-map`/`import-map`/`inspect-map`, writer ownership, restart/partial-write recovery |
| Pose → fit | [Preparation](2026-10-12-pose-preparation.md): `prepare-poses`, original fit reuse, distinct recovery map, dependency rejection/payload parity |
| Worker control | [Protocol 2.0](2026-10-12-worker-control.md): START/CANCEL, retry/backpressure/process cleanup, real metadata cancellation |
| Retained preflight | [Video](2026-10-12-video-preflight.md): metadata adoption, output storage/overlap, libc++ sentinel, preserved manifests |
| Owned inspection | [Automatic](2026-10-12-automatic-inspection.md): fresh/cache/cancel, ownership guards, original scene parity |
| Documentation | [Consolidation](2026-10-12-documentation.md): evidence-preservation audit, local links, workflow lint; no new software qualification |
| Fit calibration | [Admission](2026-10-12-fit-calibration.md): bounded undistorted camera, intrinsic/resize checks, real original/recovery/journal rejects, preserved payload/native viewer; 84 tests/four profiles |
| Fresh stage chain | [Chaining](2026-10-12-stage-chain.md): 79 checks/six profiles, synthetic full chain; real 63-frame decode/cache/pre-solver cancellation |

For current status and next work read [progress](../progress.md) and
[requirements](../requirements.md), not a historical report's “remaining” section.
