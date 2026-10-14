# Batch-pose admission — 2026-10-14 Asia/Hong_Kong

Scope/status: **M1/E1 IN_PROGRESS**. Sixteen synthetic public-API cases verify
batch admission, retry/publication and the isolated worker boundary. A reproduced
output-path bug is repaired; no reconstruction or complete gate is certified.
Parent `4060da4`; implementation is the commit containing this report. Cwd:
repository root. `R = artifacts/validation/batch-pose-admission-20261014` retains
raw logs, exact argv/exits, identities and protected-file receipts.

## Repair and contracts

The initial path regression fails all three generated cases (Catch exit **42**):
submission unexpectedly accepts a dangling RGB temporary link, dangling destination
link and linked input directory. `R/regression.log` retains the failure;
`repair.log` records the same case passing **423 assertions** after repair.
Batch output checks now inspect entries without following symlinks, reject linked
job roots before canonicalization, and recheck canonical job/input directories
before RGB writes and final publication. RGB/JSON staging uses C++23 exclusive
creation; RGB rollback removes only a temporary file created by that submission.
Existing entries, including dangling temporary links, preserve caller files.
[Pose protocol](../protocols/poses.md) records the behavior.

[Tests](../../tests/unit/batch_pose_admission_tests.cpp) cover invalid configuration,
pre-cancellation/start ordering, observation identity/time/layout/sensor rejection,
changed retry headers/pixels, consumed-header retries, exact memory/PPM byte limits,
cumulative disk exhaustion and corrected owned resume. Rejections compare exact
errors, complete caller trees including link targets, and all observation fields
and leases. Admission consumes only the accepted lease; retained copies survive.
Resumed start rejects foreign entries, locks and uncommitted inputs; changed RGB
shape/content/link failures permit corrected retry without losing receipts.

Publication obstructions remain unchanged; a journal already published before a
request failure is explicitly retained. Failed finish freezes its error without
relaunching; acknowledged receipts remain idempotent after admission closes.
Controlled local workers test failure-code mapping, incompatible capabilities,
wrong-stage/missing-output/nonzero-exit rejection, and both recipes' ordered,
bounded, exactly-once event drains. The success diagnostic copies fresh synthetic
pose fixtures to test checked import and dataset lifetime; **it runs no solver**
and provides no real reconstruction evidence. Cancellation preserves persisted
input and caller retry leases. Symlink cases explicitly skip on Windows accounts
without link permission; no cases skipped locally.

## Actual verification

Baseline `build/coverage/coverage/run-0khgqzsr` and final
`build/coverage/coverage/run-7k6ap09k` retain **86/87 identical production source
hashes**; only `batch_poses.cpp` changes. `R/baseline.json` identifies both source
hashes and confirms the final test hash matches measurement. The final run retains
**224 fresh profiles**, build/object identities, LLVM JSON/HTML and command exits.
Added guards increase measured denominators; nothing is excluded. Scope SHA-256
remains `712ecbee6339c1a83ebba78acdce3bc7d0db014d0bc23e789f688a6d1a042d1a`.

| Measurement | Baseline | Final |
|---|---:|---:|
| Batch adapter branches | 172/258 = 66.67% | **256/276 = 92.75%** |
| Non-GPU core lines | 6,507/7,302 = 89.11% | **6,559/7,323 = 89.57%** |
| Non-GPU core branches | 3,949/4,982 = 79.27% | **4,033/5,000 = 80.66%** |
| Package/update branches | 2,606/3,000 = 86.87% | **2,690/3,018 = 89.13%** |

Arch x86-64: GCC **16.2.1**, Clang/LLVM/format/tidy **23.1.1**, Python **3.14.8**,
CMake/CTest **4.4.4**. Successful commands exit 0 except the strict coverage target,
which correctly exits **1** for the unchanged package/update **90%** goal. Core's
≥80% line / ≥70% branch goals pass within this CPU scope.

| Command | Actual result / log in R |
|---|---|
| `cmake --build --preset coverage -j 4`; `cmake --build --preset coverage --target we_coverage_check` | Clang C++26, **226/226 CTest**, 86.52 s; `verified-coverage-{0,2}.log` |
| `build/coverage/we_core_tests '[batch_admission]'`, with `LLVM_PROFILE_FILE` set to absolute R + `/focused-final-%p.profraw` | **16 cases / 20,633 assertions** on final source; `final-focused.log`, exact environment in `final-focused-commands.json` |
| `cmake --build --preset dev -j 4`; `ctest --preset dev` | GCC C++26 viewer/core, **244/244 CTest**, 51.10 s; `final-dev-{0,1}.log` |
| `cmake --build --preset compat-cxx23 -j 4`; `ctest --preset compat-cxx23` | Explicit GCC C++23 CPU profile, **226/226 CTest**, 71.77 s; `final-cxx23-{0,1}.log` |
| `cmake --build --preset asan -j 4`; `build/asan/we_core_tests '[batch_admission]'` | Native ASan/UBSan/LeakSanitizer **16 cases / 20,633 assertions**, no errors; `final-asan-{0,1}.log` |
| `clang-tidy --warnings-as-errors='*' -p build/coverage tests/unit/batch_pose_admission_tests.cpp src/reconstruction_adapters/batch_poses.cpp` | No user-code diagnostics; `verified-tidy-0.log` |

Native sanitizers retain leak detection outside the sandbox ptrace restriction.
Initial fixture compilation required an explicit nanosecond constructor. Tidy
reported Catch's false-assertion flag cast; equivalent `REQUIRE(!outcome)` avoids
that third-party analyzer path without changing checks or warning policy. One
initial test expected BACKEND_FAILURE for completed-plus-nonzero; the documented
worker lifecycle and decoder require INVALID_DATA, so the expectation was corrected.
Failing logs remain in R. Format/docs/boundaries/diff receipts are `static-commands.json`.

**2,393/2,393 protected files remain byte-identical**: private videos, retained
fits/checkpoints/reconstructions and specification/runbook. No saved job, worker
recipe, schema, environment, GPU execution, threshold, upload or push changes.
Twenty adapter branch outcomes remain uncovered, including late I/O/write/hash/
cancellation paths and guards constrained by earlier admission/protocol checks.
Concurrent hostile directory replacement, real ENOSPC/allocation faults, power-loss
durability, other native platforms and full M1/E1 remain unqualified.
[Progress](../progress.md) gives the next scoped task.
