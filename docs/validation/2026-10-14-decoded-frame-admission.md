# Decoded-frame source admission — 2026-10-14 Asia/Hong_Kong

Scope/status: **M1/E1 IN_PROGRESS**. Fifteen synthetic public-API cases exercise
decoded-stage admission, exact rejection errors and replay state/lease preservation.
Production, schemas, recipes, environments and coverage scope remain unchanged;
no full milestone or gate is certified. Parent `8ee7667`; implementation is the
commit containing this report. Cwd: repository root.
`R = artifacts/validation/decoded-frame-admission-20261014` retains exact command
receipts, logs, fixture/test identities and protected-file hashes.

## Exercised contracts

[Admission tests](../../tests/unit/decoded_frame_admission_tests.cpp) cover invalid
configuration/pre-cancellation; bounded, malformed, missing and linked completion
metadata; inventory counts/types/duplicates/hashes; metadata receipts; unsupported
profiles and native journal provenance; frame counts/identities; timestamp syntax,
signedness, arithmetic, duration and monotonicity; unsafe/missing/linked image paths;
inventory/PNG headers, calibration dimensions and exact RGB budgets. Semantic
metadata mutations are **rehashed** before admission. Each rejection checks both
open and inspection errors, every caller directory entry/file byte/link target,
and a retained immutable RGB lease. Explicit repair of the fresh fixture then
allows inspection and the retained source's next frame with stable identity.

Failed reads and seeks preserve the pending reset/epoch and next frame. Tests
exercise completion/image mutations, short/oversized/nonzero decoder output,
image mutation/removal during decode, repeated EOF and invalid seeks. A local
diagnostic decoder's startup marker synchronizes in-flight cancellation; retry
delivers the same next frame, and old leases survive seeks and source destruction.
Three controlled decoders deliberately mutate their selected input; checks allow
exactly that mutation and require every other caller file to remain unchanged.

Inspection admits exactly **120 selected / 36,000 source frames**, the last source
index **35,999**, **1,200 seconds**, **4,096 inventory entries**, and dimension/byte
boundaries without launching a decoder. Dimension-limit fixtures patch synthetic
PNG headers; they do not certify pixel decoding. Legacy stages retain their
documented 16-character source-prefix limitation; native journals check the full
source SHA. No source video, solver or fitter runs. Windows accounts unable to
create symlinks explicitly skip those generated cases; no local cases skipped.

## Actual verification

Baseline `build/coverage/coverage/run-kp7adq6_` and final
`build/coverage/coverage/run-0khgqzsr` retain identical hashes for all **87 production
sources**. Final measurement retains the exact final test hash, **208 fresh
profiles**, LLVM JSON/HTML, argv/exits and build/object identities;
`R/baseline.json` verifies source identity. Scope SHA-256 remains
`712ecbee6339c1a83ebba78acdce3bc7d0db014d0bc23e789f688a6d1a042d1a`.

| Measurement | Baseline | Final |
|---|---:|---:|
| Decoded-source branches | 155/238 = 65.13% | **228/238 = 95.80%** |
| Non-GPU core lines | 6,478/7,302 = 88.72% | **6,507/7,302 = 89.11%** |
| Non-GPU core branches | 3,876/4,982 = 77.80% | **3,949/4,982 = 79.27%** |
| Package/update branches | 2,533/3,000 = 84.43% | **2,606/3,000 = 86.87%** |

Arch x86-64: GCC **16.2.1**, Clang/LLVM/format/tidy **23.1.1**, Python **3.14.8**,
CMake/CTest **4.4.4**. Commands below exit 0 except strict coverage, which exits
**1** for the unchanged package/update **90%** goal. Core's unchanged ≥80% line /
≥70% branch goals pass within this CPU scope. Existing presets retain C++26 or
explicit C++23; adding the test file regenerated their CMake test inventory.

| Command | Actual result / log in R |
|---|---|
| `cmake --build --preset coverage -j 4`; `LLVM_PROFILE_FILE=artifacts/validation/decoded-frame-admission-20261014/focused-final-%p.profraw build/coverage/we_core_tests '[decoded_admission]'`; `cmake --build --preset coverage --target we_coverage_check` | **15 cases / 24,056 assertions**, **210/210 CTest**, 60.26 s; `final-coverage-{0,1,2}.log` |
| `cmake --build --preset dev -j 4`; `ctest --preset dev` | GCC C++26 viewer/core build, **228/228 CTest**, 60.19 s; `final-dev-{0,1}.log` |
| `cmake --build --preset compat-cxx23 -j 4`; `ctest --preset compat-cxx23` | Explicit GCC C++23 CPU profile, **210/210 CTest**, 57.38 s; `final-cxx23-{0,1}.log` |
| `cmake --build --preset asan -j 4`; `build/asan/we_core_tests '[decoded_admission]'` | Native ASan/UBSan/LeakSanitizer **15 cases / 24,056 assertions**, no errors; `final-asan-{0,1}.log` |
| `clang-tidy --warnings-as-errors='*' -p build/coverage tests/unit/decoded_frame_admission_tests.cpp` | No user-code diagnostics; `final-tidy-0.log` |

Native sanitizer execution retains leak detection outside the sandbox ptrace
restriction. Initial tidy rejected swappable helper dimensions, then a copied
stop-token parameter; the helper now takes a dimension pair and observer a const
reference. Failing logs remain in R. Final revision was rebuilt and rechecked;
no runtime test failure occurred. Format/docs/boundaries/diff results reside in
`static-commands.json`.

**2,393/2,393 protected files remain byte-identical** (`preservation.json`): private
videos, retained reconstructions/fits/checkpoints and specification/runbook.
No reconstruction/GPU rerun, environment change, upload or push. Ten decoded-source
branch outcomes remain uncovered: late I/O/admission races, guards constrained by
earlier validation, platform path behavior and epoch exhaustion. Real allocation,
device/ENOSPC faults, native other-platform execution and full M1/E1 remain
unqualified. No exclusions or thresholds changed; [progress](../progress.md) gives
the next task.
