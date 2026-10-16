# sample_2 replacement — 2026-10-16 (Asia/Hong_Kong)

Status: BLOCKED on pose registration; local user-requested reset/rerun,
no Q/P/N/E gate change.
Code: `0aba7f6`, initially clean worktree. All commands use repository root.
No source or reconstruction code changes.

The replacement `data/local/sample_2.mp4` has SHA-256
`b257eb77147457b5343d6f0c7581f6a3d7a979f6e16cd4567918bd7846857465`.
`ffprobe -v error -show_entries format=duration:stream=width,height -of json data/local/sample_2.mp4`
exited 0: 1920×1080, 30.746167 seconds.
The deleted source identity was
`63e037536a34e3effff73da370951a9cd517ff77345f85e8dec681b6a74126c2`.

Before deletion, the native user service was inactive, no `we_process` process
was present, and the queue flock was acquired. A checked Python reset verified
that `artifacts/sample_2` was a real directory directly under artifacts, checked
the changed source hash, and removed only that directory: 1,035 files,
4,034,686,507 bytes. Other videos were outside deletion scope.
Receipt: `artifacts/sample_2/logs/reset.json`.

`python3 tools/new_video_job.py data/local/sample_2.mp4` exited 0.
The fresh request retains 3 fps / 120-frame admission, 1280 maximum training
dimension and 7,000 steps; pose CPU threads explicitly set to two, with existing
single-pair CPU matching. No old decode/pose/checkpoint state is reused.

`python3 tools/run_video_jobs.py --service artifacts/sample_2/request.json`
exited 0 under native access. Transient service invocation:
`6b4bfd2ad7ed46838a20a5f33a7dd866`; existing 200% CPU / 7 GiB hard memory cap.
Observed queue RUNNING, coordinator PID 371433; source inspection completed and
decode started with the new source hash. Logs live under
`artifacts/sample_2/logs/`; queue/service receipts under `artifacts/`.

## Terminal inspection

Read-only inspection of `artifacts/queue.json`, `logs/result.json`,
`logs/reconstruction.log`, decoded `result.json` and the retained pose backend
log confirms queue FINISHED / job FAILED, exit 2, elapsed **1,254.709878 s**
(20 min 54.710 s), `scene_published: false`. Inspection/decode completed;
**88 selected frames** were decoded. COLMAP 4.2.1 reported three models with
**3, 10 and 75 registered images**. The largest model covers **85.23%**;
90% admission requires at least **80/88**, leaving it five frames short.
These model counts must not be summed into a connected reconstruction.
The coordinator reports `POSE_DISCONNECTED: Less than 90% of selected frames
registered; recovery is required`.

The incomplete pose stage is retained at
`artifacts/sample_2/poses/stages/.poses-8cbpu9iu/`; its `backend.log` records
initialization failures as well as the final model counts. No fit directory or
scene exists. `logs/validate.log` is absent because automatic scene validation
runs only after successful reconstruction. No training/GPU/scene-quality result
is claimed. This failure describes the replacement source, independently of the
older deleted source’s 57/64 failure.

Next executable recovery analysis: inspect retained sparse-model image
memberships and the verified match graph before selecting any disjoint recovery
attempt. Preserve the database, models and decoded stages; do not rerun the
unchanged solve or relax registration thresholds. No recovery or reconstruction
was launched during this result review.

Documentation checks: `python3 tools/check_docs.py` and `git diff --check`
exit 0 after this update. No code changes; build/tests are not needed for this
read-only result review and documentation update.
