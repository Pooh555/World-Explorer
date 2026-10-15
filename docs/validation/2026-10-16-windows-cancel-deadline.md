# Worker cancellation clock repair — 2026-10-16 Asia/Hong_Kong

Scope/status: cancellation test observer repaired and **VERIFIED locally**;
native Windows rerun **IMPLEMENTED_UNVERIFIED**. Parent `073d6b3`; cwd repository
root. The supplied Windows output fails test 231/248 at
`worker_protocol_tests.cpp:441`: 18/19 assertions pass, while an **8.1507565 s**
launch-to-return interval exceeds six seconds. It records START, ready and CANCEL
acknowledgements, but no stop-request timestamp; actual Windows cancellation
latency cannot be recovered from this snippet. Total test time is 10.44 s.

The test started its clock before Python launch, protocol initialization and
fixture preparation. Its deadline therefore included work before cancellation
was requested. The observer now records the first stop request in the existing
event callback and measures through return/child cleanup. The **six-second
cancellation bound is unchanged**. Missing cancellation is an explicit failure.
Mode, pre-cancel and cancellation intervals are retained in Catch diagnostics.
Production process handling, two-second cooperative grace, forced termination,
30-second START acknowledgement deadline and protocol schemas are unchanged.

A fourth generated mode deliberately waits 6.25 seconds after START before
reporting ready. It must spend at least six seconds before cancellation and
still satisfy the original shutdown bound. It also retains START/CANCEL
acknowledgement, checkpoint, no-work-before-START and ignored-child cleanup
checks. All fixtures are explicitly synthetic.

| Actual command / observation | Result |
|---|---|
| `cmake --build --preset dev -j 4 --target we_core_tests`; `build/dev/we_core_tests 'typed worker cancellation acknowledges cooperative stop and bounds an ignored request'` with delayed mode and old timer | Build 0, test **42**; deterministic old-timer failure at **6.319191305 s**, 28/29 assertions pass; `R/before.log` |
| Final `cmake --build --preset dev -j 4`; same focused test with `--success` | Exit **0**; four modes / **30 assertions**, `R/final.log` and parsed `R/timings.json` |
| `ctest --preset dev --output-on-failure` | Exit **0**, **267/267**, **95.83 s**, `R/ctest-dev.log`; final optional-value guard subsequently checked by the focused run |
| `cmake --build --preset compat-cxx23 -j 4`; `ctest --preset compat-cxx23 --output-on-failure` | Exit **0**; explicit GCC C++23 CPU profile, **249/249**, **61.62 s**, `R/ctest-cxx23.log` |
| `clang-format --dry-run --Werror tests/unit/worker_protocol_tests.cpp`; `clang-tidy -p build/dev tests/unit/worker_protocol_tests.cpp` | Exit **0**, 23.1.1; no first-party tidy diagnostics after guarding the optional timestamp explicitly |

Final native Linux GCC 16.2.1 C++26 focused measurements:

| Synthetic mode | Before stop request (s) | Stop request through cleanup (s) |
|---|---:|---:|
| Cooperative wait | 0.0510942 | 0.017734 |
| Ignore request | 0.0790131 | 4.01555 |
| Before START | 0.0449702 | 2.02095 |
| Delayed ready | **6.29481** | **0.0210927** |

`R` is ignored `artifacts/validation/worker-cancel-deadline-20261016`. No
production sources, dependency pins, saved reconstructions or environments
changed. This replay proves the observer error and preserves the original
cancellation bound; it does not substitute for the requested native Windows
rerun. M2/P6 work remains at its existing handoff after this repair.
`python3 tools/check_docs.py` exits 0 (613 local targets/128 files), and
`git diff --check` exits 0.
