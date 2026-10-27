# External MODEL file observations — 2026-10-27

Scope: A0/AP0 remains **IN_PROGRESS**. A separate schema-2 no-archive MODEL
profile joins independently selected native files/reviews to the original recipe
and hashes them in two streamed passes. The [protocol](../protocols/asset_external_models.md)
preserves every v1 receipt/evidence/tree budget. Results own metadata, not external
payloads, installation locks or execution authority. Global component,
installation, review, rights, runtime and worker-limit blockers remain explicit.
A1–A6/AP1–AP9 and M4 remain unstarted.

Repository root `/home/Pooh555/programming/World-Explorer` is the default cwd.
Parent: `6eb98d261aed024c54307f3afb7648a674bb8991`. Actual argv/cwd/exit/time,
logs, probes and independent oracle live under
`artifacts/validation/a0-external-models-20261027/` (`R`). Commands use
`python3 R/run_check.py TAG COMMAND...`. CMake plus five C++ files have sorted
source SHA-256 `18db455d356b4b32e49fac624dba1d316f2cb6c828d9719b8008de4c3bab2bd1`;
`R/source-identity.json` defines the algorithm and individual hashes.

The first development build passed. Initial tests exposed a fixture-only context
comparison with no equality operator; assertions now compare epoch/pose explicitly.
An exact LLVM23.1.1 analyzer found Catch2's false-assertion enum flags; ordinary
negated assertions retain the checks without changing warnings. The new 256-file
boundary fixture initially repeated identical MODEL manifests, so original recipe
validation correctly refused duplicate component identities. Distinct logical
model paths repair the fixture; a diagnostic Catch2 `INFO` ternary also needed
parentheses for stream precedence. Failing build/focused/full-suite logs remain.
GNU `time` is absent; launch failures remain in the memory-checkpoint/doubled logs.
A separate fresh-child Python `getrusage` probe measures Linux peak RSS instead.

Final focused GNU C++26 verification passes **30 cases /16,598 assertions**
(`focused-complete`), including v1 regression cases, independent selection/context,
strict schema/type/cost/path/size/count bounds (64/model, 256 total), review pins,
wrong content, symlink/hard-link/cross-component aliases, cancellation, callback
faults and mutations at both pass boundaries. Linux symlink checks ran; Windows
privileged symlink generators are explicitly skipped there. Native Windows/macOS
and GCC13 reruns remain unqualified.

| Actual checks, exit 0 | Evidence tag / result |
|---|---|
| GNU16.2.1 development C++26 / explicit C++23 core builds | `build-dev-complete`, `build-compat-complete`; frozen throughout subsequent suites |
| Full CPU/worker suites | `ctest --preset dev -j2`: **927/927**, 170.82 s (`ctest-dev-complete`); `ctest --preset compat-cxx23 -j2`: **902/902**, 169.57 s (`ctest-compat-complete`) |
| Native ASan/UBSan/default LSan | `asan-native`: 30 cases /16,594 assertions, 30.55 s, no reported errors; ASAN/UBSAN/LSAN options unset, native execution required for LSan |
| Exact LLVM23.1.1 format/tidy, boundaries/docs/whitespace | `style-complete`: all five C++ files pass; warnings unchanged |
| Clang23.1.4 + retained libc++23.1.1, C++23 syntax | `libcxx-external-final-run`, `libcxx-components-final-run`, `libcxx-tests-complete-run`: three TUs pass |
| Legacy helper extraction | `legacy-helpers.json`: all four helpers have identical tokens except inline/namespace; original v1 constants/encoding unchanged |

The four extra GNU focused assertions create/write retained probe products;
sanitizer runs leave the probe environment unset. Each synthetic product keeps
its external file, exact MODEL manifest/review pins and original small recipe/
evidence receipts under `R/product-final/{156008466,312016932}`. The independently
implemented Python oracle streams prefix/zero-body/suffix bytes, checks every
SHA/size, exact recipe/review joins and complete receipt inventories. Each receipt
pair contains only **3,731 +30,502 blob bytes**, with no external payload copy.
`product-oracle-complete-run` and `product-final-oracle.json` retain all identities,
including reports, selected reviews and all eight remaining global blockers.

| Synthetic external file bytes | Independently reproduced SHA-256 |
|---|---|
| 156,008,466 | `da17ed8233dda77a0f80e391ac1288a7ec1532a88cc37cea146668183e553ac7` |
| 312,016,932 | `4507efd42e061bba8728d55a9593f34471e03a54b4b2cea937d0affde61f2d66` |

Linux7.2.11/GNU C++26 bounded-memory checks use `R/memory_probe.py`: one fresh
native child, `getrusage(RUSAGE_CHILDREN).ru_maxrss` in KiB; no warmup, three
repetitions/size, excluding kernel file cache and other processes. The preselected
ceiling is **65,536 KiB**. Concurrent workloads were the two CPU suites and focused
sanitizers. This checks process memory, not worker/VRAM fit or throughput.

| File case | Three final peak RSS observations (KiB) |
|---|---|
| Small multi-model | 25,176 /25,304 /25,204 |
| Proposed checkpoint size | 25,212 /24,892 /25,240 |
| Doubled size | 25,136 /25,148 /25,328 |

`memory-{small,checkpoint,doubled}-{1,2,3}-complete-run` records each actual command;
matching `-metrics.json` records peaks. Maximum **25,328 KiB (24.73 MiB)** stays
below the ceiling at twice the proposed size. Both passes use the existing
64 KiB hash reader; metadata stamps and aliases are rechecked between phases.
This cooperative-filesystem observation does not protect against hostile races,
restored/coarse timestamps or later mutation, and owns no lock/payload.

All **378** protected source/product files match prior receipts
(`preservation-after-run`). No real checkpoint, environment, solver or GPU work
was acquired/activated. Runtime receipts remain stale after relinking. The next
shared read-only request/CLI integration lives in [progress](../progress.md);
full recipe/cost/review/runtime closure, real capture and actual producers remain
open. Both complete CPU suites pass after the fixture repairs. Documentation
links/whitespace pass (`docs-final`, `whitespace-final`), and `preservation-final-run`
confirms the 378-file receipt again. No thresholds, benchmark scope or roadmap
order changed.
`R/finish.json` verifies the 24 final command receipts, source identity, unchanged
specification/runbook/presets and unchanged v1 receipt/tree/hash implementations.
