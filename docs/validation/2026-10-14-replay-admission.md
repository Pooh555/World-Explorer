# Completed-pose replay admission — 2026-10-14 Asia/Hong_Kong

Scope/status: **M1/E1 IN_PROGRESS**. Twelve synthetic public-API cases check replay
admission, retry/lease preservation, bounded events and immutable producer data.
An existing viewer cancellation observer race is repaired. Production, schemas,
recipes, environments and coverage scope are unchanged; no full gate is certified.
Parent `95f60c6`; implementation is the commit containing this report. Cwd:
repository root. `R = artifacts/validation/replay-admission-20261014` retains raw
logs, argv/exits, source/measurement identities and protected-file receipts.

## Contracts and observed failure

[Replay cases](../../tests/unit/completed_poses_tests.cpp) reject invalid producer
profiles, provenance, calibration/count limits, observation/pose dependencies,
transforms and unsupported uncertainty. Failed start permits correction; frame
admission checks metadata, packed RGB leases, unsupported sensors and exact image
memory boundaries. Failed submissions retain every header, RGB/depth lease and
IMU sample. Changed retry headers/bytes or a second operation for an admitted
frame preserve the acknowledged ticket. Authentic and consumed-header retries
produce no duplicate events; corrected nonzero operations reuse their IDs.

Initial events do not release queue slots; observation events do. Incomplete
finish remains recoverable; completed admission closes while authentic retries
remain valid. Cancellation is checked before start, after start/admission and
after finish. Mutable producer aliases can be cleared/released without changing
replayed records. Exactly **9/10 and 108/120** registrations emit LOST for missing
poses; 8/10 is rejected. These are native in-memory diagnostics with declared
synthetic hashes, not capture reconstruction or RGB-to-PNG equivalence evidence.

The first development CTest run passes **198/199**, exit **8** (`final-dev-1.log`):
the existing [owned-job test](../../tests/ui/job_state_tests.cpp) fails
`CHECK(stop_observed)`. Its worker-local callback can unregister when the gate's
stop callback wakes that worker, before the cancelling thread invokes the observer.
`callback-lifetime.cpp` in R reproduces this locally using latches, without sleeps:
the cancelled worker's local observer is not invoked (`callback-probe-1.log`).
The observer now retains the published job token and registers a callback on the
test owner through cancellation/join. Its callback still locks the viewer state
mutex; no expected assertion or production cancellation behavior is weakened.
Captured token/flag storage outlives the owned job, including assertion failures.
The repaired case passes all seven boundaries for **20 consecutive runs**.

## Actual verification

Baseline `build/coverage/coverage/run-krrg731o` and final
`build/coverage/coverage/run-etthc6yq` retain identical hashes for all **87 production
sources**. Final measurement retains both final test hashes, **179 fresh profiles**,
exact argv/exits, build/object identities, LLVM JSON and HTML. `R/baseline.json`
checks source identity. Reviewed scope SHA-256 remains
`712ecbee6339c1a83ebba78acdce3bc7d0db014d0bc23e789f688a6d1a042d1a`.

| Measurement | Baseline | Final |
|---|---:|---:|
| Replay branches | 115/178 = 64.61% | **178/178 = 100%** |
| Non-GPU core lines | 6,437/7,302 = 88.15% | **6,454/7,302 = 88.39%** |
| Non-GPU core branches | 3,741/4,982 = 75.09% | **3,807/4,982 = 76.42%** |
| Package/update branches | 2,399/3,000 = 79.97% | **2,464/3,000 = 82.13%** |

Arch x86-64: GCC **16.2.1**, Clang/LLVM/format/tidy **23.1.1**, Python **3.14.8**,
CMake/CTest **4.4.4**. Successful commands below exit 0 except strict coverage,
which correctly exits **1** for the unchanged package/update **90%** goal. Core's
unchanged ≥80% line / ≥70% branch goals pass within this CPU scope.

| Command | Actual result / log in R |
|---|---|
| `cmake --build --preset coverage -j 4`; `LLVM_PROFILE_FILE=artifacts/validation/replay-admission-20261014/focused-final-%p.profraw build/coverage/we_core_tests '[replay_admission]'`; `cmake --build --preset coverage --target we_coverage_check` | **12 cases / 8,867 assertions**, **181/181 CTest**, 36.93 s; `verified-coverage-{0,1,2}.log` |
| `cmake --build --preset dev -j 4`; `ctest --preset dev` | GCC C++26 viewer/core build, **199/199 CTest**, 37.67 s; `repaired-dev-{0,1}.log` |
| `cmake --build --preset compat-cxx23 -j 4`; `ctest --preset compat-cxx23` | Existing explicit GCC C++23 CPU-only profile, **181/181 CTest**, 44.39 s; `final-cxx23-{0,1}.log` |
| `cmake --build --preset asan -j 4`; `build/asan/we_core_tests '[replay_admission]'` | Native ASan/UBSan/LeakSanitizer **12 cases / 8,867 assertions**, no errors; `final-asan-{0,1}.log` |
| `build/dev/we_viewer_tests '[owned-job]'`; `ctest --preset dev -R '^viewer owned job cancels at checked stages and reopens retained publication$' --repeat until-fail:20` | **2 cases / 235 assertions**; **20/20 repetitions**, 3.30 s; `verified-owned-job-{0,1}.log` |
| `clang-tidy --warnings-as-errors='*' -p build/coverage tests/unit/completed_poses_tests.cpp`; `clang-tidy --warnings-as-errors='*' -p build/dev tests/ui/job_state_tests.cpp` | No user-code diagnostics; `initial-tidy-0.log`, `verified-tidy-ui-0.log` |

Native sanitizer execution retains leak detection outside the documented sandbox
ptrace restriction. It covers replay, not the viewer-state test. Probe compiler/
execution argv and exits are in `callback-probe-commands.json`; format/docs/
boundaries/diff checks are in `static-commands.json`.

**2,393/2,393 protected files remain byte-identical** (`preservation.json`): private
videos, retained reconstructions/fits/checkpoints and specification/runbook.
No reconstruction, GPU run, worker/environment change, upload or push. All mapped
replay branch outcomes are exercised; real allocation failure, other platforms,
reference fixtures and full E1/M1 remain unqualified. No exclusions or thresholds
changed. The next scoped contract task is in [progress](../progress.md).
