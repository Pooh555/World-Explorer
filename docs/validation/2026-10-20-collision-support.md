# Synthetic collision support — 2026-10-20 (Asia/Hong_Kong)

M3 remains **IN_PROGRESS**. [The contract](../protocols/collision_support.md) adds
immutable source/frame/pose-bound diagnostic space support, explicit
unknown/free/occupied cells, conservative flat-floor coverage and swept capsule
envelope probes. Shared CLI inspection publishes an ordinary labeled viewer
scene. This is synthetic software evidence; no measured collision, Jolt walking
or Q5/N2/asset/platform gate is certified.

Repository root is the default cwd; evidence is
`artifacts/validation/m3-collision-support-20261020/` (`R`). Parent commit is
`8cc2711eb4a049df627fbcd17fb3bba1704c8b2a`.
`final-source-identity.json` pins final changed sources, binaries and scripts;
actual command receipts distinguish earlier/final viewer binaries.

## Inputs and visible behavior

`we_collision_fixture` generates an analytic aligned source, support and request.
No capture is read. Source manifest SHA
`54ae971ceff12003c340ca7710c8fcfefb3cb0c36e6c9a02ac9124ca299da8dd`;
request SHA `8262f43682297ad1765db386be4e5eb8ec24479caaa1742596f3efeca059902c`;
support SHA `621779efb21c5cb0c2b073ae280d38fe61d200e518bdf08f2940fcedb83e0540`.
The shared inspection produces **240 free / 12 occupied / 4 unknown cells** and
**56/64 supported floor tiles**. Four probes report `CLEAR_DIAGNOSTIC`,
`OCCUPIED`, `UNKNOWN`, `UNSUPPORTED_FLOOR`, checking **2 / 5 / 1 / 0 cells**.
The diagonal seam remains conservatively unsupported; neither spatial allocation
nor multiple adjacent triangles automatically fills it.

Diagnostic manifest SHA
`c05583b8ce6973fa1d745e4c5cd0ff062447b21bb0a8e2a7aad6ca38a5fc0ea3`.
The native view shows **320 synthetic splats**, color-coded space and amber
unsupported tiles, a visible legend and disabled walking. Captures are retained
in R and inspected locally. These samples visualize labels, not solid boundaries.
`products.json` pins complete fixture/inspection files. Source corrections stop
queries; support is never installed as collision in the original scene.

The full real regression uses
`artifacts/validation/m3-mesh-job-20261020/full-job/stages/bake-1/output`, manifest
SHA `0b873804a3d5aa2a02c95245a43c550ed4d2ba2240d3af1e4325e23c198c6ee2`.
All **39,373 vertices / 49,213 triangles** remain. It is still visibly fragmented
captured appearance, arbitrary scale and unknown gaps. The new command refuses
it with no output directory. Its repeated GLB is byte-identical, SHA
`2e3a55b213237d48e73975513ff30eb2f114dfbfd61675fbf636cda3b97f5e24`.
No MVS, fusion, solve, completed bake or refit was rerun.

## Actual checks

`local.py`, `profiles.py`, `native.py`, `native_final.py`, `sanitizers.py` and
`finish.py` in R own exact commands, profiles, timings, hashes and raw logs.
Successful checks exit 0; explicit CLI refusals exit 2 as expected.

| Check | Result |
|---|---|
| CMake development C++26, explicit compatibility C++23, separate ASan CPU builds | Pass; GNU 16.2.1, existing presets; no standard or warning reduction |
| `ctest --preset dev`; `ctest --preset compat-cxx23` | **429/429**, **405/405**, **138.4 / 110.9 s** |
| `we_core_tests '[collision-support],[world-frame],[gltf],[sequence_texture],[mesh_texture],[mesh]'` | **52 cases / 95,438 assertions** on development, compatibility and native ASan/UBSan/LSan |
| New support/manifest-pin cases | **10 cases / 798 assertions**, including **324 bidirectional sweeps** against an independent analytic slab oracle; parsing/limits, floor winding/lattice/seams, boundaries/headroom, cancellation, source correction and failed publication preservation |
| Shared fixture → inspection → native reload | All four probe outcomes and counts match; existing/overlapping outputs, malformed requests and real input refused |
| Native fixture/inspection sanitizer parity | All **7 fixture / 5 inspection files** exactly reproduce development; leak detection enabled, no reported sanitizer errors |
| Native diagnostic / full real viewer | Final **90 / 448 frames**, zero Vulkan errors; screenshots inspected |
| Native `we_gpu_mesh R/gpu-real REAL_BUNDLE` | RTX 4060 Laptop GPU, **79 frames**, zero Vulkan errors; retained texture/splat alignment comparison within one RGB8 level, complete real geometry |
| Pinned C++ style, Black, docs, whitespace | **11 changed C++ files**, clang-format/tidy **23.1.1**; full pinned Python check and documentation/boundary/whitespace checks pass |
| Preservation | **24,457 protected files**, zero new differences; **3,982** pre-existing external-header changes retained; four original bakes, prior audits, **152 sequence inputs**, raw/baked sources and previous aligned products preserved |

The loader's optional external manifest pin checks the exact bytes decoded.
A whitespace-only manifest change is refused under the old pin while unpinned
legacy loading preserves scene identity. Existing bundle/GLB encoding is unchanged.

## Failures and remaining scope

The first eight-case CPU run passed 140/141 assertions: the intended unsupported
fixture point was fully covered. The test and diagnostic query were corrected to
a tile crossing the seam; production coverage logic was unchanged by this fix.
Tidy required explicit optional checks and a structured private visual-sample
input; warnings and numerical gates were not weakened. Code review replaced a
separate pre-load manifest hash with the exact decoded-byte pin above.

The user has no independent sample_1 distance/up reference. Real support
production, measured alignment/ground truth, general floors/slopes/steps, Jolt
capsule movement and full Q5/N2 remain open. A0–A6/AP remain NOT_STARTED and M4
has not begun. Next work is observation-backed free-space evidence, with unknown
coverage and arbitrary units retained; current task details live in progress.
