# Windows batch launch-marker repair — 2026-10-14 Asia/Hong_Kong

Scope/status: **M1/E1/E2 IN_PROGRESS**. The supplied `windows-2025` log reports
**236/238** explicit C++23 CPU tests passing, CTest exit **1**, in **213.40 s**.
The supplied excerpt omits its commit and compiler versions.
Native executables ran; the failures are two test-fixture byte comparisons, not
compiler errors. Native Windows confirmation of this repair remains pending.
Parent `3dd5346`; implementation is the commit containing this report. Cwd:
repository root. `R = artifacts/validation/windows-batch-marker-20261014` retains
the supplied log, its SHA-256, local commands/results and preservation receipts.

## Cause and repair

The diagnostic Python worker appended `launch\n` through text-mode `Path.open('a')`.
On Windows this writes CRLF. The C++ preservation observer reads binary bytes and
requires exactly `file:launch\n`, so the displayed strings look alike while their
bytes differ. Nine worker-failure variants and one resumed-publication variant
fail this check. The supplied worker integration, bundle/PLY cases and remaining
CPU tests pass within that run's scope.

[Fixture repair](../../tests/unit/batch_pose_admission_tests.cpp) changes only the
diagnostic marker to binary append and an explicit byte literal. Exact marker,
single-launch, receipt and complete caller-tree comparisons remain unchanged;
caller bytes are never normalized. Both affected cases now run with normal text
behavior and a local worker shim that forces CRLF for text writes, leaving binary
writes untouched. This reproduces the platform difference on Linux without
claiming native Windows execution.

Before the marker repair, that shim reproduces **both failed cases / all ten
assertion failures** (Catch exit **42**, `R/regression-1.log`). After repair,
**2 cases / 6,077 assertions** pass in both newline modes. No production code,
protocol, workflow, worker recipe, environment or benchmark threshold changes.
All **87 production source hashes** match the preceding coverage measurement
`build/coverage/coverage/run-0k7zy11u` (`production-identity.json`). Coverage was
not remeasured for this fixture-only repair; that report retains its original
90.03% core line / 81.65% core branch / 90.78% package-update branch scope.

## Actual local verification

Arch x86-64: GCC **16.2.1**, Clang/format/tidy **23.1.1**, Python **3.14.8**,
CMake/CTest **4.4.4**. All final commands below exit **0**. Structured receipts
retain argv, environment overrides, elapsed time, cwd and log paths.

| Command | Actual result / log in R |
|---|---|
| `cmake --build --preset coverage -j 4`; `LLVM_PROFILE_FILE=artifacts/validation/windows-batch-marker-20261014/focused-final-%p.profraw build/coverage/we_core_tests '[batch_marker]'` | Instrumented Clang C++26, **2 cases / 6,077 assertions**; `final-focused-{0,1}.log` |
| `cmake --build --preset dev -j 4`; `ctest --preset dev` | GCC C++26 viewer/core, **257/257 CTest**, 66.73 s; `final-dev-{0,1}.log` |
| `cmake --build --preset compat-cxx23 -j 4`; `ctest --preset compat-cxx23` | GCC explicit C++23 CPU profile, **239/239 CTest**, 67.57 s; `final-cxx23-{0,1}.log` |
| `cmake --build --preset asan -j 4`; `build/asan/we_core_tests '[batch_marker]'` | Native ASan/UBSan/LeakSanitizer **2 cases / 6,077 assertions**, no errors; `final-asan-{0,1}.log` |
| `clang-tidy --warnings-as-errors='*' -p build/coverage tests/unit/batch_pose_admission_tests.cpp` | No user-code diagnostics; `final-tidy-0.log` |

Native sanitizers retain leak detection outside the sandbox ptrace restriction.
Format/docs/boundaries/diff receipts are `static-commands.json`. The local CPU
inventory includes the POSIX-only closed-stdin worker case, explaining **239**
versus the supplied Windows **238** tests.

**2,393/2,393 protected files remain byte-identical**: private videos, retained
fits/checkpoints/reconstructions and specification/runbook. No reconstruction,
GPU run, upload or push. The Windows job must be rerun against the repair before
claiming its complete CPU suite passes. Native viewer/release qualification and
full M1/E1/E2 remain incomplete; [progress](../progress.md) retains parser fuzzing
as the next implementation task.
