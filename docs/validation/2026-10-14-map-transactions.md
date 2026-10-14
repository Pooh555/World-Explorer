# Durable-map transaction admission — 2026-10-14 Asia/Hong_Kong

Scope/status: **M1/E1 IN_PROGRESS**. Twelve synthetic public-API cases exercise
transaction rejection, immutable snapshots, durable retry receipts and correction
invalidation. Production, schemas, worker environments, recipes and coverage scope
are unchanged. No complete milestone or gate is certified. Parent `f0eb313`;
implementation is the commit containing this report. Cwd: repository root.
`R = artifacts/validation/map-transactions-20261014` retains commands, logs,
measurement/source identities and protected-file receipts.

## Exercised contracts

[Transaction cases](../../tests/unit/persistent_map_tests.cpp) exercise 56 variants:
operation/epoch/base/new-revision admission; each independent transaction-array
budget; invalid/duplicate/immutable calibrations; image provenance and observation
identity/dependency/timestamps; invalid/duplicate alignments; missing pose
dependencies, nonrigid transforms, revision/time disagreement, duplicate changes,
nonfinite/asymmetric/indefinite covariance, unknown tracking and world-transform
overflow; missing/duplicate/deletion-versus-pose collisions; and aggregate frame,
calibration and submap budgets. Corrected inputs retain the operation ID except
the deliberately invalid zero ID. Exact record boundaries remain admissible.

Each variant checks the exact error, acquired snapshot identity, and complete
temporary tree (manifest, immutable assets and an external synthetic producer
file). Held writer locks remain owned and inventoried without a second read
handle. The acknowledged first transaction still retries; the corrected operation
then succeeds **in the same live writer**, and both receipts replay after reopen
without changing any files. Re-encoding the retained producer snapshot verifies
it survived the correction byte-identically.

Separate cases verify that alignment corrections invalidate only their dependent
poses, the next deletion replaces the invalidation set, and old snapshots survive.
A rehashed synthetic checkpoint admits revision UINT64_MAX, then rejects wrap to
zero; explicit epoch reset permits revision one and durable retry. An obstructed
asset directory rejects reset while preserving records/receipts and caller data;
only explicit fixture repair permits that reset. [Memory-store cases](../../tests/unit/contracts_tests.cpp)
reject zero/oversized retry windows and retain the input for a valid store.

## Verification

Baseline `build/coverage/coverage/run-0hg7e901` and final
`build/coverage/coverage/run-krrg731o` have identical hashes for all **87 production
sources**. The final measurement retains exact argv/exits, final test/source/build/
object identities, fresh profiles, LLVM JSON and HTML; `R/baseline.json` verifies
the final test hashes. Scope SHA-256 remains
`712ecbee6339c1a83ebba78acdce3bc7d0db014d0bc23e789f688a6d1a042d1a`.

| Measurement | Baseline | Final |
|---|---:|---:|
| Map-store branches | 162/208 = 77.88% | **198/208 = 95.19%** |
| Non-GPU core lines | 6,421/7,302 = 87.93% | **6,437/7,302 = 88.15%** |
| Non-GPU core branches | 3,704/4,982 = 74.35% | **3,741/4,982 = 75.09%** |
| Package/update branches | 2,363/3,000 = 78.77% | **2,399/3,000 = 79.97%** |

Arch x86-64: GCC **16.2.1**, Clang/LLVM/format/tidy **23.1.1**, Python **3.14.8**,
CMake/CTest **4.4.4**. Final commands/results below are captured in R's
`verified-*-commands.json`; successful checks exit 0 except the strict coverage
target, which correctly exits **1** because package/update coverage remains below
the unchanged **90%** target. Core's unchanged ≥80% line / ≥70% branch goals pass
only within this local CPU measurement.

| Command | Actual result / log in R |
|---|---|
| `cmake --build --preset coverage -j 4`; `LLVM_PROFILE_FILE=artifacts/validation/map-transactions-20261014/focused-final-%p.profraw build/coverage/we_core_tests '[map_transaction_admission]'`; `cmake --build --preset coverage --target we_coverage_check` | **12 cases / 4,168 assertions**, **169/169 CTest**, 46.72 s; `verified-coverage-{0,1,2}.log` |
| `cmake --build --preset dev -j 4`; `ctest --preset dev` | GCC C++26 viewer/core build, **187/187 CTest**, 47.49 s; `verified-dev-{0,1}.log` |
| `cmake --build --preset compat-cxx23 -j 4`; `ctest --preset compat-cxx23` | Existing explicit GCC C++23 CPU-only profile, **169/169 CTest**; `verified-cxx23-{0,1}.log` |
| `cmake --build --preset asan -j 4`; `build/asan/we_core_tests '[map_transaction_admission]'` | Native ASan/UBSan/LeakSanitizer **12 cases / 4,168 assertions**, no errors; `verified-asan-{0,1}.log` |
| `clang-tidy --warnings-as-errors='*' -p build/coverage tests/unit/persistent_map_tests.cpp tests/unit/contracts_tests.cpp` | No user-code diagnostics; `verified-tidy-0.log` |

Earlier fixture lint failures (`tidy.log`, `tidy-final.log`, `tidy-0.log`, exit 1)
identify unchecked optional access and an out-of-range enum cast. Explicit optional
guards, independent covariance mutation and construction from underlying byte
representations repair the fixtures without suppressions. Native sanitizer
execution was required after the sandbox build's Catch discovery hit
LeakSanitizer's ptrace restriction (`build-asan.log`, exit 1); leak checks stay
enabled. Earlier intermediate measurements/checks remain in R; the final logs
above correspond to the final source. Format, docs, boundaries and diff checks
are recorded in `static-commands.json`.

**2,393/2,393 protected files remain byte-identical** (`preservation.json`): private
videos, retained reconstruction stages/fits/checkpoints and specification/runbook.
No reconstruction, GPU run, environment change, upload or push. Local synthetic
CPU evidence does not qualify native Windows/macOS, allocation/device/ENOSPC/
power-loss failure or late cancellation. Remaining map-store branch gaps include
late cancellation, post-rename ambiguous durability handling, and initial-import
guards dominated by earlier admission. No exclusions or threshold changes were
added. The next scoped contract task is in [progress](../progress.md).
