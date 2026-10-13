# Fresh three-video reset — 2026-10-13, Asia/Hong_Kong

Scope/status: user explicitly authorized removing **all** old artifacts and fresh
reconstruction of all three videos, including the renamed first input. Parent
commit `9296320`; helper/layout documentation changed, reconstruction implementation
unchanged. M1 remains IN_PROGRESS; no new gate pass is claimed.
Working directory: repository root.

Deletion: confirmed no `we_process` or local `python` reconstruction was active
using native `ps -C we_process -C python -C python3 -o pid,ppid,etime,comm`.
The initial native process check timed out during automatic permission review;
one simpler retry succeeded. Removed the actual `artifacts/` directory with
Python `shutil.rmtree` after checking the exact repository and non-symlink root.
**5,693 files / 23,260,101,885 logical bytes**
were removed, including old successful fits/checkpoints/scenes and raw validation.
Git history, source videos, build products and installed dependency environments
were preserved. Historical reports remain historical; their old private raw
locations and migration map are unavailable after this explicitly requested reset.

Only two videos were initially present. Asked for the third while preparing the
first two; user confirmed `sample_3.mp4` had finished loading, then it was probed
and admitted to its own fresh request.

Actual requests generated with:

```sh
python3 tools/new_video_job.py data/local/sample_1.mp4
python3 tools/new_video_job.py data/local/sample_2.mp4
python3 tools/new_video_job.py data/local/sample_3.mp4
```

Each writes `artifacts/sample_N/request.json` and `source.json`. Sampling remains
3 FPS, cap 120 frames, CPU threads 4, native RGB journal 2 GiB; local exhaustive
COLMAP and 7,000-step gsplat recipe, maximum training image dimension 1280 and
transient splat limit 2 million. No truncation, source alteration, model download
or remote fallback. Existing output directories are refused; future retries use
an explicit `--name`.

All three supervisors actually launched with native device access:
`python3 /tmp/world-explorer-run-video.py artifacts/sample_N/request.json`.
They invoke `build/dev/we_process reconstruct --request REQUEST` as an argv array.
Each exact supervisor is retained in its `logs/run.py`, command identity in
`logs/command.json`, worker events in `logs/reconstruction.log`, terminal result
in `logs/result.json`, and successful bundle checks in `logs/validate.log`.
At this checkpoint all three runs are active; final results are pending.

Remaining: inspect the existing processes and terminal results, validate/open
successful full scenes, record failures without accepting partial models. No
fresh scene is yet claimed as verified. `python3 tools/check_docs.py` passed (209 local targets / 64 files);
`git diff --check` passed. Five helper boundary checks rejected existing output,
NaN sampling, a traversal name, a source outside data, and a missing source
(exit 2). Request creation for all three real sources passed.

## Interruption recovery

The user reported a crashed/cut-off session. Native process inspection confirmed
all three original coordinators/workers were gone. Their logs recorded CANCEL;
completed inspection and selected-image decode stages remained available. No
Gaussian fitting or scene publication had occurred.

Restarted the same checked requests with detached supervisors (`start_new_session`,
stdin disconnected, stdout/stderr local files, SIGHUP ignored). The exact supervisor
for each video is `logs/resume-supervisor.py`; actual command/PID/attempt identity
is `logs/resume-command.json`. Reconstruction appends to the original processing
log. Successful publication is followed by normal CLI bundle validation; terminal
status is atomically written to `logs/result.json`. These processes survive the
launch command exiting; native process checks confirmed that behavior.

Sample 2's first restart failed admission because `poses/job.lock` retained PID
487794. Native `ps -p 487794 -o pid=,comm=` confirmed no such process (exit 1, empty
output); removed only that stale lock, retained the failure in
`logs/lock-recovery-result.json`, and relaunched its same request. Samples 1/3
required no lock deletion. Cache events confirm the completed inspection and
selected-image decode stages were reused. All three resumed runs are active at
this checkpoint; completion/viewing remains pending. No new deletion/reset,
worker changes, solver threshold changes or implicit downloads occurred.

## Sequential launch after another crash

The user requested sequential processing after another crash. Native process
inspection confirmed no prior coordinators/workers remained. All three logs showed
CANCEL and no fitting/published scene. A stale sample-2 pose lock remained; it is
handled only when its job reaches the queue and the recorded PID is proven absent.

Added `tools/run_video_jobs.py`: a local Linux/POSIX queue with an exclusive
kernel file lock, synchronous coordinator wait, successful bundle validation,
atomic status/results and per-video appended logs. Five earlier request-helper
checks remain unchanged. A separate **synthetic launcher fixture**, outside real
artifacts, exercised two mock processes with a filesystem overlap detector:
first failed, second succeeded, exact start/end order showed no overlap, the
missing-PID stale lock was recovered, and validation status was recorded. This
checks scheduling only, not reconstruction. Python compile/help checks passed.

Actual detached argv: `python3 tools/run_video_jobs.py --recover-stale-locks
artifacts/sample_1/request.json artifacts/sample_2/request.json
artifacts/sample_3/request.json`, launched through an argument array with local
`artifacts/queue.log` output. Supervisor PID 501577; native `ps` confirmed exactly
one coordinator, PID 501578 for sample_1, after the launch command exited.
`artifacts/queue.json` recorded sample_1 RUNNING, sample_2/sample_3 PENDING. Cache
events confirm retained metadata/decode reuse. No change to source data, recipes,
CPU threads or quality thresholds.

Completion remains pending. Serial scheduling reduces overlapping resource demand;
the observed crashes do not establish an OOM cause. Detachment cannot protect
against system shutdown or externally terminated process trees. Current next
step is inspect the running queue and terminal outputs before any additional
launch, then open successfully validated full scenes.

## Terminal verification requested by user

Read `artifacts/queue.json`, each processing/terminal log, and searched all three
video directories for published `manifest.json` and fit `result.json`. Queue status
is CANCELLED. Sample 1 exited 2 after 2,132.705 s: POSE_DISCONNECTED, two models;
no fitting/publication. Sample 2 exited 130 after 190.653 s during pose solving;
its log records accepted CANCEL. Sample 3 remained PENDING in this sequential
queue; earlier parallel attempts record cancellation. All three have **no scene
manifest and no fit result**. Native `ps -C we_process -C python -C python3`
confirmed no reconstruction workers remain. The observed cancellations do not
establish an OOM or native crash cause. No restart was initiated during this
read-only verification.

Once a full reconstruction succeeds, each view command is
`build/dev/we_viewer --scene artifacts/sample_N/scene.wexplore --source-view
--max-splats 2000000`. These are future commands, not verified current scene
opens; all scene paths are currently absent. Old completed scenes were removed
under the user's explicit reset instruction.
