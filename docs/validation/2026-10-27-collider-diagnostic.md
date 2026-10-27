# Shared collider diagnostic and CLI — 2026-10-27

Scope: A0/AP0 remains **IN_PROGRESS**. The shared application command selects
current private asset/instance pins and a private schema-4 package, checks exact
catalog/scene/pivot joins and runs actual bounded Jolt queries. The new
`we_process inspect-colliders REQUEST.json SHA256` replays the pinned frozen
selection read-only and prints the same labeled report. The
[protocol](../protocols/native_collider_diagnostics.md) separates this synthetic
arbitrary-unit diagnostic from live scene authority, bodies, measured walking,
real production and AP qualification. A1–A6/AP1–AP9 remain unstarted.

Repository root `/home/Pooh555/programming/World-Explorer` is the default cwd.
Parent: `972c84dc6e00822bd8c08b28b5896f26441c93f8`. CMake + four C++ files:
`3ae65d6ebcce3f821f89eeaebcd193f2692bfe307b3428717e22d59c9634ac95`.
`R/source-identity.json` defines the sorted path/hash algorithm. Ignored actual
argv/cwd/exit/time records, logs and outputs are under
`artifacts/validation/a0-collider-diagnostic-20261027/` (`R`), using
`python3 R/run_check.py TAG COMMAND...`.

Final focused verification passes **8 cases /14,480 assertions**
(`focused-complete`), including stale source/instance and sub-tolerance frame
changes, fresh immutable revisions, LOD independence, rehashed malformed
queries/inventories, cancellation, actual CLI success/error paths and closed-tree
preservation. Root, nested-directory and leaf symlink checks ran on Linux;
that case is excluded on Windows, without inferring a native Windows pass.

| Actual checks (exit 0) | Result / log tag |
|---|---|
| GNU16.2.1 C++26 native / C++23 CPU builds | `build-dev-final`, `build-compat-final`; binaries frozen during tests |
| Full CPU/worker suites | `ctest --preset dev -j2`: **917/917**, 198.62 s (`ctest-dev`); `ctest --preset compat-cxx23 -j2`: **892/892**, 198.63 s (`ctest-compat`) |
| Native ASan/UBSan/default LSan | `build/asan/we_core_tests '[collider-diagnostic],[native-shape],[native-collider],[collider-export]'`: **37 cases /48,837 assertions**, 172.90 s, no reported sanitizer/leak errors (`asan-native`); ASAN/UBSAN/LSAN options unset |
| Exact LLVM23.1.1 format/tidy plus boundaries/docs/whitespace | Four C++ files pass (`style-final`) |
| Local Clang23.1.4/libc++23.1.1 C++23 syntax | Command, CLI and tests pass (`libcxx-command`, `libcxx-cli`, `libcxx-tests`); no native macOS pass inferred |

The retained plain and rotated products each contain a **29-file** package,
four-vertex/two-face/**487-byte** collider input, one shared shape/two copies,
request and actual report. Independently invoked CLI runs agree byte-for-byte
with shared command output. The Python binary/closed-inventory/hash oracle,
quaternion matrices and plane/Gram-barycentric segment oracle verify all **20
queries: eight hits /twelve misses**. Both builds retain the same cook/decoded
geometry identity. Observed local displacement is **0**, maximum matrix placement
residual **1.7764e-15 world units**, endpoint conversion **1.6293e-7**, fraction
difference **4.8850e-15**, returned point residual **0**. Independent limits remain
`1e-8` placement, `1e-4` local/endpoint and `2e-4` oracle fraction comparison.
`focused-complete` retains these under `R/product-final/{plain,rotated}`.
`cli-final-plain`, `cli-final-rotated` run the actual CLI; `product-oracle-complete`
checks their outputs and writes full metrics/identities to `R/product-oracle.json`.

| Retained identity | SHA-256 |
|---|---|
| Plain request | `71e155b0a580712e4eddcf9d0ef119315efdb74654dfb0b016a6e123719839c2` |
| Plain report | `7b64d1b65b994c246479cc4db53e3bb289a6563d22b2c3fb9b4e814694d8f301` |
| Rotated request | `e411124788e26eef028cc1278d3d1fd92b721fba49e76c08915dcf7b3e035e2d` |
| Rotated report | `0650d724e35774865942e23c3d2ed3d8341aa099e48a0c66e478dde80b6bcc86` |
| Shared canonical cook | `48f95acb5c5bd8e17fcd89b1e6f6c0821f41c18a1a9fdfa641e41c39d8dc2842` |

Changed privately admitted instance placement/revision refuses the old package;
a freshly admitted package succeeds. Rehashed stale source/pivot/frame/query
changes refuse, including a frame change smaller than the placement limit.
Explicit render LOD0/1 changes retain identical base-collider shape/queries.
The existing old-request/old-package replay remains valid as a frozen snapshot,
without claiming live authority. Query, parsing, inventory, symlink and CLI error
checks preserve caller/package bytes and leave walking/body installation false.

Style review required normal constant names and a const-reference fixture input;
warnings and bounds remained enabled. A probe rerun deliberately encountered the
existing export writer's refusal to overwrite its saved package (`focused-final`).
The retained outputs were preserved; the final probe uses a fresh ignored
`product-final` directory (`focused-complete`). No production assertion was
removed or weakened to obtain a pass.

Initial/final scans match all **378** protected source/capture/completed-product
hashes (`preservation-before`, `preservation-after`). No reconstruction, model provisioning, GPU/UI modification, platform
qualification or engine packaging ran. Runtime inventory recapture, source/rights,
producer/data/resource prerequisites and real AP gates remain open.
