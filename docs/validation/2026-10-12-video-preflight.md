# Retained video preflight — 12 October 2026

Working directory: repository root. Implementation based on `b127184`; the local
commit containing this report records the complete slice. M1 remains IN_PROGRESS;
M7–M8 remain deferred. No remote push or upload.

## Implemented scope

Schema 2 explicitly selects retained poses and a committed inspection stage by
completion SHA. The GUI/CLI coordinator calls shared `preflight_video` before map
preparation/fitting and rechecks it before compilation. Checked metadata bytes
are bound to their inventory digest. Limits are ≤16 inspection files/16 MiB,
64 KiB completion/result JSON, source ≤16 GiB, dimensions ≤4096² pixels and
positive duration ≤1200 seconds. Rotation, original rational time base, SDR
policy, source size/hash and current output storage are checked. Unsafe overlaps,
non-directory output ancestors and unknown available storage fail without writes.
The scene records the selected inspection completion dependency. Legacy schema 1
remains a retained-pose continuation without a metadata-preflight claim.

The selected free-space threshold is checked at each output filesystem. It is
neither a reservation nor a prediction of whole-job peak storage. Retained
inspection adoption does not execute a backend or decode pixels. Automatic fresh
inspection, capability checks for new stages and fresh video/pose chaining remain
unfinished, as do selection diagnostics and codec/color-conversion qualification.

## Local checks

Arch Linux, GCC 16.2.1, Clang/format/tidy 23.1.1, CMake 4.4.4, OpenSSL 3.6.5,
Python 3.14.8, pinned FlatBuffers 25.2.10. Final build/test logs use
`artifacts/validation/preflight-*-final-v2.log`.

Actual commands: [2026-10-12 check reference](checks-2026-10-12.md),
DEV, CLANG, COMPAT, LIBCXX, ASAN and TSAN; results/counts and logs elsewhere apply
to this report only.

All six final profiles pass **67/67** tests; each build/CTest command exits 0.
Native sanitizer build/discovery/execution uses
authorized escalation; no sandbox failure is counted as a native test pass.
Three new tests cover schema/adoption choices, exact dependencies, malformed
metadata, unsigned rotation wrap, timestamp bounds, HDR, inspection byte/file
budgets, unsafe paths, unknown/insufficient storage, cancellation, no-write
admission and unchanged scene/map manifests across restart. Fixtures are synthetic
admission checks, not successful video reconstruction evidence.

`clang-format --dry-run --Werror` passes on all 11 changed first-party C++ files.
`python3 tools/check_boundaries.py` and `git diff --check` exit 0. Production tidy:

```sh
clang-tidy -p build/dev src/application/video_preflight.cpp src/application/reconstruction_job.cpp src/application/job_validation.cpp src/application/scene_import.cpp src/reconstruction_adapters/worker_artifacts.cpp --warnings-as-errors='*'
clang-tidy -p build/dev src/application/video_preflight.cpp tests/unit/reconstruction_job_tests.cpp --warnings-as-errors='*'
```

Both final commands exit 0 (`preflight-tidy-production-verified.log` and
`preflight-tidy-final-v2.log`). No warning or resource gate was weakened. Initial
integer widening and checked optional fixture access findings were repaired.
A test helper initially used numeric JSON equality to detect metadata changes;
signed/unsigned equality hid the extreme rotation mutation. It now compares
serialized fixture values so the intended rejection actually runs.

The initial libc++ run failed because `/proc` available storage is reported as
`uintmax_t(-1)`, while libstdc++ reports 0. Preflight now rejects the standard
unknown sentinel with IO_ERROR, rather than accepting it as unlimited space.
Both insufficient and unknown storage are tested. The initial failure remains
in `preflight-libcxx-tests-verified.log`; final libc++ checks pass. An initial
private verification script incorrectly addressed provenance at its wrapper root;
it now reads the actual `metadata` object. The script failure is retained in
`artifacts/validation/preflight-real/verify-initial-failure.log`.

Source identity (`preflight-identity.json`):
`c06e7efc75c5b479858ba51395719abf92367c3c6fba47b5eada0b42d985088e`.
This hashes 111 sorted `CMakeLists.txt` and `.cpp`/`.hpp`/`.fbs`/`.py` files under
`apps`, `include`, `src`, `tests`, `schemas`, `workers`, concatenating UTF-8
relative path + NUL + binary file SHA-256. Final dev CLI SHA:
`56f170a88c1baf2e06e3f578b74e19bea4c40c9286d6058b2d84e6ff0a53004d`.

## Retained real capture

Private video: `data/local/sample.mp4`, source SHA
`51263d9991dcbc2136d0809ecba1a8ba1b261896f3b222b7afba19784a153ef6`.
Explicit metadata stage:
`artifacts/sample2-control-final-20261012/stages/inspect-a5f8cd199ed87fa4c638253997e83bc62f26b9630d9d4c15c6b6360f9c1c7ee4`,
completion SHA
`3847af14f8981590efb48944941c0b5325238aafc2b6a0187e9ec142a0abcc75`.
The retained primary stream reports 3840×2160, duration **20.933333 seconds**,
rotation 0 and time base 1/30. Container duration is separately 20.948333 seconds;
these are not substituted for one another. No timestamp synthesis occurs.

The schema-2 request uses the original 63/63 pose job, explicit correct decode
stage and completed fit, with **64 MiB current free-space checks** at its outputs:

```sh
build/dev/we_process reconstruct --request artifacts/sample2-preflight-20261012/adopt-original.json
```

Outputs are `artifacts/sample2-preflight-20261012/original-map` and
`original.wexplore`. It does not rerun COLMAP, the optimizer or a pixel decoder,
and keeps the stale original decode pointer untouched. Native GPU/UI execution,
reference renderer parity, Windows/macOS, corpus and Q/P/N/E qualification are
outside this check. No M1 or benchmark gate is promoted.

The final real probe is:

```sh
python3 artifacts/validation/preflight-real/verify.py
```

It exits 0 (`verify-final.log`), invoking two separate schema-2 CLI processes
(exit 0) and a request with an intentionally wrong inspection SHA (exit 2).
Restart and rejection preserve both newly published map/scene manifests.
Committed-index inspection checks index/chunk digests, all **304,867 splats**,
63 source-frame dependencies and byte-identical payloads against the original
scene. All eight historical input/job/scene manifests plus the selected inspection
completion retain their hashes. The video SHA is checked separately. Full local
results and the request/CLI hashes are in `preflight-real/verification.json`.
