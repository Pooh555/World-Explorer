# Windows runtime-audit fixture repair — 2026-10-15 Asia/Hong_Kong

Scope/status: **M1/E2 IN_PROGRESS**. Supplied Windows C++23 CPU CTest reports
**240/241 passed**, exit **1**, **260.97 s**. Only `worker_integration` fails:
**115 Python cases, five errors / three assertion failures / 11 skips**. Native
executables ran successfully; the reported failure is in synthetic Linux-loader
fixtures. The excerpt omits the CI commit and compiler/Python versions. Native
Windows confirmation of this repair remains pending.

Cwd: repository root; parent `5d5d926`; implementation is the local commit
containing this report. Raw evidence is
`artifacts/validation/windows-runtime-fixture-20261015/` (**R**). Supplied log
SHA-256: `8d9c0d3f0048ab6d7edf5cfc1be539716dafda1d25ac85730e7758c525f53948`.
Final test source SHA-256:
`d111632c2f31af9092b7285a123c8a3f3cd5a165ce796b29bb7f558f013f9924`.
`identity.json` pins source, auditor, supplied log and local replay/check recipes.

## Cause and correction

The real auditor is Linux-only. Its directory validator rejects `:` because it
separates Linux library search entries. Synthetic capture tests passed host
directories, so the Windows drive prefix `D:` failed before loader resolution,
interpreter metadata, summary checks or mutation assertions could run. Seven
cases are affected; the root/library mutation case contributes two failed
subtests. Simply bypassing that rejection would expose further host assumptions:
the fake loader emitted Windows paths where its parser expects absolute POSIX
paths, and text-mode interpreter metadata could acquire CRLF.

[Fixture repair](../../tests/integration/worker_runtime_audit_test.py) gives the
synthetic loader an explicit `/synthetic-runtime` namespace. A scoped test adapter
translates those paths to contained temporary host files, preserving directory,
separator, control-character and escape rejection. Regular-file validation uses
the unchanged real validator. Synthetic interpreter metadata uses explicit LF
bytes. The fake runner never launches a loader, solver or backend. Mutation,
unresolved-dependency, library-context and raw/current-byte assertions remain.

The production auditor, checker, archived receipts, dependency pins, workflow,
schemas and environments are unchanged. Two added cases check Windows-root
spelling, Linux parser/path translation and invalid/escaping directories; the
actual Linux separator guard remains exercised. No checks were skipped or
thresholds weakened to obtain a local pass.

## Actual verification

`python3 R/regression_probe.py` replays the Windows directory predicate, host-path
loader spelling and text-mode metadata translation against parent/current test
sources. Parent: **seven cases / five errors / three failures**; repaired:
**seven cases / zero errors/failures/skips**. Logs: `before-regression.log`,
`after-regression.log`; identities/counts: `regression-results.json`. This is a
portable reproduction, not a native Windows run.

Arch x86-64, GCC **16.2.1**, CMake/CTest **4.4.4**. Exact argv, cwd, source
identities, exits and timings are in `*-commands.json`, defined by `R/run_checks.py`.

| Actual commands | Result / logs in R |
|---|---|
| `python3 R/run_checks.py focused`; `workers` | Exit **0**, **15/15 runtime / 117/117 worker cases** each on Python **3.14.8 / 3.12.15 / 3.10.22**, no local skips; `focused-*`, `workers-*` |
| `cmake --preset dev`; dev build `-j 4`; `ctest --preset dev` | All **0**, C++26 **260/260**, 63.88 s; `dev-{0..2}.log` |
| `cmake --preset compat-cxx23 -DWE_BUILD_VIEWER=OFF`; compatibility build `-j 4`; compatibility CTest | All **0**, explicit C++23 CPU **242/242**, 63.05 s; `compat-{0..2}.log` |
| `ctest --preset dev -R '^worker_integration$'`; same compatibility command | Both **0**, final LF fixture bytes, **1/1** each; `worker-ctest-{0,1}.log` |
| Default/local dependency-manifest checks; local `--require-closure` | **0 / 0 / expected 3**; historical source/mapping identities remain valid, rights gaps retained; `manifest-{0..2}.log` |
| Docs / boundaries / diff | All **0**; `static-commands.json` / logs |

Full CTest ran during the final metadata edit; the final worker-only reruns and
three-interpreter suites bind the complete repaired source. No C++ source changed,
so formatting/tidy/sanitizers were not rerun. **2,088 protected files / 94 tracked
src/include files** remain unchanged (`preservation.json`): videos, saved scenes,
failed stages, specification/runbook. No reconstruction, model loading, GPU work,
installation, push or upload occurred. Rerun the Windows CI job against the repair
before claiming a native pass. Its 241-test inventory excludes a POSIX-only native
case; the supplied skips remain separate from passes. Full M1/platform/release
qualification remains incomplete; [progress](../progress.md) retains the next work.
