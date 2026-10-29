# Frozen sample_2 baseline pose attempt — 29 October 2026

**A0/AP0 IN_PROGRESS; A1 NOT_STARTED.** A fresh experimental CPU pose attempt
uses only the existing 48 frozen inference views. All 12 selected holdouts and
held-out temporal blocks remain excluded. This is the authorized baseline-only
estimated route; it supplies no measured scale/up, completed geometry, PBR,
changed-light/background evidence or real AP/Q5/N2 qualification. No fit or asset
producer is requested. The first attempt has no terminal receipt after chat
interruption; its completed input/decode work and partial feature data are retained.

Repository root `/home/Pooh555/programming/World-Explorer`, 29 October 2026,
Asia/Hong_Kong. Implementation commit
`e52f01092c60795e34f24cefe84275a3b96300b9`; native CLI SHA-256
`8944afb1ae712999302b715b2699bc417b146cc13a04a4d4ec9a4eda1e0b70a6`.
`R=artifacts/validation/a0-corpus-v2-20261029` retains scripts and command receipts;
`J=artifacts/sample_2_a0_20261029` owns this separate attempt's source/intent,
`poses/` stages and `logs/poses.log` / `logs/result.json`. Old jobs and both source
generations are preserved. No media, generated image or stage is committed/uploaded.

## Independent inputs and actual journal check

Unchanged source SHA-256:
`a5630370d7039c78cc822dbd17a967711be3632bd2cdb43519eab8c2fa876144`.
Frozen selection SHA-256:
`10e8f500daeafff07fe69d794707c12e61114521e5474a8135dafe13bc6944f6`.
Prepared inference-stage completion SHA-256:
`381053cd8bf4498e2ee0ada7aa055f1b6f0d05844adc4051dffbccc768b9dd09`.
The [source pixel correspondence](2026-10-29-sample2-native-views.md),
[frozen producer/consumer check](2026-10-29-frozen-view-inputs.md) and
[request integration](2026-10-29-corpus-v2.md) remain separate evidence.

The existing isolated Python3.12.15 /pycolmap4.2.1 environment and explicitly
selected generated FlatBuffers25.2.10 bindings are used without installation.
`J/request.json` SHA-256 is
`86869108f95e3bb606022b770d3f57c6850c4fe70e8b57412ebe74dbcba18b14`.
It records exact argv, sequential matching recipe, original 3840×2160 inputs,
four extraction/mapping threads, one matching pair/thread at a time, 2 GiB native
RGB journal, existing 32 GiB worker stage output ceiling and 3,600 s stage timeout.
These are scoped software limits, not an aggregate process-tree memory/fit pass.
Scale remains ARBITRARY; walking is unavailable. The 90% connected-registration
admission threshold is unchanged; no partial model is promoted by this attempt.

`python3 R/pose_attempt.py run` acquires the existing global queue flock before
launching the native `solve-poses` application command with independently pinned
selection and explicit worker/pose Python paths. It owns the child and records
actual native/worker/launcher hashes, command, status, exit and elapsed time.
PID fields belong to the sandbox namespace; they are not host kill targets.
`R/pose-session.json` identifies the original execution session; that session is
now unavailable and its namespace-local PIDs must not be signalled on the host.

The worker acknowledged START and accepted the complete native RGB journal.
`python3 R/run_check.py pose-input-check python3 R/check_pose_input.py` exits 0:
all **48** P6 payloads match the independent original-source RGB hashes, exact
full-file hashes, original source IDs/PTS/capture times and native dimensions.
The closed input inventory is **48 PPMs + journal**, **1,194,394,416 PPM bytes**,
with original source count **10,798**. No selected held-out index occurs. Exact
journal identity and summary are retained in `R/pose-input-check.log`; the JSON
sibling records command/exit/time. This input-only pass is not a solver result.

## Interruption and checked recovery

On continuation, execution session66953 is unavailable, the global queue flock is
free and no owned `we_process`/pose-worker process appears in a host-side read-only
check. The sandbox cannot see host PIDs or access the user service bus; those two
checks were performed outside it. Original `J/logs/result.json` is preserved as a
**stale RUNNING record**, not a terminal success/failure or exact exit/signal.

The committed worker decode completed in **226.977 s**. The interrupted temporary
pose stage has a SQLite database with **4 images/keypoint/descriptor rows**, no
matches/two-view geometries and no sparse model. A copied DB/WAL/SHM snapshot
passes `quick_check`; two source-byte scans agree. These are partial features,
not four accepted camera poses. The source stage remains unchanged. The existing
sparse-model recovery helper cannot adopt a stage with no model; recovery will
reuse only the verified native input and committed decode, preserving the partial
feature directory without promoting it to a completed stage.

`we_process solve-poses ... --resume-input true` now exposes the existing native
checked-input resume API. Worker locks remain an explicit refusal; the CLI never
deletes one. Host verification of no live owned process precedes removal of only
this attempt's known stale lock, whose original bytes are archived. Default fresh
output behavior, original input pins, recipe and 90% registration threshold remain.

`S=artifacts/validation/a0-pose-resume-20261029` owns recovery scripts, build/style/
focused-test logs and the interrupted DB observation. Both C++26 development and
explicit C++23 compatibility builds pass. Existing batch admission plus the new
CLI lock/refusal regression pass **17 cases /23,789 assertions on each profile**.
The C++ gate passes all **3 changed C++ files** with LLVM23.1.1, including format,
static analysis, boundaries/docs and whitespace. The first fixture build lacked
its private CLI path macro and application helper include; those are corrected.
The formatter initially misread a comment apostrophe as a character literal;
its token guard was corrected before writing that file. The first DB snapshot
assertion assumed 48 inserted images; the read-only observer now reports the
actual partial count, with no change to solver admission.

The prepared recovery at `J/logs/resume-001` pins exact argv and all existing
input, committed-stage and interrupted-stage bytes. It is intended for a transient
user service with the existing laptop limits: two CPU cores in aggregate,
6 GiB memory high /7 GiB max /1 GiB swap, owned process-group cancellation and
queue flock. Actual service launch/properties and terminal outcome will be recorded
when observed. No automatic pipeline capability is inferred from preparation.

## Remaining work

Real capture admission still needs accepted poses/calibration and independently
localized evaluation cameras, exact original/solver-image correspondence and
real eligibility provenance. Recipe/resource/rights closure and the native A1
completed/PBR pilot remain open; no milestone or full gate is promoted.
