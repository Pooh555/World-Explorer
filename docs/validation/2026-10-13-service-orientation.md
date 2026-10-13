# Laptop job supervision and viewing orientation — 2026-10-13, Asia/Hong_Kong

Scope/status: user-requested sample_2 crash recovery and sample_1 tilt correction.
Parent `5f776a8`; M1 IN_PROGRESS, native Linux development host only. Cwd:
repository root. No acceptance, frame-selection, feature-count, image-resolution
or geometry/gravity threshold changes. Source data stays local/ignored.

## Saved sample_1 result and orientation

The prior recovery actually completed **7,000 steps / 295,952 splats**; normal
publication and validation exited 0. Original fit identity
`8c9c8efde20b1d76a5d95733b37abd091c41ca9d6b2ed9ba881a359dd8ef2758`,
PLY SHA `02129770c7b499100396bc79409a7e702d58a7f510d9d5de1b2e59913a24faed`.
Its native viewer also exited 0 with 295,952 resident splats and zero Vulkan errors.
These outcomes occurred before the preceding chat interruption, despite stale
handoff text still describing fitting as pending.

The first capture-up vector is `[0.03424476, 0.98214108, -0.18500316]`, **10.845°**
from arbitrary canonical +Y. The old spectator reset discarded roll relative to
+Y. Schema-2 source inspection now preserves capture orientation as an explicit
viewing reference. Source reset / Fit / bookmarks / movement retain it; default
loading without Source view still uses +Y world-up. Legacy schema 1 is supported.
The [decision record](../adr/009-capture-view-up.md) distinguishes this opt-in
presentation alignment from measured gravity, floor alignment or pose correction.

Actual `build/dev/we_process reconstruct --request
artifacts/sample_1/request-view.json` exited 0 using explicit completed-fit reuse:
no solve/optimization, normal atomic publication to the same `scene.wexplore`.
**All 18 fit files and all three prior immutable scene assets are unchanged** by
SHA-256; only manifest/provenance viewing metadata changed. The original pose-job
preservation audit previously checked all 204 files unchanged.

Native before/after commands are stored in `logs/orientation/*-viewer-command.json`:
`build/dev/we_viewer --scene artifacts/sample_1/scene.wexplore --source-view
--max-splats 1000000 --frames 3 --settings SETTINGS --screenshot READBACK`.
After view: exit 0, **295,952 splats, 59 frames, zero Vulkan errors**, owned buffer
allocation 283,293,856 bytes. Inspected readback shows corrected upright columns
and the capture-orientation/gravity-unmeasured label. Minimum-frame exit waits
scene readiness; this is not presented-FPS, input or performance qualification.

## Cancellation and memory evidence

Sample_2's latest original queue exit was **130**, accepted CANCEL, queue CANCELLED.
Native process inspection found no surviving reconstruction. This establishes
cancellation; it does not establish the original trigger or an OOM kill.

Detached subprocesses were insufficient for stable session-independent control.
A transient **user systemd service**, separate from the tool's execution group,
now owns the sequential queue. It is not enabled at boot; it is collected after
completion. CPU quota is two cores; final soft memory threshold **6 GiB**, hard
cap **7 GiB**, swap cap **1 GiB**. Service status/counters are real systemd/cgroup
probes, not inferred from a launcher exit. Actual stop signals are recorded.

The initial service attempt exposed a hard-coded stale-lock recovery bug: the
request used `pose-recovery`, while the launcher checked `poses`. Corrected to
read actual mutable outputs from bounded request bytes; live/unprobeable PIDs
block admission, retained inputs are excluded, old results are archived under
`logs/history/attempt-NNN`. Its recorded stale PID 513683 was absent and removed.

SIFT selected **64 frames / 676,024 keypoints**, up to **20,311 per frame**.
Concurrent dense distance workspaces grew anonymous memory beyond the 5 GiB
initial soft limit: high-pressure count 31,999, zero OOM/max events. Two-thread
matching still approached **7.264 GB current** with high-pressure count 105,675;
zero OOM/max events. Controlled stops preserved these attempts. The soft threshold
was raised without raising the hard cap; final CPU matching processes **one pair
at a time**, extraction/mapping budget two threads. No features or frames are
removed. New directories avoid rewriting immutable worker identities.

Final actually launched command:
`python3 tools/run_video_jobs.py --service --recover-stale-locks
artifacts/sample_2/request-bounded.json artifacts/sample_3/request-bounded.json`.
Unit `world-explorer-video-jobs.service`, supervisor PID **541759**, cgroup scoped
to the named user service. At this checkpoint sample_2 is active, sample_3 pending.
One-pair matching has written real matches; observed current memory **1.205 GB**,
peak **6.443 GB**, 2,293 high-pressure events and **zero OOM/max events**. This
partial observation does not certify all workloads or terminal reconstruction.

## Checks and remaining work

`cmake --build --preset dev -j 4`, `ctest --preset dev`: exit 0, **116/116**.
`cmake --build build/clang-viewer-cxx23 -j 4`, corresponding CTest: exit 0,
**116/116**, explicit C++23. Worker integration: **18/18** Python checks, including
three synthetic queue boundary fixtures (actual custom output paths, no overlap,
live-lock blocking, result history); these do not simulate successful scenes.
Clang-format/tidy **23.1.1** checks passed on touched first-party C++; no warning
policy changed. Documentation/boundary/diff checks retain actual command logs.

Raw evidence: `artifacts/sample_1/logs/orientation/` and
`artifacts/sample_2/logs/service-recovery/`; exact queue argv is
`artifacts/queue-service.json`. An attempted service restart before the prior stop
completed was rejected as already loaded; no duplicate launched. Retried only
once the unit was confirmed inactive/dead/MainPID=0. Controlled resource-recovery
stops are distinct from the user's earlier unexplained cancellation.

Remaining: inspect the running bounded service and sample_2/3 terminal results;
validate/view successful full scenes. Sample_1 is usable with `--source-view`;
physical gravity/scale/geometry/walking and broader native/profile gates remain
unverified. No claim that sample_2/3 have completed at this checkpoint.
