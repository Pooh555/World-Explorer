# Windows loader-path roundtrip repair — 2026-10-15 Asia/Hong_Kong

Scope/status: **M1/E2 IN_PROGRESS**. Supplied Windows C++23 CPU CTest again
reports **240/241 passed**, exit **1**, **227.51 s**; only `worker_integration`
fails. Its Python **3.14.7** run has **132 cases / seven errors / one failure /
11 skips**. The excerpt omits its CI commit and compiler version. The installed
owner tests and earlier directory/parser checks pass; synthetic capture still
fails. Native Windows confirmation of this repair remains pending.

Cwd: repository root; parent `a77fc79`; implementation is the local commit
containing this report. Raw ignored evidence:
`artifacts/validation/windows-runtime-roundtrip-20261015/` (**R**).
Supplied log SHA-256:
`33f0f1074b50aa2a9d546535c9910c1eae2b3277d807ade78b40e4d49a71394a`.
Final test source SHA-256:
`68a6e5955ea8fae4aebd72f1fedbf0031869d6f47358f4c71b5ff3020e2ecba9`.
`R/identity.json` additionally pins the production auditors and local recipes.

## Cause and correction

The [preceding fixture repair](2026-10-15-windows-runtime-fixture.md) omitted an
internal conversion: real capture calls `inspect(Path(resolved))`, then
`selected_path(str(path))`. Windows rewrites the virtual POSIX namespace using
backslashes. The adapter recognized only the original slash spelling, so it
looked for a nonexistent host `synthetic-runtime` directory. The direct namespace
test passed because it had not exercised the full capture path.

The first mutation subtest's unexpected exception also bypassed restoration.
Its altered program remained in the temporary fixture and caused the following
library subtest to fail with `Selected dependency is not ELF` instead of the
expected mutation error.

[Test-only correction](../../tests/integration/worker_runtime_audit_test.py)
recognizes the drive-less Windows spelling of the explicit virtual namespace
before mapping it to contained temporary files. Drive-qualified/UNC spellings,
escape, control-character and Linux separator checks remain. Mutation assets
restore in `finally`. A new full-capture case injects Windows path conversion at
the real loader-output boundary and requires identical nodes, roots and command
arguments. Assertions/limits are unchanged; no tests are removed or skipped to
obtain a pass. Production auditors, pinned receipts, workflow and C++ are unchanged.

## Actual checks

`python3 R/regression_probe.py` injects `PureWindowsPath` at capture's constructor
boundary and runs all seven affected cases. Parent source reproduces **seven
errors / one failure**; repaired source passes **seven cases**, zero errors/
failures/skips. `before-regression.log`, `after-regression.log` and
`regression-results.json` retain the results/source hashes. This is a portable
reproduction, not native Windows execution.

Arch x86-64, GCC **16.2.1**, CMake/CTest **4.4.4**. `R/run_checks.py` and
`*-commands.json` retain actual argv, cwd, source identities, exits and log paths.

| Commands, from root | Actual result / logs in R |
|---|---|
| `python3 R/run_checks.py focused`; `workers` | **0**, **16/16 runtime / 133/133 worker cases** each on Python **3.14.8 / 3.12.15 / 3.10.22**, no local skips; `focused-*`, `workers-*` |
| Dev configure/build `-j 4`/CTest | All **0**, C++26 **260/260**, 58.44 s; `dev-{0..2}.log` |
| `compat-cxx23 -DWE_BUILD_VIEWER=OFF` configure/build `-j 4`/CTest | All **0**, explicit C++23 CPU **242/242**, 57.70 s; `compat-{0..2}.log` |
| Default dependency-manifest check | **0**, current tracked source/receipt structure; `manifest-0.log` |
| Local manifest check, with/without `--require-closure` | Both **2**, previously recorded absent snappy path; `manifest-{1,2}.log`. This independent system drift is retained, not repaired by a fixture change |
| Docs / boundaries / diff | All **0**, `static-commands.json` / logs |

All **2,088 protected files / 94 tracked src/include files** remain unchanged
(`R/preservation.json`): videos, saved scenes, failed stages, specification/runbook.
No reconstruction, model/checkpoint loading, GPU/UI run, installation, push or
upload. C++ formatting/tidy/sanitizers were not rerun for this Python fixture
change. The supplied Windows skips remain distinct from passes. Rerun the native
Windows CI job against the repair before claiming a native pass. Full M1/E2 and
runtime-evidence refresh remain open; [progress](../progress.md) owns the next task.
