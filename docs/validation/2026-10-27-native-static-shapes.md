# Diagnostic native static shapes — 2026-10-27

Scope: A0/AP0 remains **IN_PROGRESS**. A backend-neutral immutable decoder and
application bridge now feed an actual source-bound Jolt static mesh owner, with
decoded compressed-triangle retention/loss checks, build identity and fresh
transformed single-sided ray queries. The [protocol](../protocols/native_static_shapes.md)
separates this synthetic arbitrary-unit diagnostic from scene-instance authority,
physics bodies, support ledgers, measured walking, caches and real AP qualification.
A1–A6/AP1–AP9 remain unstarted.

Repository root `/home/Pooh555/programming/World-Explorer` is the default cwd.
Parent: `b34abee41263ec31b61682dc2ea90af506d5468f`. CMake + eight C++ files:
`3f386d1fc3fc42f8714b2f6e046e0824b8f342b7f685fa8060ffa2fc3f2b4c76`
(`R/source-identity.json` defines the sorted path/hash algorithm). Ignored actual
argv/cwd/exit/time records, logs and products are under
`artifacts/validation/a0-native-shape-20261027/` (`R`).
`python3 R/run_check.py TAG COMMAND...` records each command.

`focused-final` passes **9 cases /14,335 assertions**: every truncated rehashed
prefix, malformed bounded geometry, all source pins, deterministic construction
and policy identity, sanitizer collapse, independently selected compression loss,
cancellation, original face mapping, backfaces and holes. The final source also
passes the native sanitizer run below and `product-final` (**1,596 assertions**).

| Actual checks (exit 0) | Result / log tag |
|---|---|
| GNU16.2.1 C++26 native / C++23 CPU builds | `build-dev-includes`, `build-compat-includes`; profiles frozen during tests |
| Final full CPU/worker suites | `ctest --preset dev -j2`: **909/909**, 174.82 s (`ctest-dev-final`); `ctest --preset compat-cxx23 -j2`: **884/884**, 174.59 s (`ctest-compat-final`); earlier pre-include-hygiene runs also pass |
| Native ASan/UBSan/default LSan | `build/asan/we_core_tests '[native-shape],[native-collider],[collider-export],[jolt-shape],[kinematic-capsule]'`: **36 cases /35,327 assertions**, 100.98 s, no reported sanitizer/leak errors (`asan-native`); ASAN/UBSAN/LSAN options unset |
| Exact LLVM23.1.1 format/tidy, docs/boundaries/whitespace | All **eight C++ files** pass (`style-all-final`) |
| Local Clang23.1.4 / libc++23.1.1 C++23 syntax | Neutral decoder, application bridge, new tests, corrected new/old Jolt shape TUs pass (`libcxx-input`, `libcxx-bridge`, `libcxx-tests`, `libcxx-shape-corrected`, `libcxx-old-shape`); no native macOS pass inferred |

`product-final` sets `WE_NATIVE_SHAPE_PROBE_ROOT=$PWD/R/product` on the actual
ring query path. Its **775-byte**, eight-vertex/eight-face input and report contain
real decoded Jolt triangles and **507 queries** across identity and two rotated,
translated/scaled frames. Jolt **5.3.0**, version ID **328448**, single precision
x86-64 SSE2, assertions/debug, 16-bit ObjectLayer is the tested configuration.
The development archive remains pinned by `cmake/dependencies.cmake`; this report
certifies neither another dependency profile nor a portable binary cache.

`product-oracle-final` independently decodes the binary input, reconstructs the
geometry/build hashes and adapter source selection, checks complete oriented
triangles and uses quaternion matrices plus plane/Gram-barycentric intersections
for every query. All **225 hits /282 misses** agree, reaching all eight original
source faces. Maximum local vertex displacement is **6.7435e-7 units**; decoded
ring area is **12.0000038147** versus original **12**. Maximum fraction difference
is **3.2512e-8**, reported endpoint conversion **5.7784e-7 world units**, returned
point residual **0**. Independent local/world policy limits remain `1e-4`, and
fraction comparison tolerance remains `2e-5`. This verifies the actual local
shape/query path, without contact, navigation or real geometry qualification.

| Retained identity | SHA-256 |
|---|---|
| Canonical collider input | `b12f5f964219388164e4fcc7f9bf9b7a62c824bc467b346558701cdef38d4a9b` |
| Adapter source selection | `bbfbc78f29e006766bf12c9aee44f1bbeeb934f9d7e19fab8ed4921ca35b45f9` |
| Decoded geometry | `3f1ed211ae93f2996505451f6e3883ab8a2e4cc0b5d65788415a86741c22466c` |
| Diagnostic construction | `44f72c955852e9badfb0f23e39c691c2a0b99106d8853e436601efe8f0e8d722` |
| Complete query/report JSON | `4c207443a21cf35dff64a343875df049c718038b2ffff4266df338ecef74ec78` |

Initial compilation exposed Jolt's unqualified `uint64` version-macro expansion
and a shadowed fixture constant; scoped upstream spelling and a distinct name
repair them. Exact style checks required direct span inclusion, unsigned masks,
normal naming/ownership and explicit optional guards. The libc++ probe exposed
Jolt math headers' transitive reliance on `<type_traits>`; the private integration
header now includes it explicitly and both new/old shape probes pass. Sandbox
ASan test discovery encountered LeakSanitizer's ptrace restriction; the native
build and run pass with leak detection enabled. Failing logs remain in `R`;
no warning, standard, production threshold or failed assertion was weakened.

Initial/final protected-data scans match all **378 hashes**. No reconstruction,
model provisioning, GPU/UI changes, native foreign-platform qualification or
engine packaging ran. Application instance selection/diagnostic exposure, bodies,
convex props, real source/scale/support and AP budgets remain open. Current runtime
inventory receipts still require recapture after relinking.
