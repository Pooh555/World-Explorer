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
