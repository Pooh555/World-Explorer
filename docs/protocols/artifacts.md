# Local artifact layout

Generated data remains local and ignored under `artifacts/`. Source videos belong
anywhere inside `data/`; filenames are user-owned and are never renamed by the
job helper. Dependency environments/caches and build products keep their existing
locations outside this directory.

| Directory | Purpose |
|---|---|
| `videos/<video-id>/runs/<run-id>/` | One reconstruction attempt: request, source identity, inspection, decode, poses, map, fit, scene and logs |
| `videos/<video-id>/scenes/` | Retained standalone scenes, such as `original.wexplore` |
| `videos/<video-id>/archive/` | Historical experiments and requests for that video |
| `verification/YYYY-MM-DD/<topic>/` | Dated checks, readbacks and profiling evidence |
| `verification/legacy/<topic>/` | Older evidence whose filenames or original grouping lack a reliable date |
| `diagnostics/<topic>/` | Synthetic fixtures and worker diagnostics |
| `maintenance/<topic>/` | Local inventories, environment probes and migrations |
| `archive/<capture-id>/` | Historical captures whose source is no longer available |

Video IDs use a lowercase hyphenated source stem plus the first 12 SHA-256
characters, for example `sample-1-51263d9991dc`. Full hashes are retained in source
records. New run IDs use UTC `YYYY-MM-DDTHHMMSSffffffZ-reconstruction`; existing
historical runs retain their known date and semantic purpose. Each attempt gets
disjoint outputs; new requests never overwrite another run. Source renaming can
change the readable stem; compare full hashes before deciding to reconstruct a
renamed input again.

For a future video:

```sh
python3 tools/new_video_job.py data/local/room.mp4
```

This prints the new request path without starting processing or downloading
dependencies. Review it using `build/dev/we_viewer --review-request REQUEST_PATH`
or execute it with `build/dev/we_process reconstruct --request REQUEST_PATH`.
The helper selects the existing local development recipe: 7,000 steps, 1280
maximum training image dimension, three sampled frames per second, at most 120
frames, four CPU threads and a 2 GiB native RGB journal. `--sample-fps` explicitly
changes sampling. Long or large videos can exceed these limits; processing fails
instead of truncating the capture or silently reducing quality. Worker/backend
availability and source/storage admission are checked by the normal pipeline.

Keep completed scenes, checkpoints and their evidence. Remove failed generated
jobs for deleted videos when cleanup is authorized. Immutable stage directories,
asset filenames, checksums and job metadata are not renamed or edited internally.
Historical requests and scripts may contain their original absolute paths and
are archived records, not ready-to-run commands after relocation. Generate a
fresh request for new processing; standalone scene bundles remain relocatable.

The 2026-10-13 migration is recorded locally in
`artifacts/maintenance/2026-10-13-layout/migration.json`. Resolve historical
`artifacts/...` locations by replacing the longest matching old prefix with its
mapped destination. Historical reports retain their actual original commands.
Current handoff commands use the new paths.
