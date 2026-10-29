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
input, committed-stage and interrupted-stage bytes. The host transient user service launch exits **0**, invocation
`520c3799353a4edabf8f1790c2fcdca1`; actual unit properties confirm active/running,
CPUQuotaPerSecUSec2s, MemoryHigh6,442,450,944, MemoryMax7,516,192,768 and
MemorySwapMax1,073,741,824 bytes. Queue flock and owned-group cancellation apply.
Exact launch argv and properties are in `S/service-launch.json` and
`S/service-properties.json`. Its request SHA-256 is
`b1e5b88b9d1409a99d3d4868227602050558295807a1190d31c3a8dc2f31d058`.

The service records native implementation commit `5e30d0833b2d17051dcc6ce38db52dfcbacd4705`
and CLI SHA-256 `7d9bb03be13e7a318223b064fd886ffbef758bfb25d5e7b30e217f9b6dc391bd`.
It observed no live owned host worker, archived/recovered only the known stale
worker lock, and replayed the exact journal. The worker acknowledged START,
reported **stage_cached** for committed decode key `a13f92e9…` and started unchanged
pose key `664c2523…`. The original partial stage and stale first-attempt result
remain untouched. Recovery `logs/resume-001/result.json` owns its actual
terminal outcome; the explicit cancellation described below supersedes its initial
running observation.
This is operator-supervised experimental recovery, not AP9 automatic yield.

A separate `check_solver_pixels.py` check through the existing isolated
Pillow11.3.0 environment exits **0** in **28.372 s**: all **48/48** regenerated
solver PNGs match independent original-source RGB hashes, indices, PTS and native
3840×2160 grids. **0/48** encoded file hashes equal the intake PNGs, despite equal
pixel samples. Solver PNGs total **452,425,157 bytes**. Exact pairings and proof
are in `S/solver-pixel-correspondence.json` and `S/solver-pixels.log` / `.json`.
Later pose-bound admission must verify actual encoded-image references and retain
both variants' correspondence; pixel equality alone never changes a byte pin.

The live service health observation reports active/running with 6,915,424,256 bytes
current cgroup memory, 6,916,976,640 peak and 1,073,541,120 swap at that observation;
limits remain enforced. `S/service-health.json` owns those instantaneous values.
They are not a production resource-fit pass or terminal result. Final outcome
must be read from the supervised phase's record before continuing downstream.

## Observed memory throttling and bounded retry

A later host cgroup read shows **598,378 memory-high events**, **zero** max/OOM/
OOM-kill events, **6,916,022,272** current memory bytes and **1,073,733,632** swap
bytes. The new pose database had no writes for ~19 minutes. This is observed
reclaim/throttling, not proof of OOM or an accepted resource fit. Counters and
file ages are retained in `S/cgroup-diagnostic.json` and command output.

The operator stopped only this owned service. Native cancellation was acknowledged,
the worker reported **stage_preserved /incomplete**, and exit **130** was recorded
after **1,230.613 s**. The controller's low-level status is FAILED; the observed
cause is deliberate operator cancellation after resource diagnosis, not a failed
registration verdict. All protected files are byte-identical. No poses/map were
published, no input was dropped and no quality threshold changed.

One explicit concurrency retry ran at
`artifacts/sample_2_a0_cpu1_20261029`. The CLI adds bounded `--cpu-threads 1..4`
(default four), passing the existing native configuration directly; invalid,
zero, excessive and duplicate options refuse before dispatch. This retry selects
**one extraction/mapping thread**, retains one matching thread/pair and preserves
all 48 original 4K inference views, source IDs/PTS, feature settings, sequential
recipe and 90% registration threshold. It is a separately labeled resource variant,
not the requested four-thread result or AP9 automatic yield. A fresh disjoint job
copies exact owned PPM bytes; changed worker resource identity is not written over
or relabeled as the old request/cache. The previous stages/logs remain protected.
Both C++26/C++23 builds and batch admission checks pass after adding this control:
**17 cases /23,904 assertions per profile**. The C++ gate passes both changed
translation units with LLVM23.1.1. Raw commands/results have `cpu-control` tags in
`S`. The original data and former stages remain protected.

The one-thread service launch exits **0**, invocation
`475c2e3f6a5741b59c1d224336a1494c`, unit
`world-explorer-sample2-a0-cpu1-20261029.service`. Its request SHA-256 is
`1c0d0af37ffd8966757760575b0c028d3bba83f43dcfdfafeda7a2781036be93`.
The controller records implementation `c4f0eca86b0beee3afb5b7cdf6312533e1b6a5a3`;
actual `poses/batch-request.json` contains `cpu_threads=1`, the worker accepted
START, and fresh decode key `f7a00bee…` began. Exact launch data is in
`S/cpu1-service-launch.json`; new `logs/result.json` owns its terminal state. No further automatic resource
retry is implied.

The one-thread decoder completed in **112.007 s**, then started pose key
`852822ba…`. All **48** regenerated PNG byte hashes, source IDs, PTS and capture
times equal the previous verified solver decode; `S/cpu1-decoded-equivalence.json`
retains the full table. The original intake PNGs remain separate byte variants.

A host health check observes active/running, **2,988,662,784** current memory bytes,
**3,264,036,864** peak, **34,799,616** swap bytes and **zero** high/max/OOM events.
Its database had grown to **56,983,552 bytes** plus WAL and was written within
seconds of observation, resolving the earlier observed stall. These instantaneous
values and actual configured limits are pinned in `S/cpu1-service-health.json`;
they do not certify whole-job fit or a completed reconstruction.

The one-thread run subsequently finished with native **exit 2** after **633.306 s**:
`POSE_DISCONNECTED`, because no single model covers the other candidates' registered
frames. All protected files remain byte-identical. The retained failed stage
`poses/stages/.poses-w4ll81pj` contains two models with **19 and 17 registered
views**; admission requires at least **44/48** in one valid selected model. No
poses/map were published. The memory stall was resolved, while registration failed.

## Retained-model continuation

The read-only command below exits **0**; its full report is in
`S/cpu1-recovery-analysis`:

```sh
.venv-worker/bin/python -I -B tools/inspect_pose_recovery.py \
  --request artifacts/sample_2_a0_cpu1_20261029/poses/batch-request.json \
  --failed-stage artifacts/sample_2_a0_cpu1_20261029/poses/stages/.poses-w4ll81pj \
  --output artifacts/validation/a0-pose-resume-20261029/cpu1-recovery-analysis
```

It records **225** stored pairs, **169** positive-inlier pairs and connected
positive-inlier graph across all 48 views. At the existing 30-inlier pair count,
the graph has a 46-view component and two singletons. These pair counts are not
2D–3D/PnP support, parallax proof or accepted registration. Model selection remains
`unique_strict_superset_v1`, with no valid selected model.

One operator-assisted continuation copies the complete saved database and larger
19-view model into disjoint `artifacts/sample_2_a0_recovery1_20261029/poses`.
`S/recovery_controller.py prepare` pins **124** source files; its first preparation
failed before any worker because the source-root constant mistakenly named the
new destination. Only empty newly created directories were removed, the constant
was corrected, and `S/prepare-mapping-recovery-corrected.json` records exit **0**.
No source data changed. The existing `tools/recover_video_poses.py --initial-model`
helper uses the saved matches and original 48 decoded images with one mapping
thread/random seed 42; it retains the existing mapper/admission thresholds.
This is assisted recovery, excluded from automatic yield, with no feature/match
rerun and no holdout use.

The host service launch exits **0**, invocation
`bea2d4818d7a48d69be205bb3127823a`, unit
`world-explorer-sample2-a0-recovery1-20261029.service`; request SHA-256
`680280a07068f9aa19d5be21c0e7c4eb1f6c5591544f418cd8b51610507aa04e`.
Exact argv and limits are in `S/recovery-service-launch.json`: CPUQuota200%,
MemoryHigh6 GiB /MemoryMax7 GiB /swap1 GiB, stop90 s, runtime7,600 s.
The terminal `logs/result.json` records **FAILED /exit 1** after **200.466 s**,
with all **124** protected CPU1 files unchanged. Mapping returned the same **19/17**
models (`S/recovery-mapped-counts.log`); the normal pose stage then refused them
with the same disconnected-model error. Decode key `930c128a…` completed in
**160.567 s**, and failed pose stage `.poses-ztokvc74` is retained. No pose pointer
or map is published. No further unchanged seed continuation is warranted.

## Changed matching recipe

`python3 S/check_pair_coverage.py` exits **0** against a private copy of the retained
database. `S/pair-coverage-summary.json` records **903/1,128** possible unordered
pairs absent from the sequential database, including **280/323** pairs between
the two retained model memberships. Missing pairs are candidates, not proof of
overlap or registerable geometry. This observed coverage gap motivates one explicit
`exhaustive_guided` recipe variant, rather than repeating the unchanged mapping.

`S/guided_controller.py prepare` exits **0** and reserves fresh disjoint
`artifacts/sample_2_a0_guided_20261029`, copies the exact inference PPM/journal,
and protects **266 files** across both completed failed attempts. The native
`solve-poses` request selects existing `--recipe exhaustive_guided --resume-input true --cpu-threads 1`.
All 48 original 4K views, IDs/PTS, features and 90%/unique-superset admission remain
fixed. The matcher selects guided exhaustive matching; previous request/cache
identities are never relabeled. This operator-selected recipe variant is assisted,
with no AP9 automatic-yield credit. It does not use any holdout or evaluation data.
The service launch exits **0**, invocation
`e5313c48a37c40738ab15fe31590f85a`, unit
`world-explorer-sample2-a0-guided-20261029.service`; request SHA-256
`e9bbad5bdcf55ce070e5cd4304079e11197288f96896342ba3da45c4ce2599b3`.
`S/guided-service-launch.json` pins exact argv and the same CPU/memory/time caps.
The worker acknowledged START, and actual `poses/batch-request.json` selects
`pose_recipe=exhaustive_guided`, `cpu_threads=1`. Decode key `4b7fa3ad…` started.
`S/guided-service-health.json` confirms active/running, CPUQuotaPerSecUSec2s and
the requested memory limits, with 1,933,578,240 current /2,190,479,360 peak memory
bytes and zero swap at that observation. These are live observations, not a
terminal result or production-fit pass. The new `logs/result.json` owns
live/terminal state; previous job outputs remain protected. Native success must
also publish and reopen the fresh sibling
`poses.map` before downstream capture admission.

The required tracked-Python Black check passes again using the retained pinned
26.5.1 formatter; `S/black-recovery.json` owns exact argv/exit. The preservation
check exits **0** and confirms 378 protected media/results, 327 old-job files,
62 environment metadata files and 1,944 prior audit files unchanged, together
with AGENTS/runbook/specification identities; `S/preservation-recovery.json`
records the command and `S/preservation.json` the hash-baseline/source identity.
No production source changes follow the tested concurrency-control commit.
Documentation and whitespace checks pass; exact documentation command/exit is
retained in `S/docs-guided-handoff.json` (and the final documentation receipt).

## Remaining work

Real capture admission still needs accepted poses/calibration and independently
localized evaluation cameras, exact original/solver-image correspondence and
real eligibility provenance. Recipe/resource/rights closure and the native A1
completed/PBR pilot remain open; no milestone or full gate is promoted.
