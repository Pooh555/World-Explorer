# Native CI audit-path observer — 2026-10-13 Asia/Hong_Kong

Scope/status: **E1/E2 IN_PROGRESS**; fix of the shared failure in the supplied
Windows/macOS `native-builds` logs. Parent `bc52a8c`; implementation is the commit
containing this report. Cwd: repository root. Raw evidence:
`R = artifacts/checks/native-audit-paths-20261013`. Hosted rerun of this repair
remains **IMPLEMENTED_UNVERIFIED**. No full M1/M4 or viewer/device qualification.

## Supplied observations

| Supplied C++23 CPU run | Actual outcome |
|---|---|
| Windows | **101/102 CTest pass**, 92.43 s; only `worker_integration` fails; process exit 1 |
| macOS | **102/103 CTest pass**, 41.50 s; only `worker_integration` fails; process exit 8 |

Each worker suite runs **36 Python cases: 32 pass, one fails, three intentional
Linux/POSIX queue skips**. The shared failure is
`test_record_and_inventory_digests_use_the_same_file_read`, with **`0 != 1`**
at the observed-open count. The earlier SQLite failure is absent: all named pose
recovery cases, including failed-query closure and retained-database preservation,
pass in both supplied logs. This is new hosted evidence for that narrower repair,
while the complete worker suite is still failing.

The current workflow configures `compat-cxx23` with `WE_BUILD_VIEWER=OFF` on
Windows-2025/macOS-15. These logs show compiled CPU contracts executing, not an
SDL/Vulkan viewer pass. Tested Git SHA/compiler/Python versions are not supplied;
the macOS paste is an excerpt. The one-test count difference matches the native
closed-stdin launch case excluded by `_WIN32`.

Retained attachment SHA-256:

- Windows: `7f6ad98254d1a5c0a9d8193600098e5006622e0f9d9fcedcd66e87e8dc07acf4`
- macOS: `5402a5a0ae398a4d0bc8958fd80893c81575fd2db4a9d5d369ae1cb65765e281`

## Cause and repair

The test's observer compared the original fixture spelling against the canonical
path passed to `Path.open` by the auditor. Equivalent paths can differ after
resolution; the observer then counts zero despite the successful audit. The exact
host spellings are not logged, so a controlled alias replay supplies the causal
check. An existing `site-packages/../site-packages` fixture reproduces **`0 != 1`**
locally against the parent test. The observer target now uses `resolve(strict=True)`.
A separate alias regression exercises that difference on every OS without symlink
privileges. Exact one-read, digest and verified-receipt assertions remain enforced.

Only the test changes. Production auditing, worker sources, package versions,
installed identity receipts, CI skips and thresholds remain unchanged. Initial
replay setup aliased the site but not its metadata path and failed in `relative_to`;
`before-harness-error.log` retains it. The corrected replay then reproduces the
actual assertion failure; no failing replay is counted as verification.

## Actual local checks

Arch Linux; Python **3.14.8 / 3.12.15 / 3.10.22**, GCC **16.2.1**. Commands below
use R expanded to the path above; logs and source identities remain ignored.

| Command | Exit / result |
|---|---|
| `python3 R/reproduce_before.py` before repair / after repair | **1**, observed `0 != 1` / **0**, exact one-read check passes; `before.log`, `after.log` |
| `python3 -m unittest discover -s tests/integration -p 'worker_environment_audit_test.py' -v` and the same argv with `.venv-worker/bin/python`, `.venv-train/bin/python` | **0**, **12/12 each**, no skips; `audit-python*.log` |
| `python3 -Werror::ResourceWarning -m unittest discover -s tests/integration -p 'worker*_test.py' -v` and the same argv with `.venv-worker/bin/python` | **0**, **37/37 each**, no skips/resource warnings; `worker-python*.log` |
| `python3 R/run_checks.py compat` | Actual C++23 CPU build then CTest: **0, 103/103**; `compat-commands.json`, `compat-*.log` |
| `python3 R/run_checks.py dev` | Actual GCC C++26 development CTest: **0, 119/119**; `dev-commands.json`, `dev-0.log` |
| `python3 R/run_checks.py static` | Syntax, documentation, include boundaries and diff checks: **0**; exact argv/logs retained |

An initial CTest probe before refreshing the older local compatibility build passes
102/102; `ctest-compat-existing-build.log` retains its narrower binary scope. The
103-case result above follows the actual build of current sources.

No video, scene, fit/checkpoint, installed environment or reconstruction job was
changed or started. Hosted Windows/macOS reruns are still required to confirm this
observer repair. Native viewer/device/release, full corpus and Q/P/N/E gates remain
unqualified; the M1 native input/HiDPI handoff remains the next independent task.
