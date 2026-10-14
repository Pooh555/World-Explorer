# Durable-map admission — 2026-10-14 Asia/Hong_Kong

Scope/status: **M1/E1 IN_PROGRESS**. Ten new synthetic public-API cases expose and
repair revision-zero dependency admission. No complete milestone or gate is
certified. Parent `645075f`; implementation is the commit containing this report.
Cwd: repository root. `R = artifacts/validation/map-admission-20261014` retains
logs, source/measurement identities and read-only preservation receipts.

## Behavior and regression

Checkpoint decoding validates records before restoring the initial canonical
dependency. An empty revision-zero checkpoint could therefore retain that
dependency, contradicting reset's empty-state invariant. A structurally valid,
rehashed synthetic checkpoint reproduces acceptance: `before.log` has **4/5 cases
passing, 927/928 assertions**, focused executable exit 42. Recovery now rejects
the dependency at revision zero before reading its asset. This is the only
production change; schemas, recipes, environments and coverage scope are unchanged.

The [admission cases](../../tests/unit/persistent_map_tests.cpp) exercise 14 invalid
configurations; 16 manifest type/schema/hash/byte/depth mutations; seven receipt/
coordinate mutations; revision-zero, invalid-hash and missing dependencies; 26
record mutations covering duplicate IDs, timestamps, calibration/pose links,
transforms and covariance; five missing/occupied initialization states; three
retained-asset failures; calibration/observation/submap budgets; independent pose/
invalidation budgets; and valid antipodal world quaternions.

Malformed records are rehashed so they reach semantic admission. Rejection
preserves the supplied manifest and original acquired snapshot. Restoring the
authentic manifest/dependency recovers the committed map and operation retries.
Occupied caller files survive initialization rejection; an obstructed assets
directory fails mutation without publishing RAM/manifest/receipts, then succeeds
after repair. All fixtures are temporary and synthetic; arbitrary scale remains
explicit. These cases do not certify real reconstruction or power-loss behavior.

## Measurement and actual checks

Baseline `build/coverage/coverage/run-95oe9cro` matched production source hashes
before editing (`baseline.json`). Final `build/coverage/coverage/run-c5d4ivg5`
retains exact argv/exits, source/test/build/object hashes, 141 fresh profiles,
LLVM JSON and HTML. `measurement-runs.json` and `final-summary.json` index it.

| Measurement | Baseline | Final |
|---|---:|---:|
| Map persistence branches | 148/226 = 65.49% | **190/228 = 83.33%** |
| Map codec branches | 173/236 = 73.31% | **208/236 = 88.14%** |
| Non-GPU core lines | 6,355/7,301 = 87.04% | **6,392/7,302 = 87.54%** |
| Non-GPU core branches | 3,566/4,980 = 71.61% | **3,646/4,982 = 73.18%** |
| Package/update branches | 2,226/2,998 = 74.25% | **2,305/3,000 = 76.83%** |

The guard adds one instrumented line and two branch outcomes. Scope hash remains
`712ecbee6339c1a83ebba78acdce3bc7d0db014d0bc23e789f688a6d1a042d1a`;
≥80% core lines / ≥70% core branches / ≥90% package-update branches are unchanged.
The strict target correctly exits **1** because the last goal remains unmet.
Arch x86-64: Clang/LLVM/format/tidy **23.1.1**, GCC **16.2.1**, Python **3.14.8**,
CMake/CTest **4.4.4**. Successful commands below exit 0.

| Command | Result / raw log in R |
|---|---|
| `cmake --build --preset coverage -j 4` | C++26 build passes; `build-complete.log` |
| `LLVM_PROFILE_FILE=artifacts/validation/map-admission-20261014/focused-%p.profraw build/coverage/we_core_tests '[map_admission]'` | **10 cases / 1,105 assertions**; `focused.log` |
| `cmake --build --preset coverage --target we_coverage_check` | **143/143 CTest**, 42.17 s; expected target exit 1; `coverage.log` |
| `cmake --build --preset dev -j 4`; `ctest --preset dev` | GCC C++26 viewer/core build; **161/161 CTest**, 37.60 s; `build-dev.log`, `ctest-dev.log` |
| `cmake --build --preset asan -j 4`; `build/asan/we_core_tests '[map_admission]'` | Native ASan/UBSan/LeakSanitizer **10 cases / 1,105 assertions**, no errors; `build-asan.log`, `asan.log` |
| `clang-tidy --warnings-as-errors='*' -p build/coverage src/map_store/map_persistence.cpp tests/unit/persistent_map_tests.cpp` | No user-code diagnostics; `tidy-final.log` |

Native sanitizer execution ran outside the sandbox because sandbox ptrace
prevents LeakSanitizer discovery; leak checks remain enabled.
Initial builds/lint rejected partial designated initializers, JSON/string
comparison, arithmetic widening and unchecked optional access. Explicit assignment,
typed extraction/arithmetic and an emplacement reference correct the fixtures;
failing `build-*.log` and `tidy-first.log`/`tidy-after.log` remain. No warning was
weakened. Format, documentation, include boundaries and diff checks pass;
`static-commands.json` records exact commands/exits.

Read-only hashing confirms **2,393/2,393 protected files unchanged**, including
videos, retained stages, fit/checkpoint and specification/runbook (`preservation.json`).
No user reconstruction, GPU run, worker/environment change, upload or push.
Other native platforms, real ENOSPC/device/power-loss failures, cancellation at
publication boundaries and full gate qualification remain unverified. Canonical
producer snapshot admission is the next scoped task in [progress](../progress.md).
