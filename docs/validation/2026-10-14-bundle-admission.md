# Scene bundle and PLY admission — 2026-10-14 Asia/Hong_Kong

Scope/status: **M1/E1 IN_PROGRESS**. Thirteen synthetic public-API cases exercise
scene packages and gsplat PLY interchange. Raw PLY byte accounting is repaired;
the strict local CPU coverage goals now pass. Full M1/E1 and GPU/platform gates
remain incomplete. Parent `1d2ca5c`; implementation is the commit containing this
report. Cwd: repository root.
`R = artifacts/validation/bundle-admission-20261014` retains logs, exact argv/exits,
source/test/build identities and protected-file receipts.

## Repair and exercised contracts

The initial byte-budget regression fails for both LF and CRLF (Catch exit **42**,
`R/regression-compiled.log`): a header is accepted with its limit one byte below
its actual size. The reader omitted the magic line and counted normalized text,
allowing carriage returns to escape accounting. It now counts every raw byte,
including line endings, and reads each header line against the remaining total
budget. Vertex/trailing lines retain a 16,384-byte bound, now charged in raw bytes.
The same boundary case passes **16 assertions** after repair (`repair.log`).
[Scene protocol](../protocols/scene.md) records the limits.

[Bundle cases](../../tests/unit/bundle_admission_tests.cpp) cover conventions,
typed identities, paths/links, rehashed provenance/index/chunk corruption, invalid
numeric payloads, aggregate counts and exact encoded byte budgets. Failed reads
check exact errors, the entire caller tree including link targets, and retained
immutable scene hashes. Explicit fixture repair permits the original scene to
load again. Metric scale, maximum provenance field lengths and SH data round-trip.
Rejected native snapshots preserve the complete directory. Publication failures
at chunk/index/provenance/manifest boundaries preserve existing caller entries
and the old scene; only expected immutable assets published before failure may
remain unreferenced. Corrected retry and repeated publication preserve content.

[PLY cases](../../tests/unit/ply_admission_tests.cpp) cover exact LF/CRLF header
limits, carriage-return-heavy input, malformed profiles/counts/properties, duplicate
and unknown columns, truncated/oversized rows, bad numerics and trailing data.
Positive SH degrees **0–3**, reordered columns and optional normals check decoded
scales, quaternion normalization, opacity branches, channel-major to coefficient-
major RGB ordering, unused zero coefficients and canonical world rotation.
Failures preserve input bytes and retained snapshots; open/admission errors remain
explicit. All fixtures are fresh synthetic diagnostics; no worker, saved capture,
scene, reconstruction or GPU operation runs. Windows accounts without symlink
permission explicitly skip linked-asset cases; no cases skipped locally.

## Actual verification

Baseline `build/coverage/coverage/run-7k6ap09k` and final
`build/coverage/coverage/run-0k7zy11u` retain **86/87 identical production source
hashes**; only `bundle.cpp` changes. Final measurement retains **237 fresh profiles**,
both final test hashes, build/object identities and LLVM JSON/HTML.
`R/baseline.json` verifies source identity. The old post-line total check is
replaced by remaining-budget reads, changing the mapped branch count by two.
No paths or files are excluded. Reviewed scope SHA-256 remains
`712ecbee6339c1a83ebba78acdce3bc7d0db014d0bc23e789f688a6d1a042d1a`.

| Measurement | Baseline | Final |
|---|---:|---:|
| Bundle/PLY branches | 233/298 = 78.19% | **280/296 = 94.59%** |
| Non-GPU core lines | 6,559/7,323 = 89.57% | **6,593/7,323 = 90.03%** |
| Non-GPU core branches | 4,033/5,000 = 80.66% | **4,081/4,998 = 81.65%** |
| Package/update branches | 2,690/3,018 = 89.13% | **2,738/3,016 = 90.78%** |

Arch x86-64: GCC **16.2.1**, Clang/LLVM/format/tidy **23.1.1**, Python **3.14.8**,
CMake/CTest **4.4.4**. Every final command below exits **0**, including strict
coverage: unchanged core ≥80% line / ≥70% branch and package/update ≥90% branch
goals pass within this local CPU scope.

| Command | Actual result / log in R |
|---|---|
| `cmake --build --preset coverage -j 4`; `LLVM_PROFILE_FILE=artifacts/validation/bundle-admission-20261014/focused-final-%p.profraw build/coverage/we_core_tests '[bundle_admission]'`; `cmake --build --preset coverage --target we_coverage_check` | Clang C++26, **13 cases / 8,467 assertions**, **239/239 CTest**, 84.77 s; `final-coverage-{0,1,2}.log` |
| `cmake --build --preset dev -j 4`; `ctest --preset dev` | GCC C++26 viewer/core, **257/257 CTest**, 81.82 s; `final-dev-{0,1}.log` |
| `cmake --build --preset compat-cxx23 -j 4`; `ctest --preset compat-cxx23` | Explicit GCC C++23 CPU profile, **239/239 CTest**, 68.09 s; `final-cxx23-{0,1}.log` |
| `cmake --build --preset asan -j 4`; `build/asan/we_core_tests '[bundle_admission]'` | Native ASan/UBSan/LeakSanitizer **13 cases / 8,467 assertions**, no errors; `final-asan-{0,1}.log` |
| `clang-tidy --warnings-as-errors='*' -p build/coverage tests/unit/bundle_admission_tests.cpp tests/unit/ply_admission_tests.cpp src/scene_store/bundle.cpp` | No user-code diagnostics; `final-tidy-0.log` |

Native sanitizers retain leak detection outside the sandbox ptrace restriction.
Initial harness compilation required comparing checked hash values rather than
`expected` objects whose error has no equality. Tidy rejected swappable integer-
patch parameters and inline integer division in a float expectation; bounded
spans and an explicit coefficient count repair the helpers. Failing logs remain
in R. Final format/docs/boundaries/diff receipts are `static-commands.json`.

**2,393/2,393 protected files remain byte-identical**: private videos, saved
fits/checkpoints/reconstructions and specification/runbook. No recipe/schema/
environment/threshold changes, uploads or pushes. Sixteen bundle/PLY branch
outcomes remain uncovered, primarily real write/flush faults, concurrent reads,
extreme sizes and guards constrained by earlier checks. Real ENOSPC/allocation
faults, power-loss durability, fuzz campaigns, other native platforms and full
M1/E1 remain unqualified. [Progress](../progress.md) gives the next executable task.
