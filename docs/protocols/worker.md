# Worker boundary and stage format

`we_process` and the GUI invoke the same native `run_worker` application command.
Arguments are passed as an array without a shell. Native cancellation terminates
the worker process tree (POSIX groups or Windows Job Object); Ctrl-C is handled by
the CLI. Stdout/stderr are continuously drained; GUI logs are bounded at 128 KiB.

Native `run_worker` uses **FlatBuffers event protocol 1.0**, generated from
[`events.fbs`](../../schemas/worker/events.fbs) with FlatBuffers **25.2.10**. C++/Python
bindings and the matching Python runtime live in the build tree; no global Python
install is needed. CLI, GUI and local gsplat start/resume use this shared path.

Stdout contains standard size-prefixed envelopes: uint32 little-endian body length
then a FlatBuffer with identifier `WEW1`. Maximum body is 1 MiB, checked before
allocation. Verification limits are depth 16/tables 128; semantic checks validate
version, UTF-8, lengths, finite durations, paths, hashes and resource limits.
Sequence starts at zero, with ≤10,000 events per invocation. Handshake must be
first and unique; progress follows; exactly one completion or failure ends the
session. Unknown types/versions, sequence gaps/retries, data after terminal,
truncated EOF and inconsistent process exit invalidate the invocation. Zero exit
without completion fails. Invalid streams cancel and reap the worker.

Handshake declares backend/version, sensors, batch/streaming capability, device
options, checkpoint semantics/rights and frame/source/output limits. Concrete
recipes remain batch-only, with no streaming tracker or remote transport.
Completion references a bounded local manifest by absolute path, byte count and
SHA-256; the native client checks the file before accepting the worker exit.
Canonical scene/fit/pose admission independently verifies inventories and provenance.

Stderr is separately drained in bounded blocks and delivered to the existing GUI/
CLI log callback. Backend incidental stdout is redirected to stderr. POSIX uses
inherited pipes; Windows uses local byte-mode named pipes rejecting remote clients.
Commands and human-readable JSON request locations are selected through argv for
this one-shot batch transport. Persistent bidirectional command/cancellation
envelopes are pending; native process cancellation remains the control path.

Direct Python CLI invocation without `--wire-bindings DIR` retains JSONL diagnostic
output. This is explicit diagnostic compatibility, never a native fallback.
`worker_configuration.framed_protocol=false` is used by labeled synthetic
subprocess fixtures; production callers default to verified events.
Native frame-source/pose-estimator adapters and canonical FlatBuffers pose assets
remain M1 work; their public interfaces need concrete execution integration.

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
The shared native [reconstruction coordinator](jobs.md) can now continue completed
poses with an explicitly selected local recipe, resume fitting or reuse a selected
completed fit, compile it and load it in the GUI. Separate `fit`/`compile-fit`
commands remain available for diagnostics. Reconstruction recipes are unchanged;
their event transport follows the shared verified boundary. Worker/adapter source
changes produce new recipe identities. Completed artifacts remain usable through
explicit checked reuse; older optimizer checkpoints retain their original recipe
hash and are not silently treated as compatible with changed worker code.
