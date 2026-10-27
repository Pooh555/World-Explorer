# Collider export companion — 2026-10-27

Scope: A0/AP0 remains **IN_PROGRESS**. An explicit schema-4 package retains exact
collider/base/cook/provenance, checked per-instance transforms and LOD0 identity
while sharing selected render resources. Ordinary schema-6 PBR scenes retain the
new source pins with collision unavailable and walking blocked. The
[protocol](../protocols/collider_exports.md) distinguishes this synthetic local
integration from backend shapes, real production/scale, engine mapping and AP
qualification. A1–A6/AP1–AP9 remain unstarted.

Repository root `/home/Pooh555/programming/World-Explorer` is the default cwd.
Parent: `c7b391644dd01fe7b0ce11f6a6c0a0e31639d650`. CMake + ten C++ files:
`8bf0b95cde406daf7039782264a25e837df3cd94bd0542ada7e7973a0faf023c`
(`R/source-identity.json` defines the sorted path/hash algorithm). Ignored command/result JSON,
logs and products are under `artifacts/validation/a0-collider-export-20261027/` (`R`).
`python3 R/run_check.py TAG COMMAND...` records actual argv/cwd/exit/elapsed time.

Focused verification passes **20 cases /20,030 assertions**, including LOD0/1
selection, structural graphs, rotated pivots, mixed assemblies, strict placement
limits, rehashed tampering and owned recovery (`focused-first`, exit 0).

| Actual checks (exit 0) | Result / command reference |
|---|---|
| GNU16.2.1 C++26 native / C++23 CPU builds | `build-dev-updated`, `build-compat`; binaries frozen during CTest |
| Full CPU/worker suites | `ctest --preset dev -j2`: **900/900**, 195.55 s; `ctest --preset compat-cxx23 -j2`: **875/875**, 193.45 s |
| Native ASan/UBSan/default LSan | `build/asan/we_core_tests '[collider-export],[native-collider],[native-assets],[native-export],[native-lod],[native-lod-export],[native-structure],[pbr-scene]'`: **71 cases /69,088 assertions**, 231.93 s, no reported sanitizer/leak errors; options unset (`asan-native`) |
| Exact LLVM23.1.1 format/tidy + docs/boundaries/whitespace | Ten C++ files pass (`style`) |
| Local libc++23.1.1 / Clang23.1.4 C++23 syntax | Companion helper, export, PBR admission and tests pass (`libcxx-helper`, `libcxx-export`, `libcxx-appearance`, `libcxx-tests`); no native macOS pass inferred |
| Pinned Khronos validator 2.0.0-dev.3.10 / Node26.12.1 | Eight LOD documents plus rotated assembly: **9 documents, zero errors/warnings** (`validators`, `validator-rotated`); cached validator/node identities unchanged |

`product-probe` sets `WE_COLLIDER_EXPORT_PROBE_ROOT=$PWD/R/product` on the actual
LOD/collider preview test, emitting two **35-file** packages and native bundles.
Each retains five collider vertices/four faces and **575 exact canonical bytes**
regardless of the selected five-vertex/four-face LOD0 or four-vertex/two-face LOD1
render mesh. Both copies share that input. `rotation-probe` emits a separate
29-file package with a rotated pivot and distinct rotated/scaled copy.

`product-oracle-run` independently checks closed inventories/hashes, original
base/root data, full oriented face permutations, provenance, canonical binary
re-encoding, glTF sharing and native binary source pins. A Python quaternion-matrix
oracle verifies every collider point/frame probe under each original pivot and
instance transform: default residual **0**, rotated maximum **3.9721e-15 arbitrary
world units**, within the selected `1e-8`. The collider companion is byte-identical
across render LOD selections; cook SHA is
`93bb3cad43faf723e41b2dc65a3c02fc4f86d06b33656bdaf3e788f9b7d28091`.
Companion SHA: `7ecc78fc7dd4399de88a2a26f0f2a038c40eeb67fc0ae8085e1dd2f849e30f60`.
`product-oracle.json` records all package identities. Regenerated legacy textured
**24 files**, structured **21 files**, both **30-file** LOD packages and their
complete PBR bundles match their frozen predecessors byte-for-byte.

RTX4060 Laptop native `we_gpu_pbr R/gpu-levelN R/product/level-N.wexplore` for N=0/1
passes **101 frames each, zero Vulkan validation errors**, including ordinary
replace/invalidate/delete. The unchanged independent
`a0-native-structure-20261027/pixel_oracle.py R/gpu-levelN` checks **393 /399 pixels**
over six views each: maximum **one RGB8 level** against preselected tolerance two.
`we_process validate` reopens both bundles successfully. Ordinary
`we_viewer --scene R/product/level-1.wexplore --settings R/viewer-settings.json
--frames 120 --screenshot R/viewer-level1.ppm` passes **120 frames**, four unique
vertices/two triangles/two copies, zero errors at 1428×868. Lossless PNG inspection
confirms the synthetic/draft/LOD1 labels, both copies, preview controls and walking
unavailable. This verifies render/source integration, without backend physics,
FPS, real A1 or AP qualification.

`build-dev.log` catches use of a codec-private length helper in the new exporter;
explicit `std::hypot` repairs it. `build-dev-tests.log` catches a dependent fixture
type; an explicit geometry reference repairs lookup. The walking check uses the
existing missing-measurement barrier. No production budget, warning or failed
case was weakened or removed.

Initial and final protected-data scans match all 378 hashes. Recipe/resource/capture
prerequisites, backend/convex cooking, engine execution and full AP/Q/N gates
remain open. No model installation, reconstruction or platform qualification ran.
