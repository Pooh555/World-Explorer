# Artifact cleanup — 2026-10-13, Asia/Hong_Kong

Scope: user-authorized cleanup and semantic naming of local artifacts after
deleting two new videos. Parent commit `4c2e90d`; no reconstruction, compiler,
dependency or acceptance-threshold changes. Working directory: repository root.
M1 remains IN_PROGRESS; this is file preservation and usability evidence.

Artifacts now follow the [layout protocol](../protocols/artifacts.md): video and
run identity, standalone scenes, historical experiments, dated/topic verification,
diagnostics, maintenance and unavailable-source capture archives. Existing hashed
stage/asset names and all internal job records remain unchanged. Historical paths
are resolved using the longest matching prefix in the local migration map;
historical reports retain the commands actually run. Archived absolute-path
requests/scripts are records, not ready-to-run commands after relocation.

The retained source `data/local/sample_1.mp4` still has SHA-256
`51263d9991dcbc2136d0809ecba1a8ba1b261896f3b222b7afba19784a153ef6`.
Both completed results, all checkpoints, successful jobs and old decode pointer
are preserved. The deleted AVI's generated job was already absent at inventory.
The deleted MP4's failed job was checked against its request and exit-2 result,
had no completed fit/scene, and was removed: **795,094,461 logical bytes**
(approximately 758 MiB). Other historical data was organized rather than deleted.

Actual migration: `python3 /tmp/organize_world_artifacts.py`; first attempt stopped
before mutation on an unclassified date-before-suffix name. Corrected classification
then completed: **383 moves, 5,517 retained files/links** checked before/after by
inode, length and nanosecond modification time, or unchanged symbolic-link text.
A final inventory recheck also passed. This is preservation by rename, not a claim
that all historical jobs can be resumed at their new paths. The executed script,
mapping, inventory and result are retained in
`artifacts/maintenance/2026-10-13-layout/`.

Both relocated bundles passed `build/dev/we_process validate SCENE_PATH` (exit 0):

- `artifacts/videos/sample-1-51263d9991dc/scenes/original.wexplore`
- `artifacts/videos/sample-1-51263d9991dc/runs/2026-10-13-fresh-reconstruction/scene.wexplore`

Both also opened with native `build/dev/we_viewer --scene SCENE_PATH --source-view
--max-splats 1000000 --frames 3 --settings OUTPUT_SETTINGS --screenshot OUTPUT_PPM`.
The fresh run additionally used `--review-request HELPER_REQUEST`. Actual argv and
exit codes are in `native-results.json`: both exit 0, original **304,867 splats /
92 frames**, fresh **297,489 splats / 78 frames**, **zero Vulkan validation errors**
on the local RTX 4060 Laptop. Automatic exit waits initial scene/review readiness,
so actual frame counts exceed the minimum. These are local opening/read-only review
checks, not performance, input or other-platform qualification.

`python3 tools/new_video_job.py data/local/sample_1.mp4` generated a unique,
content-keyed request with disjoint outputs. Its normal application review passed
without launching reconstruction. Three invalid sampling values, a path outside
`data/`, and a missing source were rejected (exit 2). The temporary generated run
was removed after verification; its request is retained as evidence only. No
model download or solver rerun occurred. New videos use this helper explicitly;
there is no automatic directory watcher.

Raw checks, request, logs and readbacks:
`artifacts/verification/2026-10-13/artifact-layout/`. Documentation links and
`git diff --check` passed. Current handoff uses relocated scene paths; the
implementation task remains checked retained metadata/storage review.
