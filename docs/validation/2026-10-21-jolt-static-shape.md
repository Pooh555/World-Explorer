# Pinned Jolt static shape — 2026-10-21 (Asia/Hong_Kong)

M3 remains **IN_PROGRESS**. This slice pins Jolt 5.3.0 to the existing immutable
vcpkg baseline, builds its CPU static library inside the private `collision`
target, and creates a source-bound in-memory mesh shape from the existing
canonical synthetic floor triangles. A project-owned diagnostic segment ray
cast proves the native backend is used. The shape is not installed in the
viewer or source scene; there is no explicit barrier mesh, Jolt character
controller, real clearance or measured walking. See the
[contract](../protocols/collision_cook.md) and
[version/license review](../research/jolt_dependency.md).

Repository root is the default cwd; parent commit is
`806ca62449d62542d5b10a43f9f8d20fedf7b01e`. Ignored logs and captures
are in `artifacts/validation/m3-jolt-20261021/` (`R`). The official v5.3.0
archive SHA-256 is
`e7f9621e480646c434150e1fbe3a9410f4ec4b04ffe54791e0678326b741b918`;
its exact retained MIT notice SHA-256 is
`800abe35d64ad9defd636ff1ee8c961e06f0ebca3ef8d10083e8aa0e8ef86ac3`.
The pinned vcpkg registry's Jolt port declares 5.3.0. No downloaded source,
generated product or local capture was committed.

`build/dev/we_core_tests '[jolt-shape],[collision-cook]' --reporter compact`
passes **5 cases / 81 assertions**: Jolt's static mesh accepts both selected
floor triangles, a descending segment hits at the expected fraction, an
above-floor segment misses, and zero-length or precision-losing rays fail. Corrected revisions
and pre-cancellation reject without replacing or damaging the prior owner.
The backend's own mesh sanitization is checked for triangle loss; any discarded
selected triangle fails construction. Queries revalidate the exact source.
The occupied/unknown grid remains in the separate conservative diagnostic
ledger; a geometric floor ray hit does not establish a walkable region.

`cmake --preset dev`, `cmake --build --preset dev -j 4` and
`ctest --preset dev --output-on-failure -j 4` pass on GNU 16.2.1/C++26:
**450/450** CPU and worker checks. The explicit
`cmake --preset compat-cxx23 -DWE_BUILD_VIEWER=OFF`, `cmake --build --preset
compat-cxx23 -j 4` and `ctest --preset compat-cxx23 --output-on-failure -j 4`
pass on GNU 16.2.1/C++23: **425/425**. The focused native command
`ASAN_OPTIONS=detect_leaks=1 build/asan/we_core_tests
'[jolt-shape],[collision-cook],[collision-support]' --reporter compact`
passes **15 cases / 879 assertions** without reported ASan/UBSan/LSan errors.
Jolt itself is instrumented in this sanitizer build.
The pinned C++ style gate, full tracked Python Black, documentation links,
include boundaries and whitespace checks pass. R holds raw suite/build/style
logs and exact command results.

The retained synthetic inspection manifest stays
`c05583b8ce6973fa1d745e4c5cd0ff062447b21bb0a8e2a7aad6ca38a5fc0ea3`.
`SDL_VIDEO_DRIVER=x11 build/dev/we_viewer --scene
artifacts/validation/m3-collision-cook-20261021/inspection/diagnostic.wexplore
--frames 90 --settings R/viewer-settings.json --screenshot
R/diagnostic-visible.ppm` exits 0 on the NVIDIA RTX 4060 Laptop GPU:
**320 synthetic splats, zero Vulkan validation errors**. The screenshot SHA is
`e3afa007da2ac3645413372a604674781f5b113abffd92c416809c881ef9d637`.
Visual inspection shows the green/red/blue/amber diagnostic labels, arbitrary
scale, and walking unavailable. No real solve, fusion, bake or refit was rerun.

The first sanitizer link failed because Jolt's default no-RTTI build lacked
type information for UBSan vptr checks. Enabling Jolt's `CPP_RTTI_ENABLED` and
the vcpkg `rtti` feature fixed the link without reducing sanitizer coverage.
Sandboxed Catch2 discovery then hit the known LeakSanitizer/ptrace limit;
native-access discovery and focused execution passed. The dated dependency
inventory passes its tracked-input check (7 profiles, 86 components, 257 open
gaps); broad `--local-evidence` currently reports a changed runtime payload
after these native rebuilds and does not certify current loader mappings.
Release-vcpkg, native Windows/macOS, explicit wall shapes, 120 Hz capsule
movement and Q5/N2 remain unverified. All 5,184 retained real cells are still
unknown. The internet-sourced `sample_1` has no independent distance/up
reference; ADR 020's local software exit and A1's independent-scale pilot stay
separate from full measured gates.
