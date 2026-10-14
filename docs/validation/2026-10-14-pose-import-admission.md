# Completed-pose file admission — 2026-10-14 Asia/Hong_Kong

Scope/status: **M1/E1 IN_PROGRESS**. Fourteen synthetic public-API cases check
completed pose import, caller-file preservation and immutable retained datasets.
Production, schemas, recipes, environments and coverage scope are unchanged; no
full milestone or gate is certified. Parent `2555f8d`; implementation is the commit
containing this report. Cwd: repository root.
`R = artifacts/validation/pose-import-admission-20261014` retains raw commands,
logs, source/measurement identities and protected-file receipts.

## Exercised contracts

[Import cases](../../tests/unit/completed_pose_import_tests.cpp) exercise invalid
configuration/pre-cancellation; bounded malformed job metadata; source, pose/decode
pointer and output mismatches; missing checked assets; unsupported stage and
canonical coordinate/scale/gravity profiles; malformed inventory/count/quality
metadata; camera identities, dimensions, pixel centers, matrices and coefficients;
pose/frame/backend identities, timestamps, revisions and transform vectors.
Semantically invalid stage data is **rehashed** before admission, reaching record
validation after inventory verification. Original stages are fresh temporary
diagnostics with explicitly synthetic source bytes, not reconstructed captures.

Each rejection checks its exact error, temporary directory entries/file bytes and a
retained dataset's canonical-record/provenance encoding. Only explicit fixture
repair restores the original owned files; the importer never writes repairs.
Reimport then succeeds with the original encoding and unchanged tree, including
unrelated caller data. Native canonical timestamps remain distinct from the
documented nonnegative legacy replay profile.

Positive PINHOLE import retains zero distortion, the half-pixel conversion and
capture-time pose ordering; its acquired dataset survives subsequent source-file
changes. Exactly **9/10 and 108/120** registrations retain unregistered selected
observations, while **8/10** fails. Distinct checked observation paths sharing a
training basename fail without silently collapsing frame identity. No decoder,
solver or fitter is launched; no quality/reconstruction gate is inferred.

## Actual verification

Baseline `build/coverage/coverage/run-etthc6yq` and final
`build/coverage/coverage/run-kp7adq6_` retain identical hashes for all **87 production
sources**. Final measurement saves the exact final fixture/test hash, **193 fresh
profiles**, argv/exits, build/object identities, LLVM JSON and HTML;
`R/baseline.json` verifies source identity. Reviewed scope SHA-256 remains
`712ecbee6339c1a83ebba78acdce3bc7d0db014d0bc23e789f688a6d1a042d1a`.

| Measurement | Baseline | Final |
|---|---:|---:|
| Pose importer branches | 131/210 = 62.38% | **196/210 = 93.33%** |
| Non-GPU core lines | 6,454/7,302 = 88.39% | **6,478/7,302 = 88.72%** |
| Non-GPU core branches | 3,807/4,982 = 76.42% | **3,876/4,982 = 77.80%** |
| Package/update branches | 2,464/3,000 = 82.13% | **2,533/3,000 = 84.43%** |

Arch x86-64: GCC **16.2.1**, Clang/LLVM/format/tidy **23.1.1**, Python **3.14.8**,
CMake/CTest **4.4.4**. Successful commands below exit 0 except strict coverage,
which correctly exits **1** for the unchanged package/update **90%** goal. Core's
unchanged ≥80% line / ≥70% branch goals pass within this CPU scope. Adding the
scoped test file regenerated existing CMake profiles without lowering standards.

| Command | Actual result / log in R |
|---|---|
| `cmake --build --preset coverage -j 4`; `LLVM_PROFILE_FILE=artifacts/validation/pose-import-admission-20261014/focused-final-%p.profraw build/coverage/we_core_tests '[pose_import_admission]'`; `cmake --build --preset coverage --target we_coverage_check` | **14 cases / 36,151 assertions**, **195/195 CTest**, 41.72 s; `verified-coverage-{0,1,2}.log` |
| `cmake --build --preset dev -j 4`; `ctest --preset dev` | GCC C++26 viewer/core build, **213/213 CTest**, 40.84 s; `verified-dev-{0,1}.log` |
| `cmake --build --preset compat-cxx23 -j 4`; `ctest --preset compat-cxx23` | Existing explicit GCC C++23 CPU-only profile, **195/195 CTest**, 49.47 s; `verified-cxx23-{0,1}.log` |
| `cmake --build --preset asan -j 4`; `build/asan/we_core_tests '[pose_import_admission]'` | Native ASan/UBSan/LeakSanitizer **14 cases / 36,151 assertions**, no errors; `verified-asan-{0,1}.log` |
| `clang-tidy --warnings-as-errors='*' -p build/coverage tests/unit/completed_pose_import_tests.cpp` | No user-code diagnostics; `initial-tidy-0.log` |

Native sanitizer execution retains leak detection outside the documented sandbox
ptrace restriction. No fixture/build failure was observed; the strict target's
unmet coverage result remains visible. Exact command receipts accompany the logs;
format/docs/boundaries/diff results are in `static-commands.json`.

**2,393/2,393 protected files remain byte-identical** (`preservation.json`): private
videos, retained reconstructions/fits/checkpoints and specification/runbook.
No reconstruction, GPU execution, environment change, upload or push. Fourteen
importer branch outcomes remain unexercised, covering late I/O/hash mutations and
guards constrained by earlier validation, including nonfinite JSON numbers.
Real allocation/device/ENOSPC failures, other native platforms and full E1/M1
remain unqualified. No exclusions or thresholds changed; [progress](../progress.md)
gives the next scoped contract task.
