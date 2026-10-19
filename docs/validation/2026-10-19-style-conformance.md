# Coding-style template conformance

This slice aligns the formatter/linter configuration, templates, and first-party
source with the documented conventions. C++ formatting was applied to 229
tracked files, including spaces inside every non-empty braced initializer. The
control-flow check now requires braces on all bodies, and `.clang-format` sorts
project, standard-library, and platform includes into distinct groups. The
existing first-line `#pragma once` policy remains enforced; clang-tidy's
conflicting pragma portability check is the sole exception.

Python source modules under `tools/` and `workers/` now define alphabetized
`__all__` exports; lowercase classes in source and tests were renamed to
PascalCase. The Python template's own export ordering was corrected. The
production compatibility target remains Python 3.10–3.12; the Python 3.15
template remains excluded from formatting.

## Validation

- `cmake --build --preset dev -j 4`: passed.
- `ctest --preset dev`: 371/374 passed. Tests 15–17 failed in batch worker
  launch/protocol handling; all three failures reproduced in an isolated rerun.
  The failures are in the existing batch-admission tests, whose changes here
  are formatting and brace placement only.
- `python3 tools/check_cpp_style.py --build-dir build/no-style-compile-db`:
  passed formatting, include boundaries, documentation, and whitespace checks;
  clang-tidy was explicitly skipped because that directory has no compile
  database. Log: `artifacts/validation/style-conformance-20261019/cpp-style-no-tidy.log`.
- Full `python3 tools/check_cpp_style.py`: reached 101/133 clang-tidy translation
  units without a diagnostic failure, then was stopped because progress stalled
  on `tests/unit/decoded_frame_admission_tests.cpp`. Partial log:
  `artifacts/validation/style-conformance-20261019/cpp-style-final.log`.
- `git ls-files -z -- '*.py' | xargs -0 -n1 python3 -m black --check --quiet --workers 1`:
  unavailable; Black is not installed (`No module named black`).
- Python compileall and the worker runtime/pipeline integration tests passed
  earlier in this slice; the documentation gate and `git diff --check` passed.

These results do not certify the M3 product gate or any platform/GPU profile.
