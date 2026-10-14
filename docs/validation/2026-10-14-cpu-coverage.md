# Scoped CPU contract coverage — 2026-10-14 Asia/Hong_Kong

Scope/status: **M1/E1/E2 IN_PROGRESS**. Local non-GPU core line/branch coverage
meets its numeric subtargets; package/update branches remain below **90%**.
No milestone or complete E1 gate is certified. Parent `a9012ca`; implementation
is the commit containing this report. Cwd: repository root. Raw evidence:
`R = artifacts/validation/cpu-coverage-20261014`; `measurement-runs.json` identifies
the retained LLVM run directories, summary hashes and exact command receipts.

## Behavior and reviewed scope

The isolated `coverage` preset uses Clang **23.1.1**, C++26, atomic LLVM counters,
no viewer and no sanitizer. All **40** production CPU translation units are
instrumented; **144** vendor units are not (`instrumentation.json`). Reports load
the test binary and all seven library archives, preserving unexecuted objects in
the denominator. Each run owns fresh profiles; Catch discovery and previous runs
are excluded. Compiler/profiler major mismatch, missing translation units,
duplicate files, invalid counters or empty groups abort measurement.

The [protocol](../protocols/coverage.md) and checked-in
[scope](../../tools/coverage_scope.json) define both groups. Package/update uses
whole scene/map/adapter modules and the renderer update sink, including operational
code. Vendor/generated/test code and executable UI/entry points are excluded from
this core-library measurement. **No compiled platform file or error path is
excluded**. Inactive preprocessor branches, unemitted header/template code, native
UI, GPU behavior and other operating systems require separate qualification.
LLVM true/false branch outcomes and line counts are summed across files.

| Group | Baseline covered/total | After regressions covered/total | Target |
|---|---:|---:|---:|
| Core lines | 6,243/7,296 = **85.57%** | 6,324/7,301 = **86.62%** | ≥80% |
| Core branches | 3,343/4,974 = **67.21%** | 3,490/4,980 = **70.08%** | ≥70% |
| Package/update lines | 3,982/4,583 = **86.89%** | 4,063/4,588 = **88.56%** | Informational |
| Package/update branches | 2,018/2,992 = **67.45%** | 2,150/2,998 = **71.71%** | ≥90% |

The small denominator increase is the added enum validation. Report/check targets
use identical fresh-test measurement; the strict target exits **1** because the
package/update goal is unmet. Intermediate 69.90%/69.98% core results and strict
failures remain in R, without threshold or benchmark changes.

## Regressions and repairs

The baseline selected hostile scene fields, failed transactions, replay exhaustion,
renderer admission/acknowledgements/budgets/receipt eviction and package parsing.
Thirteen synthetic C++ cases cover these contracts and invalid optical intrinsics.
Corrupt index/chunk cases recompute checksums, reaching parser/semantic validation
rather than merely exercising checksum rejection.

Two initial failures (`scene-before.log`, exit **42**, 2/4 cases failed) proved
unknown scale status was admitted and unknown operation kind was interpreted as
geometry invalidation. Both now return `INVALID_DATA` before publication. Rejected
transactions preserve snapshot identity and leave operation IDs reusable; measured
scale roundtrips remain valid. Lint cleanup retains unsigned hashing arithmetic,
borrows the reset snapshot while copying owned state, and uses typed fixture IDs
and checked optional extraction. No warning policy is weakened.

## Actual checks and preservation

Arch Linux x86-64, GCC **16.2.1**, Clang/LLVM/format/tidy **23.1.1**, Python
**3.14.8**, CMake/CTest **4.4.4**. All successful checks below exit 0. Logs are in R;
LLVM run receipts contain argv, cwd, exits, source/test/build/object hashes and HTML.

| Command | Observed result / raw log |
|---|---|
| `cmake --preset coverage`; `cmake --build --preset coverage -j 4` | Configure/build pass; `configure.log`, `final-build.log` |
| `cmake --build --preset coverage --target we_coverage_report` | Baseline **104/104** CTest, 102 fresh profiles, 53 mapped files; `baseline-target.log` |
| `cmake --build --preset coverage --target we_coverage_check` | **117/117** CTest, 115 fresh profiles, 53 mapped files; expected exit **1** for coverage goal; `final-measurement.log`, `final-summary.json` |
| `cmake --preset dev`; `cmake --build --preset dev -j 4`; `ctest --preset dev` | GCC C++26 viewer/core build, **135/135** CTest; `dev-configure.log`, `dev-final-build.log`, `ctest-dev-final.log` |
| `build/dev/we_core_tests '[validation]'` | Final focused **13 cases / 1,119 assertions**; `dev-final-focused.log` |
| `cmake --preset asan`; `cmake --build --preset asan -j 4`; `build/asan/we_core_tests '[validation]'` | Native GCC C++26 ASan/UBSan/LeakSanitizer: **13 cases / 1,119 assertions**, no errors; `asan-configure.log`, `asan-final-build.log`, `asan-final-focused.log` |
| `python3 -m unittest discover -s tests/tools -p coverage_test.py -v` | **5/5** denominator/threshold integrity checks, also registered in CTest |

Four invalid coverage configurations (GCC, viewer, sanitizer and tests disabled)
are rejected before dependency setup (`rejection-commands.json`). Format dry-run,
tidy with warnings as errors on five touched C++ units, documentation, boundaries
and diff checks pass (`static-commands.json`). Initial lint findings and their
repairs are retained in `tidy*.log`. Sandbox ASan discovery failed because
LeakSanitizer cannot inspect threads under ptrace (`asan-build.log`); native
process access succeeds with leak detection still enabled. No sanitizer option
was disabled, and this is a focused sanitizer result, not the full suite.

Read-only SHA-256 comparison confirms **2,393/2,393 protected files unchanged**,
including all three videos, fit/checkpoint, retained stages and specification/
runbook (`preservation.json`). No decode, solver, optimizer, environment install,
native GPU run, upload or push occurred. Worker-envelope admission and durable-map
error paths remain major branch gaps; current work lives in [progress](../progress.md).
