# Automatic metadata inspection — 12 October 2026

Working directory: repository root. Implementation based on `5cdd9a1`; the local
commit containing this report records the completed slice. M1 remains IN_PROGRESS;
M7–M8 remain deferred. No remote push or upload.

## Implemented scope

Schema-2 reconstruction requests support `inspection.action: inspect`, with an
explicit local worker/Python and fresh or matching owned metadata job. Shared
GUI/CLI orchestration checks protected paths and current output storage before
ownership or worker launch. A bounded owner file binds capture SHA, worker
script/path, explicit Python choice and protocol. Native OS locking excludes
concurrent coordinators without leaving stale OS ownership after process exit.
Unknown entries, symlinks and incompatible/corrupt ownership fail without reseeding.
Request files are protected from writable reconstruction outputs.

The verified FlatBuffers 2.0 handshake must advertise video batch capability,
enough source bytes and at least 16 MiB output capacity before START. Returned
`inspect` completion must name a bounded immutable `result.json` inside the owned
job's `stages/inspect-<key>`. Its exact artifact hash/size, stage inventory and
source are checked, then shared preflight runs before durable pose preparation.
Mutable pointer files do not select the returned artifact. Script/video changes
during execution reject admission. A new transport session on restart allows the
worker's compatible content-addressed metadata cache to be reused. Existing
retained pose/map/fit/scene dependency checks and cancellation remain controlling.

Automatic inspection does not decode RGB, solve poses or run the optimizer.
Retained metadata adoption and schema-1 requests remain supported. The metadata
worker's existing decoded-frame storage estimate still applies to inspection;
no frames are allocated. This is not storage reservation/forecast qualification,
codec/color conversion qualification or blur/cut/lens/coverage selection.
Native file locks are cooperative local exclusion, not defense against external
directory mutation. Owner publication does not certify power-loss behavior.
Interrupted owner writes need diagnosis/fresh output. A surviving Python
`job.lock` requires verifying its recorded process is gone; native code never
silently deletes it. See [request contract](../protocols/jobs.md).

## Local checks

All final build/CTest commands exit **0**, with **74/74** tests in each profile.
Arch Linux, GCC 16.2.1, Clang/format/tidy 23.1.1, CMake 4.4.4, OpenSSL 3.6.5,
Python 3.14.8 and pinned FlatBuffers 25.2.10.

Actual commands: [2026-10-12 check reference](checks-2026-10-12.md),
DEV, CLANG, COMPAT, LIBCXX, ASAN and TSAN; results/counts and logs elsewhere apply
to this report only.

Sanitizer discovery/execution uses authorized escalation. Logs are ignored
`artifacts/validation/inspection-build-*.log` and `inspection-tests-*.log`.
Final dev logs are `inspection-build-dev-final.log` and
`inspection-tests-dev-initial.log` (the first test run passed; code was unchanged
after it). Seven new tests exercise typed automatic-inspection requests, protected
paths, no-write/no-START rejection, unsupported video/source/output capabilities,
missing/escaping/wrong-stage artifacts, malformed metadata, changed source/script,
same-identity restart, worker failure, cooperative cancellation/restart and
unchanged scene/map publication. Native file-lock tests cover exclusion, release,
retained inode, hard links, directories and POSIX symlinks. Fixtures explicitly
use synthetic byte captures/metadata and cannot certify reconstruction quality.

`clang-format --dry-run --Werror` passes on all 11 changed first-party C++ files.
`python3 tools/check_boundaries.py` and `git diff --check` exit 0. This command
exits 0, without weakening warnings:

```sh
clang-tidy -p build/dev src/core/file_lock.cpp src/application/video_inspection.cpp src/application/video_preflight.cpp src/application/reconstruction_job.cpp tests/unit/video_inspection_tests.cpp tests/unit/reconstruction_job_tests.cpp --warnings-as-errors='*'
```

The log is `inspection-tidy-initial.log` (first analysis passed). Initial compile
failures from heterogeneous `ranges::find`, ternary INFO macro precedence and a
missing generator include were repaired before final builds; retained logs are
`inspection-build-dev-initial.log`, `inspection-build-dev.log` and
`inspection-build-dev-fixed.log`. No test threshold was changed.

Source SHA (`inspection-identity.json`):
`370b02bb54c1023a4e4dc2edc43136f28744a687f154b2c0fbd2c30a747d95e5`.
It hashes 117 sorted `CMakeLists.txt` and `.cpp`/`.hpp`/`.fbs`/`.py` files under
`apps`, `include`, `src`, `tests`, `schemas`, `workers`, concatenating UTF-8
relative path + NUL + binary file SHA-256. Final dev CLI SHA:
`2836cc9e43fbca686218802dea3a171e16384845a75ee16382542b350ce14cf9`.
No Python worker source or optimizer recipe was modified.

## Retained real capture

Private video `data/local/sample.mp4`, SHA
`51263d9991dcbc2136d0809ecba1a8ba1b261896f3b222b7afba19784a153ef6`.
The initial real coordinator command exits 0:

```sh
build/dev/we_process reconstruct --request artifacts/sample2-inspection-pipeline-20261012/inspect-and-adopt.json
```

It creates an owned `metadata-job`, acknowledges START, inspects the primary
3840×2160 SDR stream (20.933333 seconds, rotation 0, original time base 1/30), then
adopts the original 63/63 pose job, explicit correct decode stage and completed
fit. Outputs are `original-map` and `original.wexplore` in that pipeline directory.
Metadata stage key:
`a5f8cd199ed87fa4c638253997e83bc62f26b9630d9d4c15c6b6360f9c1c7ee4`;
completion SHA:
`9384a55e318f8be2cf50e8d44ffe09f227757dda2136bef7022d226b3c9c782c`.
Its reported **0.2141042230068706 s** is metadata-stage elapsed time only; it is
not a P1 whole-video measurement. Log: `inspection-real-initial.log`.

The independent local probe exits 0:

```sh
python3 artifacts/validation/inspection-real/verify.py
```

`inspection-real/verify.log` and `verification.json` record:

- Separate-process same-request restart exits 0 and logs `stage_cached`; seven
  current ownership/metadata/map/scene manifests keep their hashes.
- Changed Python ownership selection and metadata output overlapping retained
  poses exit 2 before START; current outputs and retained inputs remain unchanged.
- A separate fresh metadata job acknowledges START/CANCEL after SIGINT and exits
  **130**. No map/scene is created, its cooperative Python lock is removed, and
  previous outputs remain unchanged. This is actual metadata cancellation, not
  optimizer-checkpoint proof.
- Eleven prior manifests and the video retain their hashes. These are the eight
  historical job/fit/scene manifests, prior selected inspection completion and
  two prior preflight output manifests. The stale original decode pointer remains
  untouched.
- Committed index/chunk digest checks confirm **304,867 byte-identical splat
  payloads**, 63 source-frame dependencies and the new scene's exact inspection
  completion provenance.

No RGB decoding, COLMAP, optimizer, GPU or native GUI run occurred. No native
Windows/macOS file-lock/control qualification, renderer parity, prescribed ≤250k
fixture, reference hardware, corpus or Q/P/N/E gate is promoted. Full fresh
decode/pose orchestration remains the next M1 implementation task.
