# Windows recovery database handles — 2026-10-13, Asia/Hong_Kong

Scope/status: E1/E2, local repair of the supplied `native-builds` Windows-2025
worker failure. Parent `d8760f6`; implementation identity is the commit containing
this report. Cwd: repository root. Windows rerun remains IMPLEMENTED_UNVERIFIED;
no full M1/M4 or native viewer/release qualification. No workflow, compiler
standard, dependency pin or acceptance threshold changes.

## Supplied failure and correction

The attached Windows log reaches CTest: **100/101 pass**, including every native
contract test. Only `worker_integration` fails. Python **3.14.7** runs 24 cases:
six recovery cases error during temporary-directory cleanup, three Linux/POSIX
queue cases are intentionally skipped, and 15 pass. Every error is **WinError 32**
deleting `database #saved.db`; ResourceWarning messages identify unclosed SQLite
connections. This supersedes the earlier FFmpeg setup failure as current Windows
handoff evidence. No tested CI commit SHA was supplied. Hosted input SHA-256:
`a9524926b6d6d7e0849d1d8df3d76a735de48fb6b38d2629913556623346aa5b`.

`with sqlite3.connect(...)` manages transactions, not connection lifetime. Both
recovery SQL reads and fixture writes relied on that context to close the handle.
Linux permits unlinking an open database and masked the cleanup failure; Windows
refuses deletion while a connection remains open. All four original connection sites now
use `contextlib.closing`. Fixture writes additionally keep the transaction context
so commits/rollback are preserved. Read-only URI encoding and input bytes remain
unchanged; cleanup errors and warnings are not suppressed.

The regression harness retains real SQLite connections and verifies each rejects
SQL after helper exit, including success, missing images, bad seeds, insufficient
inliers, backend failure and a new failed-query case. Strong references prevent
garbage collection from concealing a leak on POSIX. The harness finally closes
remaining handles after assertions so a failing regression does not mask its
cause with an additional Windows cleanup error. Fixtures also close their own
connections before temporary-directory cleanup.

## Actual checks

Local Arch Linux: Python **3.14.8 / 3.12.15**, GCC **16.2.1**, explicit C++23 CPU
profile. Raw hosted log, old helper, before-repair harness, source hashes and check
logs: ignored `artifacts/checks/windows-sqlite-20261013/`.

| Command | Observed result / log |
|---|---|
| `python3 artifacts/checks/windows-sqlite-20261013/regression-before.py` | exit 1 against parent helper; the real connection still accepts SQL; `regression-before.log` |
| `python3 -Werror::ResourceWarning -m unittest discover -s tests/integration -p 'worker*_test.py' -v` | exit 0, **25/25**, no skips/resource warnings; `python314-tests.log` |
| `.venv-worker/bin/python -Werror::ResourceWarning -m unittest discover -s tests/integration -p 'worker*_test.py' -v` | exit 0, **25/25**, no skips/resource warnings; `python312-tests.log` |
| `cmake --build --preset compat-cxx23 -j 4` | exit 0, existing CPU profile with `-std=c++23`; `build-compat.log` |
| `ctest --preset compat-cxx23` | exit 0, **102/102**; 25 Python checks inside worker case; `ctest-compat.log` |
| `python3 tools/check_boundaries.py` | exit 0; `boundaries.log` |
| `python3 tools/check_docs.py`; `git diff --check` | exit 0, 222 targets / 69 files; `docs.log`, `diff-check.log` |

Linux includes the POSIX-only closed-stdin worker-launch test; Windows excludes
it, accounting for the 102 versus 101 CTest totals.

No user video, completed fit, scene or reconstruction job was changed or launched.
The worker fixtures include tiny synthetic FFmpeg input; they do not certify real
reconstruction quality. Hosted Windows confirmation remains the next platform
check; local lifetime assertions are narrower evidence than a Windows pass.
