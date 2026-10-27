# External MODEL shared request and CLI — 2026-10-27

Scope: A0/AP0 stays **IN_PROGRESS**. The shared application command and
`we_process inspect-external-models REQUEST.json SHA256` reopen independent
small receipt snapshots and exact MODEL/review/host/byte selections, then run
the existing two-pass observer. The [protocol](../protocols/external_model_inspection.md)
requires strict request budgets before receipt admission and always labels output
unqualified, with every global blocker and no acquisition/execution authority.
Original v1 limits/encodings remain unchanged. A1–A6 and M4 remain unstarted.

Repository root `/home/Pooh555/programming/World-Explorer` is default cwd.
Parent: `6a1f436a2ff7b454ab567eca28bfcdb5ecc224c4`. Actual argv/cwd/exit/time, logs,
requests/reports and independent oracle are under
`artifacts/validation/a0-external-model-cli-20261027/` (`R`), using
`python3 R/run_check.py TAG COMMAND...`. CMake plus five C++ files have sorted
source SHA-256 `90a18e6eef5bdf67c62581c70d17c7a2a35fbe0aa8388db2f11a5940452287d6`;
`R/source-identity.json` defines the algorithm and individual hashes.

Focused GNU C++26 checks pass **40 cases /19,196 assertions** (`focused-first`),
including 30 existing observer/v1 evidence/tree cases. New cases cover exact
request/report/receipt/context/review joins, relative/absolute UTF-8 native paths,
unknown costs/restricted rights, every global blocker, malformed types/fields/
enums/paths, 2 MiB exact/excessive raw bytes, duplicate/NUL/deep JSON, count and
byte ceilings before opening missing selected receipts, cancellation/callback
failure, changed bytes/timestamps and symlinked requests/roots/parents. Linux
symlink cases ran; that case is explicitly skipped on Windows. Actual CLI success
matches shared JSON; wrong hashes/arguments/changed files refuse with exit 2 and
preserve sources. A test-only JSON/string ranges comparison initially failed
compiler/tidy concepts; explicit string extraction fixes it without suppressions.
Those failing logs remain. Native path sizing now uses UTF-8 consistently.

The independent `R/replay_large.py` selects prior verified synthetic report pins
outside the inspected receipts, writes fresh requests, invokes the actual CLI,
streams independent file hashes and checks complete report pins, costs, review
joins, authority labels and blockers. Each large source has **36 unchanged files**;
both preserve the original checkpoint-sized and doubled payload and small receipts.
`large-replay-run` passes; `R/product-oracle.json` records two request/report identities, all six exit statuses
(two successes, two wrong-request-hash refusals, two rehashed changed-selection
refusals) and **72** preserved source files; the script defines their exact argv. Original large-file
bytes/identities remain in the [observer evidence](2026-10-27-external-models.md).

| Synthetic file bytes | Request SHA-256 | Report SHA-256 |
|---|---|---|
| 156,008,466 | `3cd68b827ccf542d0cca01c5c87e5e21d8b499d0bf811feb6d94e4b3bbcc7e46` | `d904905d42b853c2ab9a9eef428242143076749c41d30f2ab21455c539b8cb13` |
| 312,016,932 | `435b79368df25d472a75a5d852e69cfef634d20ab1519170f87bc2689e50322c` | `77db0357676986778cd80e49d5ce25621df9596b0d4a03cfe367b12d0db75aea` |

| Actual checks, exit 0 | Evidence tag / result |
|---|---|
| GNU16.2.1 C++26 native / explicit C++23 core builds | `build-dev-fixed`, `build-compat-fixed`; binaries frozen during complete suites |
| Full CPU/worker suites | `ctest --preset dev -j2`: **937/937**, 427.66 s (`ctest-dev`); `ctest --preset compat-cxx23 -j2`: **912/912**, 410.79 s (`ctest-compat`) |
| Native ASan/UBSan/default LSan | `asan-native`: **40 cases /19,196 assertions**, 139.91 s, no reported errors; ASAN/UBSAN/LSAN options unset, native execution required for LSan |
| Exact LLVM23.1.1 format/tidy, boundaries/docs/whitespace | `style-final`: all five C++ files pass, warnings unchanged |
| Local Clang23.1.4/libc++23.1.1 C++23 syntax | `libcxx-{command,tests,cli,models}-run`: four TUs pass; native macOS/Windows/GCC13 remain unqualified |

All **378** protected source/product files match their prior receipts
(`preservation-final-run`), in addition to the 72 large-fixture source files.
Documentation/whitespace checks pass (`docs-final`, `whitespace-final`). No model
acquisition, installer, worker, GPU solve, real provenance/rights or AP qualification
occurred. Relinking leaves prior runtime payload evidence stale. The next A0
recipe/capture/readiness audit is recorded in [progress](../progress.md).
`R/finish.json` checks the 16 final command receipts, source identity, unchanged
specification/runbook/v1 implementations and all 72 retained source-file hashes.
The current milestone/thresholds and benchmark scope are unchanged.
