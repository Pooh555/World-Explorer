# Current progress

Updated 2026-10-12 (Asia/Hong_Kong). Active milestone: **M1, IN_PROGRESS**.

The native foundation builds and runs on Arch Linux. GCC 16.2.1 and Clang 23.1.1
C++26 builds, contract/worker tests, address/undefined/leak sanitizers, formatting,
include boundaries and targeted static analysis pass. Native GPU tests cover
two-splat compositing, replacement, deletion and resize with zero Vulkan and
synchronization validation errors. No M1–M6 milestone has passed its complete gate.

Implemented and integrated foundation: native SDL3/ImGui application; Vulkan
anisotropic splat projection/radix sort/quad renderer; bounded update sink;
transactional scene/map snapshots and checked scene bundles; native cancellable
worker launch; real FFmpeg/COLMAP/gsplat worker adapters; fly navigation.

The original video failed connected poses, including one explicit recovery.
The user replaced it and authorized moving the replacement to ignored
`data/local/sample.mp4`. It is 20.948333 s, 3840×2160 H.264, 30 FPS; SHA-256
`51263d9991dcbc2136d0809ecba1a8ba1b261896f3b222b7afba19784a153ef6`.
`artifacts/sample2-job` successfully registered **63/63 frames in one COLMAP
model**. Inspect/decode/pose times: 0.159/92.066/265.764 s. The explicit local
gsplat development recipe completed **7,000 steps and 304,867 Gaussians** after
native cancellation at step 4,323 and compatible optimizer/RNG resume. Its
checksummed output is published at `artifacts/sample2.wexplore` and displayed
in the native viewer with zero Vulkan/synchronization errors. Scale is arbitrary;
geometry/walking remain unavailable. Imagery stays local. Earlier failed fits
and the corrected uninterrupted fit are retained separately.

Local environment: Arch Linux, GCC 16.2.1, Clang/format/tidy 23.1.1, CMake 4.4.4,
SDL 3.4.18, Vulkan headers/loader 1.4.363, OpenSSL 3.6.5, Python 3.14.8.
An escalated `vulkaninfo --summary` detected NVIDIA RTX 4060 Laptop GPU,
Vulkan 1.4.351, driver 615.78.08. This is not reference fixture H_R/H_V.
Sandbox-only GPU probes fail to load the ICD; do not misreport that as missing hardware.

Working inspection command:

```sh
build/dev/we_viewer --scene artifacts/sample2.wexplore --source-view --max-splats 500000
```

Viewer jobs, preferences/theme and scene import/source-view conversion now have
separate files. Loading honors renderer budgets. Source view is an explicit
upright spectator reset, not physical-camera following or calibrated reference
parity. Settings replace atomically on Windows as well as POSIX. Snapshot/device
ownership cleanup was fixed and rechecked through shutdown.

The shared native coordinator now continues those completed poses with explicit
`local_gsplat_v1` start/resume/completed-reuse actions, verifies the full pose
inventory and fit provenance, publishes a checked bundle and loads it in the GUI.
Appearance drawing pauses during processing while the UI/cancellation remains
available. A native completed-checkpoint restore reproduced the identical final
PLY hash without repeating optimization or poses. The GUI startup command and
render pause/resume were exercised on the native GPU. See the
[job contract](protocols/jobs.md) and [coordinator evidence](validation/2026-10-11-coordinator.md).

The user reported native CI: Ubuntu core passed, while Linux Clang/macOS failed
required library probes and Windows failed OpenSSL discovery. The workflow now
selects compatible compiler/library pairs and explicit SDK roots, retaining the
feature and warning gates. Local GCC C++26, GCC C++23 CPU and Clang C++23 CPU
each pass 24/24 tests; actionlint, format, boundaries and targeted tidy pass.
Follow-up hosted logs confirm macOS configuration succeeds and Windows builds,
with 22/24 Windows checks passing. The remaining macOS byte-to-JSON compilation
error and Windows test assumptions about short/long path spellings are repaired.
Local checks now also pass 24/24 with libc++ 23.1.1 and with an aliased temporary
directory. The latest fixes remain IMPLEMENTED_UNVERIFIED on the hosted native
profiles until the next user push. See [initial CI evidence](validation/2026-10-11-ci.md)
and [follow-up evidence](validation/2026-10-11-ci-followup.md). Apple Clang/viewer/release
qualification remains separate from the macOS LLVM 20 CPU job.

The first worker transport slice now generates pinned FlatBuffers C++/Python
bindings and verifies bounded, ordered typed events on the shared native path.
Real capture inspection, typed gsplat failure and checked completed-fit reuse
were exercised. Four local profiles pass 27/27 tests, including ASan/UBSan/LSan.
See [worker wire evidence](validation/2026-10-11-worker-wire.md). Commands still
select explicit JSON requests through argv; persistent bidirectional control is pending.

The native `frame_source` now replays that committed decode stage with immutable
RGB leases, stable IDs, original PTS, seek/reset/EOF, bounded decoding and cancellation.
All 63 real frames replay; independent first/last RGB hashes match. Five local
profiles pass 31/31 CPU/worker checks. See [frame evidence](validation/2026-10-11-frame-replay.md)
and [source contract](protocols/capture.md).

Checked native pose import and a labeled `COMPLETED_REPLAY` estimator now preserve
OPENCV coefficients, source IDs/timestamps and optical transforms. Shared stage
verification also protects existing fit continuation. Capacity-one replay of the
63 real frames publishes an immutable in-memory map snapshot; five local profiles
pass 37/37 CPU/worker tests. See [pose evidence](validation/2026-10-12-pose-replay.md)
and [contract](protocols/poses.md). It does not certify a fresh pose solve.

The current `artifacts/sample2-job/decode.json` points to the older capture's stage,
while its completed poses/result still belong to the replacement. Do not rewrite
that pointer or restart work. Use the explicit retained stage:

```sh
build/dev/we_process replay-poses artifacts/sample2-job data/local/sample.mp4 --decode-stage artifacts/sample2-job/stages/decode-36c925841001fd1027759ac7dcab6451304305b9987c5e1da3e8ab1f5eaa9c6b
```

The native `BATCH_SOLVE` estimator now admits ordered RGB leases into a bounded
immutable journal before consuming them and runs isolated CPU COLMAP at finish.
Its shared application/CLI path preserves failed admission, retries and cancellation;
five local profiles pass **40/40** tests. Real PPM conversion exposed an automatic
FFmpeg probing error, fixed with explicit image2/PPM decoding. The subsequent
sequential attempt produced 2- and 63-frame models and was rejected without changing
the single-model gate. One explicit exhaustive-guided recovery succeeds in
`artifacts/sample2-native-poses-20261012-recovery`: one model, **63/63 poses**,
30294 points, one OPENCV calibration, an immutable in-memory map at epoch 0/revision 1.
The 62.842 s PNG stage and 1162.089 s pose stage are development timings, not P1.
Pillow independently confirms RGB equivalence for all 63 original/admitted/converted
frames. No optimizer was run; original poses/fit and both failures remain preserved.
See [batch evidence](validation/2026-10-12-batch-poses.md) and [contract](protocols/poses.md).

Working inspection of the fresh result (no solve or fit):

```sh
build/dev/we_process replay-poses artifacts/sample2-native-poses-20261012-recovery data/local/sample.mp4
```

Next executable task: define the canonical FlatBuffers pose/calibration schema and
native checked writer/reader, including camera coefficients, preprocessing,
source/journal/pose provenance and scale/gravity. Use the retained successful poses
to verify serialization without rerunning COLMAP. Then integrate durable map
snapshot/revision publication and reload with checksum/atomicity/corruption tests.
Keep both successful pose jobs and the original completed fit; do not mix their
pose provenance or restart them.
Manual pose preparation and direct Python JSONL diagnostics
remain transitional, not a waiver. Full video preflight
and automatic pose-to-fit chaining are still pending.

Outstanding gates include the prescribed ≤250k fixture, reference renderer parity,
complete worker control protocol, durable map storage, native Windows/Mac runs, prescribed
corpus/performance and geometry/collision work. No Phase 1 milestone is complete.
M7–M8 remain deferred. User edits to `docs/execution_prompt.md` and
`docs/technical_plan.md` remain untouched and excluded from implementation commits.
See [local evidence](validation/2026-10-11-foundation.md) for check scope and commands.
See [capture and current checks](validation/2026-10-11-capture.md) for the real fit,
failure/resume evidence and exact capture/fixture identities. The coordinator
report records its historical **24/24** CPU checks in GCC/Clang C++26, explicit C++23,
ASan/UBSan/LSan and separate TSan, plus native GUI and GPU checks.
The development resize (1280 px), 304,867-splat scene and this laptop do not
certify original benchmark/platform gates. No milestone was promoted to VERIFIED.
Local commits: foundation `ea289fc`; subsequent capture slice is the commit
containing the newer validation report. The coordinator slice contains its own
validation report. No remote push.
