# Retained pose continuation — 2026-10-13, Asia/Hong_Kong

Scope/status: sample_2 investigation after the bounded matching repair. Parents
`6531e23` / `48c89e0`; M1 IN_PROGRESS, local Linux development host. Cwd: repository root.
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
as keypoints; the close white-panel sequence has sparse visual texture. The
mapper's unchanged minimum of 15 verified matches leaves **57 connected images**,
independently reported by its database cache. Images 49–54 and 59 are isolated.
Since 90% of 64 requires **58** registered images, this saved match graph cannot
pass even if every connected image registers. Coverage/bridge analysis is saved
under `logs/service-recovery/`.

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

Actual continuation used retained `sparse/2` (21 frames), output
`artifacts/sample_2/pose-continued/`. Exact service argv is
`logs/continued-recovery-command.json`; its global queue flock excludes concurrent
jobs. Transient unit `world-explorer-pose-recovery.service` has the same two-core,
6 GiB soft / 7 GiB hard / 1 GiB swap bounds. Mapping took **8.964 minutes**,
retaining separate 21/31-frame models in the backend log rather than joining
them. The service exited 1 after **9 min 52.753 s**, reported peak **826.8 MiB**.
Both services are now inactive/dead/MainPID=0. This is not performance qualification.

Continuation exposed a helper layout bug: COLMAP writes resumed models at the
output root, while worker admission expects numbered candidate directories.
The helper now explicitly serializes **all returned models** into that layout;
its temporary backend output is cleaned after serialization. The actual saved
21-frame root model was serialized using the corrected writer to
`mapping/normalized-sparse/0`, then passed through `worker.Job.poses` against the
verified 64-frame decode. It exported/read normally and was correctly rejected
as **POSE_DISCONNECTED / less than 90% registered**. The standalone diagnostic
command exited 0 because that expected rejection was asserted; it is not a
reconstruction pass. `logs/coverage-check/expected-rejection.json` records the
outcome. The lost second returned model was not fabricated; the original
31-frame model remains in the original failed stage.

## Checks / remaining

`python3 -m unittest discover -s tests/integration -p 'worker*_test.py' -v`:
exit 0, **24/24**. Six new synthetic admission checks cover retained database
preservation, nonsequential image IDs, unchanged mapper options, insufficient /
unknown / repeated seeds, missing decoded images, continuation and empty mapping.
The writer fixture verifies both returned candidates survive normalization;
database filenames containing spaces/# exercise proper read-only URI encoding.
These checks do not certify reconstruction quality. Raw test log:
`artifacts/sample_2/logs/recovery-tests.log`; bridge analysis, mapper and service
logs remain inside the same video folder.

Original database SHA-256 remains identical after recovery. No full sample_2
scene can be admitted from these matches; no fit was launched. Sample_3's failed
stage remains retained, and neither video is repeatedly retried unchanged.
Better overlapping textured footage or a separately qualified solver/calibration
approach is required to revisit their pose failures.
Final `build/dev/we_process validate artifacts/sample_1/scene.wexplore` exited 0,
scene 149348248299280656 / revision 1 / one chunk; raw log is
`artifacts/sample_1/logs/orientation/final-validate.log`. An initial probe with an
unsupported `--scene` argument failed before validation; the documented
positional command above corrected it. Final documentation/diff checks passed
(220 local targets in 68 files). No native C++ changed in this continuation;
the prior 116/116 profiles retain their original scope.
Physical gravity, geometry/walking, reference performance and full Q/P/N/E gates
remain unqualified. No claim that either incomplete video has a usable scene.
