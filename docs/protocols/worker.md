# Worker boundary and stage format

`we_process` and the GUI invoke the same native `run_worker` application command.
Arguments are passed as an array without a shell. Native cancellation terminates
the worker process tree (POSIX groups or Windows Job Object); Ctrl-C is handled by
the CLI. Stdout/stderr are continuously drained; GUI logs are bounded at 128 KiB.

The development transport is **JSONL diagnostic protocol v1**, at most 1 MiB per
event. `capabilities`, `inspect --source VIDEO --job-dir DIR`, and
`run --request JOB.json` are implemented. Capability output explicitly advertises
batch-only processing, no streaming tracker and no remote transport. Schema 1 job
requests reject unknown fields, non-finite numbers and excessive resource limits.

This is not the specified length-prefixed FlatBuffers transport. That transport,
native frame-source/pose-estimator adapters and canonical FlatBuffers poses remain
M1 work; their public C++ interfaces currently define the ownership/lifecycle
contracts but are not the actual worker execution path.

Stages: inspect → decode → poses → appearance. Every stage uses a source/config/
backend/adapter content identity, an immutable temporary directory and a checksummed
completion inventory. Only a fully successful stage is renamed to its committed
location. Failure keeps incomplete outputs and the previous `result.json`.
Reusing a committed stage verifies every inventory entry. A stale `job.lock` after
a hard crash requires checking that the recorded process has exited before removal.
Different recipes get different identities. Full optimizer resume is not claimed.

Decode preserves original PTS/time base, frame IDs, display rotation and color
metadata. Uniform timestamp sampling is the implemented selector, not a qualified
blur/overlap/cut detector. Orthogonal rotations and SDR are accepted; HDR fails with
an explicit conversion requirement. Sources are local files only; FFmpeg protocols
are restricted to file/pipe. Local filesystem URLs are not a remote transport.

Pose solving uses official pycolmap 4.2.1, CPU SIFT, single OPENCV intrinsics,
sequential matching and incremental mapping. The explicit recovery recipe uses
exhaustive guided matching. Multiple reconstructed components fail visibly; fewer
than 90% registered selected frames also fails. Original native COLMAP outputs and
canonical JSON poses are retained. Appearance is not attempted after failed poses.

The upstream appearance wrapper requires the exact gsplat 1.5.3 source commit,
trainer hash, installed package and cached AlexNet metric weight hash. No implicit
network/model downloads are allowed during fitting. Its checkpoints are evaluation
artifacts, so interrupted appearance restarts; completed earlier stages can resume.

An explicitly separate `we_process fit` command runs the `local_gsplat_v1`
development recipe through the same native worker launcher. It consumes completed
undistorted pose output, publishes a content-addressed annotated PLY and a final
`result.json`, and supports complete local optimizer/RNG checkpoints. It is not
automatically substituted for the upstream `appearance` stage or Q2/Q3 recipe.
`we_process compile-fit` checks the completion schema, path containment, PLY hash
and declared count before publishing a scene. Optional bounded source camera
metadata enables an explicit upright spectator reset; it is not a calibrated
reference comparison. See the [appearance worker](../../workers/gsplat/README.md).
Scene compilation remains an explicit CLI command after a successful export.
