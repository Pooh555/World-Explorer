# Concurrent renderer contracts and TSan — 2026-10-14 Asia/Hong_Kong

Scope/status: **M1/E1 IN_PROGRESS**. Three new synthetic concurrency cases exercise
the production multi-producer/single-consumer renderer update sink. Separate
native TSan execution reports no races; this does not certify GPU lifetime,
other platforms, full M1 or complete E1. Parent `c5980bb`; implementation is
the commit containing this report. Cwd: repository root.
`R = artifacts/validation/render-tsan-20261014` retains exact argv/environment,
exit receipts, logs, source/binary identities and preservation inventories.

## Checked behavior

[Tests](../../tests/unit/render_concurrency_tests.cpp) release four producer
threads with a barrier while the test/render owner executes consumer operations.
Each producer owns its candidate and result slot; joins publish those results.
Assertions run on the test owner, and loops are finite without polling/sleeps:

| Case | Diagnostic scope / invariant |
|---|---|
| Shared receipts and consumption | **32 revisions**, two producer waves per revision: concurrent admission/consumption/retirement, then exact retries during upload/visibility. Exactly one caller transfers ownership; all others retain complete metadata/snapshot and receive the same ticket. No duplicate work; previous snapshot hashes remain stable |
| Cancellation races | **32 fresh sinks**: admission races with take/cancel. Both legal outcomes are checked: pending cancellation retains the cancelled receipt, or an admitted upload survives cancellation and becomes visible. Retries preserve callers and never restart cancelled work |
| Backpressure and repair | **16 fresh sinks**: a visible snapshot, active upload and pending successor fill a three-scene/one-pending budget. All four producers get `WOULD_BLOCK` with intact arguments while the render owner probes/retire-checks. Cancellation preserves the active upload, stale base requires resync, corrected unused operation ID succeeds, and retired residency can be released |

Each complete run executes **112 four-producer waves**. Barrier release creates
concurrent participants; it does not guarantee every scheduling interleaving.
GPU acknowledgements are synthetic CPU contract calls. No worker/backend, real
scene, Vulkan submission or physical display participates in these cases.

## Actual checks

Arch Linux x86-64; GCC **16.2.1**, CMake **4.4.4**, format/tidy **23.1.1**.
`tsan` uses explicit C++26, `WE_SANITIZER=thread`, CPU-only, with no ASan,
fuzzer or coverage instrumentation. Native test commands set
`TSAN_OPTIONS=halt_on_error=1`; no suppression or warning policy was changed.

| Commands (full argv in R) | Exit / result / raw evidence |
|---|---|
| `cmake --preset tsan`; `cmake --build --preset tsan -j 4` | Both 0; `final-tsan-build-{0,1}.log` |
| Native `build/tsan/we_core_tests '[render_threads]' --reporter compact` | 0; **3 cases / 5,308 assertions**; `final-tsan-focus-0.log` |
| Ten further native focused executions | All 0; **30 case executions / 5,320–5,332 assertions per run**, scheduling dependent; `final-tsan-repeat-{0..9}.log` |
| Native `ctest --preset tsan` | 0; GCC C++26 CPU **242/242**, 138.15 s; `final-tsan-all-0.log` |
| `cmake --build --preset dev -j 4`; `ctest --preset dev` | Both 0; GCC C++26 **260/260**, 66.64 s; `dev-{0,1}.log` |
| Compatibility build `-j 4`; `ctest --preset compat-cxx23` | Both 0; explicit GCC C++23 CPU **242/242**, 66.27 s; `compat-{0,1}.log` |
| Format, tidy warnings-as-errors, docs, boundaries, diff | All 0; `static-commands.json`, final documentation receipts |

The complete TSan CPU suite also exercises existing scene snapshot readers,
durable map writer serialization, pose publication and concurrent cancellation
contracts. The Python worker suite remains its ordinary test: native C++ TSan
does not instrument Python/extensions or external solver processes. Dev and
compatibility suites ran concurrently; their elapsed times are not performance
qualification. `R/identity.json` records exact final source, test executable,
library, compile-command/cache and runner hashes.

Initial versions pass direct tests, ten native TSan repetitions and **242/242**
native CPU checks (141.47 s). Tidy rejects optional dereferences after Catch
`REQUIRE`: its analysis does not model that macro as a guard. Explicit checked
extraction repairs the test source without a suppression. Initial and second
diagnostics remain in `initial-static-1.log` / `second-static-1.log`; final source
passes tidy and is rebuilt/retested separately in the `final-tsan-*` receipts.
No production defect, TSan race or sanitizer suppression was observed.

**2,393 protected files and all 94 tracked src/include files remain byte-identical**.
Before inventories inherit the preceding slice's post-check baseline; after
inventories rehash every selected file, with empty changed lists in
`protected-result.json` / `production-result.json`. No reconstruction, GPU,
environment, recipe, schema, threshold or coverage-scope changes; no upload/push.
Longer stress, allocation/process faults, complete platform/reference fixtures
and rights/release qualification remain pending. No gate is upgraded.
