# Native reconstruction job command (development)

`we_process reconstruct --request JOB.json` and the viewer's **Continue poses,
fit and open scene** action call the same application coordinator. The viewer can
also start this command with `--reconstruction-request JOB.json`. Processing runs
on its owned background thread; appearance projection/drawing pauses while UI,
presentation and cancellation remain active. Resident scene assets are retained.
The completed scene enters the usual renderer update path after bounded loading.

This command explicitly solves fresh poses or continues a completed pose job.
Schema 2 can inspect metadata automatically or adopt a checked retained
inspection, then decode/solve in owned jobs or adopt completed poses. Successful
stages are preserved; no recipe or completed fit is silently replaced.
The `local_gsplat_v1` recipe is explicitly selected, development only, and does
not substitute for the upstream Q2/Q3 recipe. There are no downloads or remote
fallbacks. An existing bundle and completed fit can be published/viewed without
a Python or CUDA installation.

Schema 1 rejects unknown fields, unsafe overlaps, fractional/oversized limits,
requests over 64 KiB and excessive JSON nesting. Relative filesystem paths
resolve against the request file's directory; a bare `fit_python` executable is
looked up through PATH. The UI has separate pose and reconstruction request fields.

Optional `map_directory` selects the durable pose output; when omitted, it is
the sibling `FIT_DIRECTORY.map`. Optional `decode_stage` explicitly selects the
retained decode stage belonging to this pose job. It never rewrites a stale
`decode.json` pointer. Map, scene and fit outputs must not overlap each other or
the pose/video inputs. Existing requests retain schema 1, but now require the
checked canonical pose and decode inventories in the completed job.

Schema 2 retains the same recipe/fit fields and requires an explicit `pose_action`
and inspection action. For retained poses and metadata:

```json
{
  "pose_action": "adopt_completed",
  "inspection": {
    "action": "adopt_completed",
    "stage": "metadata-job/stages/inspect-<64-hex-stage-key>",
    "completion_sha256": "<64-hex-SHA256-of-completion.json>",
    "minimum_free_bytes": 67108864
  }
}
```

The fragment is added to a full schema-2 reconstruction request. The selected
inspection can belong to a separate metadata job or the retained pose job. No
mutable inspection pointer is followed, no stage search is performed, and no
worker is launched for adoption. This path can reuse a completed scene without
Python, FFmpeg, COLMAP, CUDA or gsplat installed. Schema 1 rejects these new fields
and remains the legacy retained-pose path without a metadata-preflight claim.

For automatic local metadata inspection, replace the inspection object with:

```json
{
  "action": "inspect",
  "job_directory": "room-metadata",
  "worker_script": "../workers/worker.py",
  "python": "python3",
  "minimum_free_bytes": 67108864
}
```

All five fields are required. Paths resolve against the request directory; a bare
Python executable uses PATH. `job_directory` must be fresh/empty or belong to the
same source SHA, worker script/path and explicit Python choice. The native owner
file is published before worker launch; unknown entries, symlinks, corrupt owners
and incompatible identities fail without reseeding. Metadata output must not
overlap capture, poses, map/fit/scene outputs, request, backend, checkpoint or
worker inputs. All writable reconstruction outputs also protect the request file.

A native nonblocking file lock protects ownership and worker execution. The lock
file remains; OS ownership releases on process exit and is not inherited by the
worker. This is cooperative local exclusion, not protection against external
directory mutation or certified power-loss durability. An incomplete owner write
requires diagnosis/fresh output. A surviving Python `job.lock` requires verifying
its recorded process is gone before recovery; native code does not delete it.

The FlatBuffers handshake must advertise video batch support and enough source
bytes/16 MiB output capacity before START is sent. Automatic inspection always
enters a new transport session; the worker reuses compatible committed stages
using its current dependency/configuration identity. No RGB decoding, pose solve
or optimizer is requested. The returned verified `inspect` artifact must be a
bounded `result.json` inside this job's `stages/inspect-<key>`, and its immutable
inventory/source are checked by shared preflight. Mutable job pointers do not
select the returned artifact. Changes to the video, script or returned bytes fail
before map preparation. Cancellation uses acknowledged CANCEL and retains valid
committed stages; a subsequent matching request can restart.

Current free-space checks cover the metadata job plus map/fit/scene outputs before
ownership/START. The worker's existing decoded-frame storage estimate also applies
to inspection; it does not allocate those frames. Existing retained-pose/fit
preparation then runs through the same coordinator and records the inspection
completion dependency in the scene. Retained adoption still requires no backend.

Preflight verifies the exact selected completion digest and complete inventory
(≤16 files, ≤16 MiB, completion/result JSON ≤64 KiB). It binds the parsed metadata
bytes to their inventory hash, then checks original-video size/hash, primary video
dimensions (≤4096² pixels), positive duration ≤1200 seconds, orthogonal display
rotation, positive original rational time base, SDR policy, ignored audio and
arbitrary scale. HDR is rejected pending an explicit calibrated conversion.
Output directories cannot overlap the inspection job or video; existing ancestors
must be directories. No output is created by preflight.

`minimum_free_bytes` is an explicit current-space requirement at **each** map,
fit and scene output volume, in [1, 32 GiB]. It is not a reservation or an estimate
of complete processing storage. Repeated checks on one filesystem do not reserve
three budgets. Selection/fit/output budgeting remains separate. Preflight runs
before map preparation or fitter launch and again before compilation. Failures
preserve the previous scene and any valid map/fit. The scene records
`inspection_completion_sha256`; identical restarts retain that dependency.
Retained metadata verification does not probe current backend capabilities,
certify codec decoding/color conversion or improve timestamp-sampled selection.
New worker stages still require their own capability handshake. Blur/cuts/lens
diagnostics and reference-platform end-to-end qualification remain pending.

For fresh solving, select `pose_action: "solve"`, omit `decode_stage`, and add
both `decode` and `pose_worker`. Example fragment for a full schema-2 request:

```json
{
  "pose_action": "solve",
  "pose_job": "room-poses",
  "decode": {
    "action": "decode",
    "job_directory": "room-decode",
    "worker_script": "../workers/worker.py",
    "python": "python3",
    "sample_fps": 3.0,
    "max_frames": 120,
    "cpu_threads": 4,
    "max_output_bytes": 34359738368
  },
  "pose_worker": {
    "worker_script": "../workers/worker.py",
    "worker_python": "python3",
    "pose_python": "../.venv-worker/bin/python",
    "recipe": "sequential",
    "cpu_threads": 4,
    "journal_bytes": 2147483648
  }
}
```

Decode can instead explicitly adopt an existing inventory with
`{"action":"adopt_completed","stage":"JOB/stages/decode-<key>","completion_sha256":"<digest>"}`.
That selection is the **pose input**, separate from the legacy `decode_stage`
field selecting a retained pose job's domain. Decode/pose configuration is
rejected for `pose_action: adopt_completed`; schema 1 remains unchanged.

Decode uses a separate owned job and immutable `decode-request.json`. Parameters
are explicit: finite sample rate [0.1, 4], 3–120 selected frames, 1–4 CPU threads,
positive output bytes ≤32 GiB. Timestamp sampling defaults to 3 fps. Optional
`decode.frame_selection: "quality_keyframes_candidate_v1"` or
`"quality_keyframes_candidate_v2"` explicitly selects
the [unqualified quality candidate profile](frame_selection.md), requires 2–4
candidates/s and binds the selector helper bytes in owned parameters. Omitting
the field or selecting `timestamp_sampling_v1` keeps the legacy behavior. Source,
selected inspection, worker and parameters bind ownership. START requires video
batch capacity and the requested byte/frame limits. The returned bounded artifact
must belong to that decode job; all inventory bytes, catalog IDs/timestamps and
inspected dimensions/rotation/time base are checked. Changed parameters require
fresh output rather than reseeding old ownership. Existing decoded image hashes
are checked again before scene compilation.

Fresh poses use native `frame_source` leases and the isolated batch estimator,
with an immutable intent at `POSE_JOB.plan/owner.json` and a lifetime writer lock.
This binds video/decode SHA, worker/path/Python choices, recipe, threads and
journal budget (4 MiB–2 GiB). Decode, pose, intent, map, fit and scene directories
must be disjoint and protect request/worker/backend/checkpoint inputs. Existing
unowned pose jobs are not taken over; explicitly adopt them or select fresh output.

Successful native RGB admission writes each immutable PPM before consuming the
lease. Restart replays the same checked selection, reusing only byte-identical
PPMs and exact journal/request bytes; mismatches preserve caller leases/data.
Temporary/uncommitted input or a surviving Python worker lock requires explicit
diagnosis, not deletion. Cooperatively cancelled solver work can restart from its
complete journal and compatible cached stages. Completed solves from the same
owned intent are checked/reused without invoking the solver; their original
recipe/provenance is retained. Input journal/source/frame hashes are verified
before normal durable map preparation and fitting. A different intent, corrupt
input, stale map epoch/revision or different fit-camera solution rejects publication.
The intent lock remains held through fit/scene publication. Old `solve-poses`
defaults to fresh output; only owning callers explicitly enable input replay.

No decoder/solver/optimizer dependency is installed implicitly. `pose_python`
must select the deliberately installed local COLMAP environment. Fresh solving
typically pairs with fit `action: start`; completed fit reuse is still explicitly
selected and must match the new pose solution, not just video/image bytes.

Example continuing poses with a fresh appearance job:

```json
{
  "schema_version": 1,
  "recipe": "local_gsplat_v1",
  "action": "start",
  "video": "../data/local/room.mp4",
  "pose_job": "room-poses",
  "gsplat_source": "../.deps/gsplat",
  "fit_python": "../.venv-train/bin/python",
  "fit_worker_script": "../workers/gsplat/fit.py",
  "fit_directory": "room-fit",
  "scene_output": "room.wexplore",
  "steps": 7000,
  "max_image_dimension": 1280,
  "max_splats": 2000000
}
```

Actions:

- `start`: rejects an existing checkpoint or completed fit. Requires explicit
  backend source, script and Python executable.
- `resume`: same fields plus `checkpoint`, selecting a trusted local full
  optimizer/RNG checkpoint. The worker validates its exact input/recipe identity.
  A completed fit directory is never reopened for optimization. Fresh-solve chaining
  permits the fitter's canonical `FIT_DIRECTORY/checkpoint.pt` inside its writable
  fit directory only for explicit resume. Other overlapping checkpoint locations
  remain protected; a different checkpoint must be outside writable outputs.
  Request/backend/worker aliases still reject even when they name that checkpoint.
- `reuse_completed`: requires `completed_fit_identity`, the 64-character SHA-256
  identity from the selected local fit's `result.json`. Omit backend fields and
  checkpoint. Reuse checks source, recipe settings and training image hashes;
  compilation checks the immutable PLY hash/count and binds that selected identity.
  This explicitly adopts the existing fit; C++ does not independently recompute
  its Python optimizer identity or qualify reconstruction quality.

Before fitting/publication, the coordinator validates the pose completion pointer,
source identity, entire checksummed stage inventory (≤4096 files, ≤32 GiB),
undistorted model presence and path containment. Symlinks, extra uncommitted files
and changed bytes fail. Source and pose inventory are checked again after fitting.
Nonzero worker exits and missing completion manifests cannot publish a scene.

Before fitting, shared native pose preparation converts the retained dataset to
canonical FlatBuffers and publishes it inside a persistent map. It uses operation
1, epoch 0/revision 1, retaining source/decode/pose/optional-journal completion
dependencies and retry receipts. An empty owned map can finish interrupted
preparation; an identical committed map is reused after restart. A different
dependency, correction/new epoch, corrupt manifest or occupied unowned directory
fails without reseeding or resetting. The coordinator holds the writer lock until
scene publication finishes. A cancelled/failed fit leaves valid prepared poses
available, alongside the previous scene.

Fit admission checks the complete registered image inventory and its native
COLMAP camera views against canonical poses, applying Rx(pi) and a relative
`5e-6` tolerance for the trainer's float32 matrices. Thus identical image bytes
cannot attach a fit from a different camera solve. It also reads the hash-selected
undistorted `training/sparse/cameras.bin` through a bounded native interchange
adapter: one SIMPLE_PINHOLE/PINHOLE record, at most 64 bytes, positive finite
focals/finite principal point, original dimensions 11–8192 with ≤4096² pixels.
Capture OPENCV calibration is not substituted for undistorted training calibration.
Fit original/output dimensions must match that camera and the explicit resize
recipe (no upsampling; nearest-even integer dimensions). Half-pixel backend
centers scale independently in x/y; the adapter exposes integer centers. All nine
intrinsic entries must match within `8 * float_epsilon * max(1, abs(expected))`
to account for float32 storage/multiplication. Malformed models, changed hashes,
different dimensions/intrinsics and unsupported multi-camera profiles fail before
scene publication. Source, decode and pose
dependencies are checked again before compilation. The scene records the
canonical asset/pose-completion hashes and map epoch/revision; appearance chunks
conservatively depend on every registered source frame. This initial-fit path
requires revision 1; it does not silently reuse a fit after pose corrections.

Cancellation interrupts checksum reads and worker processes. The previous scene
manifest survives failures before publication. Final immutable asset writing and
atomic manifest replacement form a commit boundary; a later cancellation cannot
undo a committed bundle. PLY decoding and final bundle writing are currently
bounded but not interruptible internally. If the viewer budget rejects the
published scene, the error includes its output path and keeps the previous scene.
The startup request mode returns failure instead of treating an empty viewer as
success. No scene is silently trimmed to fit a smaller renderer budget.

Shared native subprocess calls use verified FlatBuffers 2.0 START/CANCEL commands
and events described in [worker.md](worker.md). Direct Python invocation retains
JSONL diagnostics. Fresh chaining has contract checks and [one real local owned run with checkpoint
resume and native viewer handoff](../validation/2026-10-13-fresh-chain.md).
Reference hardware, prescribed fixture/native-platform qualification and complete
M1 gates remain required. Worker source changes retain
the original recipe/checkpoint identity checks; completed fits can still be
explicitly reused, but older optimizer checkpoints are not silently upgraded.

## Reconstruction progress observers

The synchronous shared coordinator accepts an optional project-owned typed
progress callback alongside diagnostic logging. Both run on its calling job
thread. Native stage boundaries also apply to retained adoption/reuse, which must
not claim optimizer work. Inspection, decode, batch-pose and fit launchers forward
verified worker events through optional synchronous observers; unframed diagnostic
processes provide logs only. Wire format and worker/owner identities are unchanged.
The application checks stage correspondence, finite nonnegative event duration and
bounded optional fitting-step metadata before exposing UI values. The viewer's
[status and terminal handoff contract](viewer.md) owns presentation/lifetime.

## Reviewed request bytes

`read_reconstruction_job(request, expected_sha256)` optionally checks a lowercase
SHA-256 against the exact bounded JSON bytes it parses. Malformed digests fail
before parsing; a mismatch fails before any worker or output publication. An empty
digest preserves existing CLI/startup behavior. Schema versions, path-overlap
protections, limits, recipes and immutable worker ownership are unchanged.
The [viewer request review](viewer.md) pins a canonical request path and digest,
checks explicitly selected retained metadata/source identity through shared
preflight and reports current output storage. Automatic inspection stays pending.
The shared coordinator rechecks inputs/storage and gates backend capability at
execution; review never starts workers or substitutes a cached stage.
