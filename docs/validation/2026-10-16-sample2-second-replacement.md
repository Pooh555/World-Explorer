# Second sample_2 replacement — 2026-10-16 (Asia/Hong_Kong)

Status: BLOCKED on pose registration; user-requested local reset/rerun.
No Q/P/N/E gate change. Cwd: repository root. Existing documentation edits
were preserved; no source code, recipe thresholds or worker environments changed.

New source `data/local/sample_2.mp4` SHA-256:
`10a6405980025eadfbe12e4c68d86bbab8d41af24e0fa48f2d12ca829f0e1de3`.
Previous source SHA-256:
`b257eb77147457b5343d6f0c7581f6a3d7a979f6e16cd4567918bd7846857465`.
The previous [failure report](2026-10-16-sample2-replacement.md) is historical;
its raw stages were deleted by this explicit reset request.

Native preflight: `systemctl --user is-active world-explorer-video-jobs.service`
reported inactive; `pgrep -x we_process` found no coordinator. A checked Python
reset acquired the queue flock, verified a changed source hash and a real direct
child directory, and deleted only `artifacts/sample_2`: 326 files / 813,123,544
bytes. Receipt: `artifacts/sample_2/logs/reset.json`. Other videos are outside scope.

`python3 tools/new_video_job.py data/local/sample_2.mp4` exited 0. The fresh
request retains 3 fps, 120-frame admission, maximum training dimension 1280,
7,000 fit steps and two pose CPU threads. No old stage/checkpoint is reused.

`python3 tools/run_video_jobs.py --service artifacts/sample_2/request.json`
exited 0 with native access; invocation `4045ee1ebebd47978c70679f1c9953ee`.
Existing supervision caps remain 200% CPU / 7 GiB memory. Queue inspection
confirmed RUNNING, coordinator PID 380789; reconstruction log confirms the new
source hash, successful inspection (1920×1080) and decode start.
Raw state: `artifacts/queue.json`, `artifacts/sample_2/logs/reconstruction.log`.

## Terminal inspection

Read-only inspection of `artifacts/queue.json`, `logs/result.json`,
`logs/reconstruction.log` and the retained pose `backend.log` confirms queue
FINISHED / job FAILED, exit 2, elapsed **1,137.472573 s** (18 min 57.473 s),
`scene_published: false`. Inspection/decode completed; parsing the decoded
stage's `result.json` confirms **90 selected frames**. Source SHA-256 was
rechecked against the current video and matches the identity above.

COLMAP 4.2.1 reports two models with **4 and 79 registered images**; independent
read-only inspection of each sparse `images.bin` count agrees. The largest
connected model covers **87.78%**, below the unchanged 90% requirement of
**81/90**: two additional registered frames are needed. Separate model counts
cannot be summed into a connected reconstruction. The coordinator reports
`POSE_DISCONNECTED: Less than 90% of selected frames registered; recovery is required`.

Failed stage: `artifacts/sample_2/poses/stages/.poses-1_k2am3l/`, including
`backend.log`, database and sparse models. Neither `fit/` nor `scene.wexplore/`
exists; `logs/validate.log` is absent because scene validation follows successful
reconstruction. No training, scene-quality or GPU result is claimed.

Next executable recovery analysis: inspect model image memberships and the
verified match graph before choosing a disjoint recovery attempt. Preserve
failed/decoded stages; do not rerun the unchanged solve or relax thresholds.
This result review did not launch processing or change artifacts.

Documentation checks: `python3 tools/check_docs.py` and `git diff --check`
exit 0 after the terminal-result update. No code changes; build/tests are not
needed for this read-only inspection and documentation update.
