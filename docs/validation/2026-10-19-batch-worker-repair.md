# Batch diagnostic worker repair — 2026-10-19 (Asia/Hong_Kong)

Scope: repair the three CPU batch-admission regressions following style commit
`145d5ab`. Repository root is the working directory for all commands below.
Fixtures are synthetic; no real reconstruction or platform gate is promoted.

The style change renamed the Python class `event_writer` to `EventWriter` but
missed two references in the Python worker script embedded in
`tests/unit/batch_pose_admission_tests.cpp`. That script failed before emitting
its handshake, producing the misleading downstream incomplete-stream failures.
Both references now use `we_protocol.EventWriter`; all failure mappings,
capability rejection, event ordering, retry and file-preservation assertions
remain unchanged. The previous suggestion that these failures were unrelated
to the style slice was incorrect.

## Checks

Logs are retained in `artifacts/validation/batch-worker-repair-20261019/`.

- Before repair: `ctest --preset dev -R 'batch (worker failure mapping|synthetic completion|resumed publication)' --output-on-failure`
  exited 8; all three tests failed (`before.log`).
- `cmake --build --preset dev -j 4` exited 0 (`build.log`).
- After repair, the same targeted CTest command exited 0: **3/3 passed** in
  15.87 seconds (`targeted.log`), including failure mapping, successful ordered
  completion and resumed publication/caller-file preservation.
- `python3 tools/check_cpp_style.py tests/unit/batch_pose_admission_tests.cpp`
  exited 0 with clang-tidy enabled (`style.log`). Formatting, include boundaries,
  documentation links and whitespace checks passed.
- `ctest --preset dev` exited 0: **374/374 passed**, including worker integration,
  in 90.47 seconds (`ctest.log`) on the local Linux development profile.

The native M3 integration next step and qualification requirements are unchanged.
