# A0 declared manual-action journal — 25 October 2026

**VERIFIED: local CPU journal slice; A0/AP0 remain IN_PROGRESS.** Bounded declared
action receipts, immutable history, separate scope accounting and a shared native
record command are implemented. The [protocol](../protocols/manual_actions.md)
owns encoding, API, bounds and authenticity limits. This does not prove human
activity, consent, completed output edits, automatic yield or AP0 qualification.

Cwd: repository root; timezone Asia/Hong_Kong; work spans 24–25 October. Parent
`814d15934364cb64cc97980f15f38e4c70f29304`.
`R=artifacts/validation/a0-manual-actions-20261024` (ignored, start-date name).
`implementation.json` pins ten source/build/tool/test inputs, SHA-256
`61a2105da61e8ec2d46a7898c9cfe4b7c405b88c8b4ff16eb602c62ef8ac67ce`.
The boundary checker/CMake now explicitly allow `application` → `asset_catalog`
for the shared command; solver isolation, coverage scope and thresholds retain
their existing definitions.

## Exercised behavior

Eleven kinds derive setup, evaluation-only or output-assistance scope. Requests
bind independently admitted receipt/graph/context and exact selected targets;
action IDs, operation IDs and content hashes remain distinct. Canonical sorted
targets make permutations idempotent. Journal snapshots own records/accounting;
setup/evaluation records cannot supply inference action hashes through the new
binding API. The existing graph still validates declarations without requiring
that binding. No actual output command/result is authorized by a receipt.

Nine new native cases cover all kinds, separate accounting, target order, exact
and later-generation replay, changed-operation/action-ID reuse, stale bases,
source/context/target substitution, malformed/rehashed records, duplicate keys,
wrong scope, unknown versions/kinds, invalid integers/UTF-8/NUL/time and byte
bounds. Exact 16-target/64-byte-actor/4,096-byte-detail/seven-day limits admit.
Foreign, protected-source, held-lock and available symlink destinations preserve
caller trees. Cancellation and observer exceptions preserve the committed
manifest; changed pending retries refuse and matching retries recover. A real
child process exits 77 after installing the action blob but before manifest
publication; reopening shows zero committed actions, then the shared command
recovers the exact operation. This is controlled interruption, not arbitrary
power-loss or disk-fault qualification. The maximum 1,024-action history was not
exercised end-to-end.

The independent Python standard-library producer (`python3 R/oracle-final.py`,
expand R) supplies synthetic source/recipe receipt bytes and requests to a native
probe using the shared application command. **11 canonical action hashes match**
its independently encoded expected documents; completed replay preserves all
files; **eight mutations reject**. Actual `we_process inspect-actions` reports
3 setup/1 evaluation/7 assistance actions and 303/101/707 declared active ms;
three bad pin/generation invocations reject. Source bytes remain unchanged
(`oracle-final-results.json`, `oracle-final.log`). No real human action or asset
production is represented by these fixtures.

## Actual checks

| Command | Exit / result; logs under R |
|---|---|
| `cmake --build --preset dev -j 4`; `ctest --preset dev -j 4` | 0; GNU 16.2.1 C++26 viewer/CLI/core, **607/607**; `build-dev-final.log`, `ctest-dev-final.log` |
| Same with `compat-cxx23` | 0; explicit C++23 CPU, **582/582**; `build-compat-complete.log`, `ctest-compat-final.log` |
| `build/dev/we_core_tests '[manual-actions]'`; same under `build/compat-cxx23/` | 0; **9 cases / 1,524 assertions each**; `focused-final.log`, `focused-compat-final.log` |
| `cmake --build --preset asan -j 4 --target we_core_tests`; native `build/asan/we_core_tests '[manual-actions]'` | 0; same **9/1,524**, ASan/UBSan/default LSan without reports; `build-asan-final.log`, `asan-final.log` |
| LLVM 23.1.1 `python3 tools/check_cpp_style.py` on eight affected C++ paths | 0; format/tidy/boundaries/docs/whitespace; `style-complete.log` |
| Pinned Black 26.5.1 tracked-Python check per AGENTS | 0; `black.log` |
| `python3 tools/check_docs.py`; `git diff --check` | 0; `docs.log`, `whitespace.log` |
| `python3 R/check_preservation.py` (expand R) | 0; **378 protected media/result hashes match**; `preservation.json` |

Probe compile uses `c++ -std=c++2c -Iinclude -isystem build/dev/_deps/json-src/include R/probe.cpp build/dev/libapplication.a build/dev/libasset_catalog.a build/dev/libcore.a -lcrypto -pthread -o R/probe-final`.
Initial fixture output incorrectly overlapped protected source and correctly
refused (`focused-first.log`). Corrected disjoint roots pass. Tidy found an
invalid-enum test cast, duplicate switch branches and constant names; explicit
bit-pattern input, merged labels and conforming names retain negative checks.
Sandbox LSan test discovery failed under ptrace (`build-asan.log`); native build
and execution pass with leak checking enabled.

No media processing, reconstruction, inference, installation, runtime inventory
refresh or GPU/platform qualification ran. Runtime receipts still need recapture
after relinking. Actual mutation/result binding and enforced ancestry remain in
[progress](../progress.md); unknown provisioning costs still block this host.
