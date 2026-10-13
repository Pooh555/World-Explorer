# Retained pose continuation — 2026-10-13, Asia/Hong_Kong

Scope/status: sample_2 investigation after the bounded matching repair. Parent
`6531e23`; M1 IN_PROGRESS, local Linux development host. Cwd: repository root.
No frame, feature, resolution, registration or triangulation thresholds changed.
Sample_1's completed fit and orientation correction remain preserved; see
[native orientation evidence](2026-10-13-service-orientation.md).

## Completed bounded solve

The sequential service completed all **2,016 matching pairs** for 64 images;
sample_2 exited **2** after **2,331.295 s**, with POSE_DISCONNECTED rather than a
crash. Retained models cover 31, 15 and 21 frames. The 15-frame model is a subset
of the 21-frame model, but the 31-frame model is separate: union **52/64**. The
strict-superset selection repair correctly rejects these results. No scene was
published. The queue's subsequent sample_3 attempt also exited 2 after
497.460 s with POSE_DISCONNECTED; the unit was already inactive when inspected
for recovery. Neither terminal result is an unexplained process crash.

The completed database has 676,024 keypoints. Original database SHA-256:
`c3205c33418fa710f49130d5aee704629faf76b4eaaa4dca56f42d992e5b2515`.
It remains at `artifacts/sample_2/pose-bounded/stages/.poses-6irgr28x/database.db`.
Cross-model pairs have 965 (31/32), 120 (2/42), 99 (3/42) and 79 (1/42) inliers.
The first three are classified PLANAR_OR_PANORAMIC by pinned pycolmap 4.2.1;
matches alone therefore do not establish a usable 3D bridge. Four unregistered
frames have fewer than the mapper's default 30 absolute-pose inliers available
as keypoints; the close white-panel sequence has sparse visual texture.

## Actual recovery and safeguards

Fresh `pose-seeded/` initialization from 31/32 was rejected by COLMAP as an
unsuitable initial pair, producing no model. Normal pose admission then rejected
it as INSUFFICIENT_PARALLAX; it did not become a scene. The helper now stops
immediately on empty mapping before repeated journal conversion.

`tools/recover_video_poses.py --initial-model` continues a copied retained model
against the completed database, instead of repeating feature extraction/matching.
`--seed-images` is an alternative initialization, requiring the backend's existing
verified-inlier minimum. Both modes use the pinned mapper with the existing
thread/seed settings and the normal transactional journal/export/coverage checks.
No cross-model transforms or fabricated/interpolated poses are introduced.
Input decode completion, database hash, model inventories and original paths are
recorded locally; originals are checked after successful recovery. Cancellation
uses the existing worker cancellation handling.

Actual continuation uses retained `sparse/2` (21 frames), output
`artifacts/sample_2/pose-continued/`. Exact service argv is
`logs/continued-recovery-command.json`; its global queue flock excludes concurrent
jobs. Transient unit `world-explorer-pose-recovery.service` has the same two-core,
6 GiB soft / 7 GiB hard / 1 GiB swap bounds. Early observed peak was **826,994,688
bytes**; this partial observation does not qualify worst-case memory.

## Checks / remaining

`python3 -m unittest discover -s tests/integration -p 'worker*_test.py' -v`:
exit 0, **24/24**. Six new synthetic admission checks cover retained database
preservation, nonsequential image IDs, unchanged mapper options, insufficient /
unknown / repeated seeds, missing decoded images, continuation and empty mapping.
These checks do not certify reconstruction quality. Raw test log:
`artifacts/sample_2/logs/recovery-tests.log`; bridge analysis, mapper and service
logs remain inside the same video folder.

Continuation is running at this checkpoint; full pose coverage, fitting and scene
validation remain pending. Sample_3's failed stage remains retained during this
single recovery.
Physical gravity, geometry/walking, reference performance and full Q/P/N/E gates
remain unqualified. No claim that either incomplete video has a usable scene.
