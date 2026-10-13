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

Keep logs for an attempt inside its `logs/` folder. Verification for a scene can
use `logs/validation/`; unrelated future checks may use a clearly named folder
under `artifacts/checks/`. Do not accumulate root-level trial requests, loose
readbacks, archives or several incompatible naming schemes.

On 2026-10-13 the user explicitly requested deletion of **all** old artifacts and
fresh reconstruction, superseding the earlier preservation/archiving cleanup.
Historical validation reports remain in Git as records; their old private raw
artifact locations are no longer available. New runs use the layout above.
