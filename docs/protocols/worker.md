# Worker boundary and stage format

`we_process` and the GUI invoke the same native `run_worker` application command.
Bootstrap arguments are passed as an array without a shell; native invocations
then use bounded stdin commands and separately drained stdout/stderr. Ctrl-C is
handled by the CLI; GUI logs remain bounded at 128 KiB.

Native `run_worker` uses **FlatBuffers command/event protocol 2.0**, generated
from [`commands.fbs`](../../schemas/worker/commands.fbs) and
[`events.fbs`](../../schemas/worker/events.fbs) with FlatBuffers **25.2.10**.
C++/Python bindings and the matching Python runtime live in the build tree. No
protocol 1 fallback is attempted. Existing artifact/request JSON schemas remain
independent of this incompatible transport upgrade.

Both directions use standard uint32 little-endian size-prefixed FlatBuffers.
Stdout envelopes have identifier `WEW2` and a maximum 1 MiB body; stdin envelopes
have `WEC2` and a maximum 64 KiB total frame. Limits are checked before allocation.
A fresh 64-character hexadecimal session binds every message. Event sequence
starts at zero with ≤10,000 events; invocation operation is 1 (handshake uses 0).
Native structural verification limits are depth 16/tables 128; semantic checks
cover version, UTF-8, lengths, finite durations, paths, hashes, resource limits,
sequence and lifecycle. Unknown types/versions/sessions, gaps, data after terminal,
truncated EOF or inconsistent process exit invalidate the invocation.

Commands in this one-batch-per-process profile:

| Command | Sequence / operation | Behavior |
|---|---|---|
| START | 0 / 1 | Up to 64 adapter-owned argument strings, each nonempty UTF-8/NUL-free ≤4096 bytes, total ≤32 KiB. Carries selected options and explicit local JSON request paths as structured values; never shell text. |
| CANCEL | 1 / 2 | Targets invocation operation 1; requests the worker's existing cancellation/checkpoint hook. |

The worker advertises capabilities first. Only after native verification does the
client send START; the worker acknowledges it before invoking the backend or
emitting progress. The Python command reader explicitly verifies tables/vtables,
field extents/alignment, references, strings, vectors and semantic bounds;
generated Python accessors alone are not structural verifiers. START retries with
identical decoded arguments acknowledge again without invoking twice, including
valid alternate FlatBuffer layouts. Changed content under the same operation is
rejected. CANCEL retries acknowledge again and call the cancellation hook once.
At most 64 subsequent messages are admitted; no growing receipt history exists.
Acknowledgements bind command kind, sequence and operation. Unsolicited replies
are rejected. Success requires START acknowledgement and a checked completion.
A protocol failure may end a session before START is accepted. The client imposes
a 30-second startup/START-acknowledgement deadline.

Handshake declares backend/version, sensors, batch/streaming capability, device
options, checkpoint semantics/rights and frame/source/output limits. Recipes
remain batch-only; there is no streaming tracker, multi-job daemon or remote
transport. Completion references a bounded local manifest by absolute path,
byte count and SHA-256; the client checks it before accepting the exit. Scene/fit/
pose admission independently verifies inventories and provenance. Session IDs do
not change stage/checkpoint content identities; changed worker source still does.

CANCEL acknowledgement means accepted, not that a checkpoint is already durable.
The worker's terminal result/committed assets determine the outcome. The client
allows two seconds for cooperative stop, then POSIX SIGTERM and a further two
seconds before SIGKILL; Windows terminates the owned Job Object after its two
seconds. Output keeps draining during cancellation and input backpressure.
Invalid event streams force cleanup. A stop before START does not launch backend
work. Cancellation after an atomic stage commit retains that valid stage.

POSIX uses local pipes with nonblocking parent writes and reserves descriptors
above 0–2 for desktop hosts with closed stdin. Thread-local SIGPIPE masking leaves
the application signal policy intact. Native solver subprocesses stay in the
owned worker process group; direct Python diagnostic runs retain their separate
subprocess groups. Windows uses local named byte pipes with overlapped input
writes, a whitelist of inherited stdio handles and a lifetime Job Object. Python
wire streams explicitly use binary mode on Windows. C++ callbacks run on one
process owner; Python command acknowledgements and worker events share a locked
writer. The input reader uses raw `os.read`, avoiding a buffered stdin lock during
interpreter shutdown. EOF/malformed commands request backend stop even if writing
the failure to a disconnected stdout fails.

Direct Python CLI invocation without wire bootstrap retains JSONL diagnostics.
This is explicit diagnostic compatibility, never a native fallback.
`worker_configuration.framed_protocol=false` remains reserved for labeled synthetic
subprocess fixtures. Native Windows/macOS execution and real optimizer cancellation
through the new command transport remain unqualified.
Native decoded-frame replay, completed-pose import/replay and fresh selected-frame
COLMAP batch solving are integrated; see [capture](capture.md) and [poses](poses.md).
Canonical FlatBuffers pose/calibration interchange is implemented in
[`poses.fbs`](../../schemas/map/poses.fbs); see [pose assets](poses.md).
Native [persistent maps](maps.md) retain snapshots and retry receipts.
Durable pose preparation is integrated with fitting; automatic full-video
preflight/solve chaining remains M1 work.

`solve-journal --request PATH` consumes a hash-bound native RGB journal, commits a
checked PNG stage and runs the same CPU COLMAP path. It does not inspect/decode the
video or fit appearance. The native estimator always uses verified event transport;
the request is still an explicit JSON file selected through START arguments. Original PTS,
capture times and source identities are preserved. The optional native event
observer exposes typed worker capability/failure/completion messages to adapters.

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
