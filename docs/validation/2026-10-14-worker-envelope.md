# Worker envelope and completion admission — 2026-10-14 Asia/Hong_Kong

Scope/status: **M1/E1 IN_PROGRESS**. Sixteen new synthetic cases and repaired
Catch section discovery strengthen the existing protocol's evidence. Production
code, wire schemas, recipes, worker environments and coverage scope are unchanged.
No complete milestone or E1 gate is certified. Parent `a0742ca`; implementation
is the commit containing this report. Cwd: repository root.
`R = artifacts/validation/worker-envelope-20261014` holds logs, exact command
receipts, source/fixture identities and preservation hashes.

## Covered contracts and test repair

The new [validation cases](../../tests/unit/worker_validation_tests.cpp) exercise
version/session/operation/sequence rejection; handshake limits and capabilities;
empty, excessive and malformed UTF-8 strings; START/CANCEL reply fields;
pre-START progress/completion/failure rules; null/unknown payloads; completion
path/hash/size metadata; short framing and incomplete EOF; coalesced messages;
exact text/argument budgets; valid Unicode; and the 10,000-event cap. Rejection
must publish no event and leave the stream permanently invalid. The last valid
terminal has sequence 9,999; sequence 10,000 is rejected. Valid command retries
and duplicate acknowledgements retain their documented semantics.

Unknown tags are injected into serialized bytes instead of constructing invalid
C++ enum values. Unicode path fixtures use native paths and explicit UTF-8.
All diagnostics identify themselves as synthetic; these are contract checks,
not real reconstruction or quality evidence.

Coverage exposed an existing false confidence: the first `SECTION` in the session/
acknowledgement testcase returned before Catch discovered its sibling sections.
The original testcase passed with **one assertion** (`legacy-before.log`). Each
section now owns its setup and reaches the other section declarations; all five
execute with **14 assertions** (`legacy-after.log`). No production behavior changes.

A native subprocess fixture emits structurally valid completion references to
actual temporary files. The valid file produces exactly one completion notification.
Missing files, directories, wrong declared size/hash and contents exceeding the
declared size produce the specific regular-file or checksum/size error and **zero
completion notifications**. This reaches client admission rather than depending
on the Python writer's own rejection. Invalid START input also fails before a
nonexistent fixture script can launch.

## Measurement and actual checks

The unchanged [scope](../../tools/coverage_scope.json) includes every compiled CPU
library and platform path. Baseline is the previous source-matched retained run
`build/coverage/coverage/run-kqrvy_fa`; `baseline.json` checks its production hashes.
The final run is `build/coverage/coverage/run-95oe9cro`; its receipts retain source,
test/build/object hashes, argv/exits, 131 fresh profiles, LLVM JSON and HTML.
`measurement-runs.json` indexes both intermediate and final measurements.

| Measurement | Baseline | Final |
|---|---:|---:|
| Worker protocol lines | 385/433 = 88.91% | **416/433 = 96.07%** |
| Worker protocol branches | 202/286 = 70.63% | **278/286 = 97.20%** |
| Non-GPU core lines | 6,324/7,301 = 86.62% | **6,355/7,301 = 87.04%** |
| Non-GPU core branches | 3,490/4,980 = 70.08% | **3,566/4,980 = 71.61%** |
| Package/update branches | 2,150/2,998 = 71.71% | **2,226/2,998 = 74.25%** |

Denominators and ≥80%/≥70%/≥90% thresholds are unchanged. The strict target
correctly exits **1** because package/update coverage remains below 90%.
Arch x86-64: Clang/LLVM/format/tidy **23.1.1**, GCC **16.2.1**, Python **3.14.8**,
CMake/CTest **4.4.4**. All successful checks below exit 0.

| Command | Result / raw log in R |
|---|---|
| `cmake --build --preset coverage -j 4` | C++26 build passes; `build-manifest.log` |
| `cmake --build --preset coverage --target we_coverage_check` | **133/133 CTest**, 36.17 s; expected target exit 1; `coverage-complete.log`, `final-summary.json` |
| `cmake --build --preset dev -j 4`; `ctest --preset dev` | GCC C++26 viewer/core build, **151/151 CTest**, 35.67 s; `build-dev-complete.log`, `ctest-dev-complete.log` |
| `build/asan/we_core_tests '[worker_validation],"worker event sessions and command acknowledgements cannot be spoofed or reordered"'` after `cmake --build --preset asan -j 4` | Native ASan/UBSan/LeakSanitizer **17 cases / 20,747 assertions**, no errors; `build-asan-complete.log`, `asan-complete.log` |
| `clang-format --dry-run --Werror` and `clang-tidy --warnings-as-errors='*' -p build/coverage` on both touched C++ files | Pass; `static-commands.json`, `tidy-final.log`, `tidy-manifest.log` |

The first coalesced fixture reused progress sequence 2 for completion and failed
(`focused-first.log`); correction to sequence 3 passes. Initial lint rejected
throwing static string initialization and invalid C++ enum construction; constexpr
session bytes and serialized tag mutation correct both without weakening warnings.
The final focused cases preserve these fixes. Documentation, include boundaries
and diff checks pass; exact commands/exits are in `static-commands.json`.

Read-only hashing confirms **2,393/2,393 protected files unchanged**, including
videos, retained stages, fit/checkpoint and specification/runbook (`preservation.json`).
No user-video processing, worker/environment/recipe change, GPU run, upload or push.
Remaining worker branches include structural-verifier-dominated guards, allocation/
post-failure paths and the 30-second startup deadline. Native other-platform
confirmation and full coverage/sanitizer/GPU qualification remain pending.
Durable-map configuration/checkpoint admission is the next scoped task in
[progress](../progress.md).
