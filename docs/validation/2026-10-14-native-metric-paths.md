# Native CI metric mutation observer — 2026-10-14 Asia/Hong_Kong

Scope/status: **M1/E1/E2 IN_PROGRESS**. The supplied Windows-2025/macOS-15
failures share an alias-sensitive metric test observer. The test repair and
portable regression pass locally; hosted repair confirmation remains
**IMPLEMENTED_UNVERIFIED**. Parent `4b3688e`; implementation is the commit
containing this report. Cwd: repository root. Raw evidence:
`R = artifacts/validation/native-metric-paths-20261014`.

## Supplied failures

| C++23 CPU host | Supplied CTest outcome | Worker outcome |
|---|---|---|
| Windows-2025 | **101/102 pass**, 102.70 s, exit 1 | 67 cases: 63 pass, one failure, three Linux/POSIX queue skips |
| macOS-15 | **102/103 pass**, 42.43 s, exit 8 | Same 67-case outcome |

Both fail `test_requirements_and_selected_implementation_mutation_abort` at
line 187: **`ValueError not raised`** for the selected implementation mutation.
The attachment excerpts omit part of the CTest/worker listings and do not supply
the tested Git SHA, compiler or Python versions. Their final summaries identify
only `worker_integration` as failing. Named SQLite recovery checks pass in both;
the earlier audit observer is not individually shown. These CPU logs do not
qualify the native viewer/device or release profile.

Exact supplied bytes are retained as `windows-supplied.log` and
`macos-supplied.log`; SHA-256:

- Windows: `ce76f35b4d703176188548007482fbe57f8e3c01b5c36b01a28191b254c2541a`
- macOS: `e1f242227d6997c626f2f064adf43d03790c2e0f149be55d5bdaae025dd1f67c`

## Cause and correction

The production auditor resolves installed paths before rehashing. The test
compared those paths with an unresolved fixture spelling, which can differ on
native temporary directories. Its hook could therefore miss the intended
mutation. Exact resolved host spellings are not logged. A controlled
`site-packages/../site-packages` replay against the parent test reproduces the
same assertion locally (`before.log`, exit 1).

The observers now resolve both targets and incoming paths, and explicitly assert
that each mutation happens exactly once. The new alias regression exercises
implementation and requirements mutation rejection on every OS without symlink
privileges. Synthetic RECORD construction also uses canonical relative paths.
The first repaired replay exposed an aliased fixture `relative_to` error during
RECORD refresh (`after-harness-error.log`); canonical fixture paths correct it.
The complete replay then passes (`after.log`, exit 0). Production checks, package
pins, thresholds and workflow skips are unchanged.

Test source SHA-256:
`8b6dbec809a9118eea66e10eb70860737ed080fda4e85dabaf9970250d06e271`.
`identity.json` records the parent, source/diff hashes, attachment bytes and tools.

## Actual local verification

Arch Linux x86-64, GCC **16.2.1**, Python **3.14.8 / 3.12.15 / 3.10.22**,
CTest **4.4.4**. No C++ changed; existing compatibility CPU and dev binaries are
used. All commands below exited 0, except the retained pre-repair/harness failures.
`run_checks.py` records exact argv, cwd, exits and log hashes in
`<group>-commands.json`.

| Command (R expanded above) | Observed result |
|---|---|
| `python3 R/run_checks.py focused` | Metric identity **18/18 each** on all three interpreters; `focused-*.log` |
| `python3 R/run_checks.py workers` | Full worker suite **68/68 each**, no skips or resource warnings; `workers-*.log` |
| `python3 R/run_checks.py ctest` | `ctest --preset compat-cxx23`: **103/103** (C++23, viewer off); `ctest --preset dev`: **121/121** (C++26); `ctest-*.log` |
| `python3 R/run_checks.py static` | Documentation, include boundaries and `git diff --check`; `static-*.log` |

Both Python groups use `-Werror::ResourceWarning -m unittest discover -s
tests/integration`, patterns `worker_metric_identity_test.py` and
`worker*_test.py`, with `-v`. No backend/model installation or reconstruction runs.
Read-only SHA-256 comparison against the previous holdout preservation receipt
confirms **2,393/2,393 protected files unchanged** (`preservation.json`,
`protected-after.json`), including videos, retained stages, fit/checkpoint and
specification/runbook. Environments are preserved.

Hosted Windows/macOS reruns remain required. Full M1/M4, coverage, native
viewer/release and Q/P/N/E qualification remain incomplete; the independent next
M1/E1 task stays scoped C++ contract coverage in [progress](../progress.md).
