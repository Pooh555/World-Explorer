# Canonical producer admission — 2026-10-14 Asia/Hong_Kong

Scope/status: **M1/E1 IN_PROGRESS**. Thirteen new synthetic public-API cases verify
canonical snapshot/file admission, bounded encoding and retry preservation.
Production, schemas, recipes, environments and coverage scope are unchanged;
no complete milestone or gate is certified. Parent `5e5ef67`; implementation is
the commit containing this report. Cwd: repository root.
`R = artifacts/validation/canonical-admission-20261014` retains raw logs, source/
measurement identities and read-only preservation receipts.

## Exercised contracts

[Persistent import cases](../../tests/unit/persistent_map_tests.cpp) reject eleven
producer identity/dependency/transform mutations, ten missing-record/provenance/
scale variants, advanced epochs/revisions and invalidated frames, pre-cancelled
memory/file imports, three record budgets, unsupported memory stores, operation
zero and occupied dependency paths. Six file variants cover missing/directory/
empty/truncated sources, wrong checksums and semantically invalid rehashed bytes.
Map keys must match record IDs; local camera transforms must remain rigid and
their composed world transforms finite. Invalid source data remains rejected
even when its operation ID was already acknowledged for authentic data.

Tree identities hash every regular file and retain directory entries. Rejected
producer/file admission preserves the committed tree and acquired snapshot;
authentic imports then reuse the operation ID. Reopening preserves the authentic
receipt despite a rejected malformed retry. Valid advanced input is re-encoded
byte-identically after rejection. Lower map budgets and operation-zero rejection
can leave a **complete unreferenced dependency**, without publishing a checkpoint
or receipt; budget repair and reopen permit the authentic import/retry. Occupied
caller entries survive until the fixture explicitly removes them.

[Codec cases](../../tests/unit/pose_asset_tests.cpp) reject eight invalid limit
configurations through encode/decode/read/inspection; independently bound
calibrations, observations, poses, alignments and invalidations; sweep sub-output
byte budgets in 64-byte increments and check exact/one-byte-short admission;
reject each required/optional short or nonhex provenance hash after rehashing;
and inspect invalidations with an absent journal. Failed encoding creates no
destination/staging entries. Exact-limit publication/read succeeds with the
original hash. All stores/assets are temporary and synthetic; arbitrary scale,
unknown gravity and no invented geometry remain explicit.

## Actual checks and measurement

Baseline `build/coverage/coverage/run-c5d4ivg5` matches all 87 retained production
source hashes (`baseline.json`). Final `build/coverage/coverage/run-0hg7e901` retains exact argv/exits,
source/test/build/object hashes, 154 fresh profiles, LLVM JSON and HTML.
`measurement-runs.json` also identifies the preceding 1,172-assertion measurement;
the final cases add acknowledged-retry preservation and pass **1,271 assertions**.

| Measurement | Baseline | Final |
|---|---:|---:|
| Map codec branches | 208/236 = 88.14% | **222/236 = 94.07%** |
| Pose asset branches | 88/118 = 74.58% | **115/118 = 97.46%** |
| Map store branches | 150/208 = 72.12% | **162/208 = 77.88%** |
| Non-GPU core lines | 6,392/7,302 = 87.54% | **6,421/7,302 = 87.93%** |
| Non-GPU core branches | 3,646/4,982 = 73.18% | **3,704/4,982 = 74.35%** |
| Package/update branches | 2,305/3,000 = 76.83% | **2,363/3,000 = 78.77%** |

Denominators, reviewed scope and ≥80%/≥70%/≥90% thresholds are unchanged. Scope
SHA-256: `712ecbee6339c1a83ebba78acdce3bc7d0db014d0bc23e789f688a6d1a042d1a`.
The strict target correctly exits **1** for unmet package/update coverage.
Arch x86-64: Clang/LLVM/format/tidy **23.1.1**, GCC **16.2.1**, Python **3.14.8**,
CMake/CTest **4.4.4**. Successful checks below exit 0.

| Command | Result / raw log in R |
|---|---|
| `cmake --build --preset coverage -j 4`; `LLVM_PROFILE_FILE=artifacts/validation/canonical-admission-20261014/receipt-%p.profraw build/coverage/we_core_tests '[canonical_admission]'` | C++26 build; **13 cases / 1,271 assertions**; `build-receipt.log`, `focused-receipt.log` |
| `cmake --build --preset coverage --target we_coverage_check` | **156/156 CTest**, 39.06 s; expected target exit 1; `coverage-final.log`, `final-summary.json` |
| `cmake --build --preset dev -j 4`; `ctest --preset dev` | GCC C++26 viewer/core build, **174/174 CTest**, 37.95 s; `build-dev-final.log`, `ctest-dev-final.log` |
| `cmake --build --preset asan -j 4`; `build/asan/we_core_tests '[canonical_admission]'` | Native ASan/UBSan/LeakSanitizer **13 cases / 1,271 assertions**, no errors; `build-asan-final.log`, `asan-final.log` |
| `clang-tidy --warnings-as-errors='*' -p build/coverage tests/unit/persistent_map_tests.cpp tests/unit/pose_asset_tests.cpp` | No user-code diagnostics; `tidy-receipt.log` |

Native sanitizer execution ran outside the sandbox because ptrace restrictions
prevent LeakSanitizer discovery; leak checks remain enabled. Initial codec build
compared `std::expected` values whose error type lacks equality (`build-codec.log`,
exit 1); checking success and comparing extracted hashes corrects the fixtures.
No production failure or weakened warning. Earlier successful checks remain in R;
final logs above correspond to the final source. Format, docs, include boundaries
and diff checks pass (`static-commands.json`).

**2,393/2,393 protected files remain byte-identical**, including private videos,
retained stages/fits/checkpoints and specification/runbook (`preservation.json`).
No reconstruction, GPU execution, worker/environment change, upload or push.
Late cancellation, real allocation/device/ENOSPC/power-loss failures, other native
platforms and full gates remain unqualified. Remaining codec guards include outer
API-dominated checks; no exclusions were added. Transaction admission is the next
scoped task in [progress](../progress.md).
