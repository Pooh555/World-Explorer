# Fresh stage chaining — 12 October 2026

Working directory: repository root. Implementation based on `95f38df`; the local
commit containing this report records the completed slice. M1 remains IN_PROGRESS;
M7–M8 remain deferred. No remote push or media upload.

## Implemented scope

Shared GUI/CLI schema-2 reconstruction supports explicit `pose_action: solve`:
owned automatic decode or checksummed retained decode → native RGB `frame_source`
admission → isolated batch pose worker → checked durable map → existing explicit
fit/start/resume/reuse → transactional scene publication. Decoder requests are
immutable and bounded; ownership binds source/inspection/worker/parameters. The
verified START/CANCEL session checks video/source/frame/output capacity. Returned
decode artifacts, all inventory bytes, catalog IDs/timestamps and inspected image
dimensions/rotation/time base are checked before poses. Data is checked again
before publication. Timestamp sampling is explicitly unqualified for the planned
blur/exposure/cuts/lens/coverage selection.

`POSE_JOB.plan` binds source/decode SHA, worker/path/Python choices, recipe,
threads and RGB journal budget; its writer lock remains owned through publication.
Unowned/corrupt jobs, overlapping writable/input/request paths and incompatible
intents fail without reseeding. Input replay verifies byte-identical admitted PPMs
before consuming leases; journal and backend-request publication accepts only
exact existing bytes. Uncommitted temporary input and stale Python locks require
diagnosis, not deletion. Complete matching solves are checked/reused without
invoking the solver. Source/journal/frame hashes bind native pose/map/fit inputs.
Old `solve-poses` defaults still require fresh output. Existing inspection owner
bytes/names remain compatible after extracting shared owned-job admission.

Backend scripts and native control protocol source were not changed. No model or
environment is installed implicitly. The API is implemented; synthetic complete
chains do not qualify a real fresh solve/fit scene. See [schema](../protocols/jobs.md).

## Local checks

All six final build/CTest profiles exit **0**, with **79/79** tests each.
Arch Linux; GCC 16.2.1; Clang/format/tidy 23.1.1; CMake 4.4.4; OpenSSL 3.6.5;
Python 3.14.8; pinned FlatBuffers 25.2.10.

Actual commands: [2026-10-12 check reference](checks-2026-10-12.md),
DEV, CLANG, COMPAT, LIBCXX, ASAN and TSAN; results/counts and logs elsewhere apply
to this report only.

Sanitizer build/discovery/execution uses authorized escalation. Final logs are
ignored `artifacts/validation/stage-chain-build-*.log` and
`stage-chain-tests-*.log`; dev final logs are `stage-chain-build-dev-final.log`
and `stage-chain-tests-dev-fixed.log`. The subsequent header comment clarifies
controlled input replay and does not alter executable behavior.

Five new tests cover complete synthetic chained publication with automatic or
retained decode, unchanged completed-solve restart, changed input/worker/recipe,
cooperative solver cancellation and journal restart, mismatched/uncommitted RGB
admission preserving caller data, failed decode/solve preserving prior scenes,
explicit schema/resources and disjoint paths. Test workers copy tiny labeled
synthetic canonical assets, not actual COLMAP/gsplat output; no quality gate passes
because those fixtures publish a scene. Existing fit start/resume/reuse tests
continue to exercise the shared fitting boundary.

`clang-format --dry-run --Werror` passes on 14 changed first-party C++ files.
`python3 tools/check_boundaries.py` and `git diff --check` exit 0. Targeted analysis
exits 0 (`stage-chain-tidy-fixed.log`):

```sh
clang-tidy -p build/dev src/application/owned_job.cpp src/application/video_decode.cpp src/application/pose_job.cpp src/application/reconstruction_job.cpp src/reconstruction_adapters/batch_poses.cpp tests/unit/reconstruction_job_tests.cpp tests/unit/completed_poses_tests.cpp --warnings-as-errors='*'
```

Initial compile failures (shadowed hash-builder name and an outdated ownership
helper call) were repaired. Initial fresh-chain test failures exposed missing
`source_sha256` in the synthetic native-journal fixture; it now emits the checked
profile. Tidy findings (swappable filename parameters and unchecked optional
access) were fixed without disabling warnings. Initial dev tests ran an older
binary after a failed build; that run is not evidence for new behavior. The
subsequent `stage-chain-tests-dev.log` records the actual 77-test fixture failure;
the final 79-test run passes. Initial failures remain in ignored logs.

Source identity (`stage-chain-identity.json`):
`1a6720b78b5252d87548fd9bc2eea00839264c1f2699379cb8b0c5fba837b0c6`.
This hashes 124 sorted `CMakeLists.txt` and `.cpp`/`.hpp`/`.fbs`/`.py` files under
`apps`, `include`, `src`, `tests`, `schemas`, `workers`, concatenating UTF-8
relative path + NUL + binary file SHA-256. Dev CLI SHA:
`3078bbda580b223322c4656b8a5d2d1e8df1603f28a29a420b6062c6cbeead43`.

## Real decode and cancellation scope

Private authorized video: `data/local/sample.mp4`, SHA
`51263d9991dcbc2136d0809ecba1a8ba1b261896f3b222b7afba19784a153ef6`.
The native full-request path adopts the retained checked metadata inspection,
runs automatic decode in fresh owned output, validates every committed file, then
SIGINT cancels at the fresh pose-admission boundary. Probe:

```sh
python3 artifacts/validation/stage-chain-real/verify.py
```

The probe exits 0; its two application processes intentionally exit **130**.
Results are `stage-chain-real/{decode-cancel.log,cached-cancel.log,verification.json}`:

- Automatic decode selects **63 frames from 628 source frames**, preserving
  original PTS/time base. It verifies the 3840×2160 image domain against inspection.
- A separate process reuses `stage_cached` with unchanged decoder ownership,
  immutable worker request, stage completion and pose intent. Its input replay
  starts from the same owned selection and cancels again.
- Each invocation acknowledges exactly one START (decoder). **Zero pose worker
  invocations**, zero optimizer invocations, zero admitted PPMs in this measured
  probe. No full RGB journal, batch backend request, map or scene is published.
  Complete RGB replay/solver cancellation is narrower synthetic test evidence.
- **18 prior manifests** and the video retain their hashes, including the original
  pose/fit/scenes, independent recovery result, prior inspection/preflight outputs
  and automatic-inspection ownership/output manifests. The stale original decode
  pointer and successful COLMAP/gsplat work are untouched.

Decode stage key:
`38b6200d10b9b7a1494c8f1ef9264a56ec3bc40f06e840ebeb6a3b788bad8ca4`;
completion SHA:
`8c3c527032ca2eb1d10c88b3d2bd0d5e1e550d8b08c19c32d3eee64bda6d1728`.
The request is `artifacts/sample2-stage-chain-20261012/decode-and-cancel.json`, SHA
`037b1e604d1b926782c4ba384742998f8dd70108a2e4277009056bda72825f2b`.
The first worker reports **173.3471889150096 s** for decode only. Compiler builds
were competing for CPU; this is not a P1 whole-video or reference-hardware result.
Large local RGB PNG outputs remain ignored.

No new solver, optimizer, GPU, native GUI or external OS run occurred. Native
Windows/macOS control/restart, renderer parity, prescribed ≤250k fixture, full
real fresh-chain solve/fit, corpus/reference hardware and Q/P/N/E gates remain
unqualified. No Phase 1 milestone was promoted to VERIFIED.
