# Native reconstruction job command (development)

`we_process reconstruct --request JOB.json` and the viewer's **Continue poses,
fit and open scene** action call the same application coordinator. The viewer can
also start this command with `--reconstruction-request JOB.json`. Processing runs
on its owned background thread; appearance projection/drawing pauses while UI,
presentation and cancellation remain active. Resident scene assets are retained.
The completed scene enters the usual renderer update path after bounded loading.

This command continues a completed pose job. It does not restart successful
poses. Use the existing pose-worker request (`stop_after: "poses"`) to produce
that job first. Full video preflight and automatic stage chaining remain M1 work.
The `local_gsplat_v1` recipe is explicitly selected, development only, and does
not substitute for the upstream Q2/Q3 recipe. There are no downloads or remote
fallbacks. An existing bundle and completed fit can be published/viewed without
a Python or CUDA installation.

Schema 1 rejects unknown fields, unsafe overlaps, fractional/oversized limits,
requests over 64 KiB and excessive JSON nesting. Relative filesystem paths
resolve against the request file's directory; a bare `fit_python` executable is
looked up through PATH. The UI has separate pose and reconstruction request fields.

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

Cancellation interrupts checksum reads and worker processes. The previous scene
manifest survives failures before publication. Final immutable asset writing and
atomic manifest replacement form a commit boundary; a later cancellation cannot
undo a committed bundle. PLY decoding and final bundle writing are currently
bounded but not interruptible internally. If the viewer budget rejects the
published scene, the error includes its output path and keeps the previous scene.
The startup request mode returns failure instead of treating an empty viewer as
success. No scene is silently trimmed to fit a smaller renderer budget.

The subprocess boundary still uses the staged JSONL diagnostic transport described
in [worker.md](worker.md). Length-prefixed FlatBuffers and concrete native
frame/pose adapters remain required M1 work.
