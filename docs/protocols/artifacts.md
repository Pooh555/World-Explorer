# Local artifact layout

Videos stay anywhere inside `data/`. Each video gets one readable folder directly
under `artifacts/`, named after its filename stem. Both directories are ignored
by Git. Build products, installed worker environments and dependency caches
remain outside `artifacts/`.

```text
artifacts/
  README.md
  sample_1/
    source.json
    request.json
    inspection/
    decode/
    poses/
    map/
    fit/
    scene.wexplore/
    logs/
      reconstruction.log
      result.json
```

`source.json` holds the original source path and full SHA-256. `request.json`
contains explicit stage actions, backend paths and limits. Worker-owned stage
hashes, immutable asset filenames and checkpoints keep their required names
inside the relevant stage folder. `scene.wexplore` appears only after successful
publication; a failed solve must not be presented as a completed scene.

For future videos:

```sh
python3 tools/new_video_job.py data/local/room.mp4
build/dev/we_viewer --review-request artifacts/room/request.json
build/dev/we_process reconstruct --request artifacts/room/request.json
```

The helper only writes the request and source record; it neither processes video
nor downloads dependencies. Names use lowercase letters, digits, underscores and
hyphens. The helper refuses an existing folder. For another attempt use
`--name room_retry_2`; it never silently overwrites a prior job. Renamed videos
can share content: compare the full source hashes when deciding whether to reuse
or explicitly reconstruct again.

The existing local development recipe uses 7,000 fit steps, maximum training
image dimension 1280, three sampled frames per second, at most 120 frames, four
CPU threads and a 2 GiB native RGB journal. `--sample-fps` explicitly changes
sampling. Long or large videos fail admission instead of silently truncating the
capture or reducing quality. The normal pipeline checks backend availability,
source identity and current storage. Scale remains arbitrary; geometry/walk is
not provided by this recipe.

When a video is replaced in place, existing source records, requests and cached
stages still belong to the original full SHA-256. Preserve them; do not rewrite
an old `source.json` to make a historical job appear current. Verify source hashes
before queueing requests and use a fresh named attempt for a replacement.
`new_video_job.py` creates the captured-appearance `local_gsplat_v1` recipe, not an
A1 completed-asset pipeline. Freeze inference/holdout/reference roles before any
asset solve; an intake partition proposal alone is not an executable request.

Keep logs for an attempt inside its `logs/` folder. Verification for a scene can
use `logs/validation/`; unrelated future checks may use a clearly named folder
under `artifacts/checks/`. Do not accumulate root-level trial requests, loose
readbacks, archives or several incompatible naming schemes.

On 2026-10-13 the user explicitly requested deletion of **all** old artifacts and
fresh reconstruction, superseding the earlier preservation/archiving cleanup.
Historical validation reports remain in Git as records; their old private raw
artifact locations are no longer available. New runs use the layout above.

## Sequential processing on the local Linux development host

Use one queue for several videos when concurrent jobs exceed laptop resources:

```sh
python3 tools/run_video_jobs.py --recover-stale-locks artifacts/room_a/request.json artifacts/room_b/request.json
```

The queue waits for each coordinator and successful scene validation before
starting the next video. Normal job failure is recorded and the next video still
runs. `artifacts/queue.json` shows RUNNING/PENDING and terminal per-job results;
logs stay inside each video's `logs/`. A kernel-owned `.queue.lock` prevents
a second queue from starting. It does not control separately launched CLI jobs;
do not start parallel reconstructions while the queue is active.

The explicit recovery flag removes a worker `job.lock` only when its recorded PID
is confirmed absent. Malformed locks and live/inaccessible PIDs fail admission.
Requests, cached stages, checkpoints and solver limits are not changed. This local
launcher uses Linux/POSIX signal and file-lock facilities; it does not qualify
other platforms. Detaching it permits the launch command to exit, but cannot
protect it from shutdown, process-tree termination or resource exhaustion.

### Supervised laptop jobs

Use `--service` with `tools/run_video_jobs.py` to run the queue as a transient
Linux user service, `world-explorer-video-jobs.service`. It is independent of
the chat/tool session, uses two CPU cores in aggregate, a 6 GiB soft reclaim
threshold, 7 GiB hard memory cap and 1 GiB swap cap, and is not enabled at boot.
Only one reconstruction runs at a time. The unit is collected on completion;
per-video logs and `artifacts/queue.json` remain.

```sh
systemctl --user status world-explorer-video-jobs.service
systemctl --user stop world-explorer-video-jobs.service
journalctl --user -u world-explorer-video-jobs.service --no-pager
```

The queue records actual stop signals and labels exit-130 attempts CANCELLED.
Previous terminal results move into `logs/history/attempt-NNN/`; they cannot be
mistaken for the currently running attempt. Lock recovery reads the request's
actual mutable worker directories, including custom pose paths, rather than
assuming a folder named poses. Live or unprobeable lock PIDs block the queue.

CPU SIFT matching processes one pair at a time because its dense descriptor
workspaces can exhaust laptop memory when multiplied by matching workers. This
changes concurrency, not frame selection, feature counts, image dimensions,
matching recipe or pose acceptance. The current remaining-video requests use
two extraction/mapping threads and fresh `pose-bounded/` jobs; checked retained
inspection/decode data is adopted. No successful sample_1 fit is restarted.

### Retained pose recovery

`tools/recover_video_poses.py` writes a fresh, disjoint pose job from a retained
failed solve and its verified decode stage. `--seed-images IMAGE IMAGE` remaps a
copied feature/match database from one verified pair; `--initial-model MODEL`
instead continues a copied model from that failed stage against all saved pairs.
The modes are mutually exclusive. Without either flag, retained models are
exported through the normal selection path. All modes retain the existing
connected-model and 90% registration checks; partial results cannot publish a
scene. Record the exact recovery command under the video's `logs/` and use the
global queue flock when running it as a separate Linux service. Keep successful
fits and original stages intact; this helper does not launch fitting.
