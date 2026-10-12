# Native reconstruction job command (development)

`we_process reconstruct --request JOB.json` and the viewer's **Continue poses,
fit and open scene** action call the same application coordinator. The viewer can
also start this command with `--reconstruction-request JOB.json`. Processing runs
on its owned background thread; appearance projection/drawing pauses while UI,
presentation and cancellation remain active. Resident scene assets are retained.
The completed scene enters the usual renderer update path after bounded loading.

This command continues a completed pose job. It does not restart successful
poses. Use the existing pose-worker request (`stop_after: "poses"`) to produce
that job first. Schema 2 adds checked retained metadata preflight below; automatic
inspection/solve chaining remains M1 work.
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

Schema 2 retains the same recipe/fit fields and requires `pose_action` to be
`adopt_completed`, plus this explicit inspection selection:

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
diagnostics, automatic inspection and fresh pose chaining remain pending.

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
  A completed fit directory is never reopened for optimization.
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
cannot attach a fit from a different camera solve. Source, decode and pose
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
JSONL diagnostics. Full automatic video-to-pose-to-fit chaining and native
platform qualification remain required M1 work. Worker source changes retain
the original recipe/checkpoint identity checks; completed fits can still be
explicitly reused, but older optimizer checkpoints are not silently upgraded.
