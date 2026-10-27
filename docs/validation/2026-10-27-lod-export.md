# Native LOD export and preview — 2026-10-27

Scope: A0/AP0 remains **IN_PROGRESS**. An explicit additive native export profile
retains all admitted LOD geometry/certificates and complete face provenance,
bounds every level's conversion, shares selected individual/assembly resources,
and compiles the selected level through ordinary schema-6 native PBR resources.
The [protocol](../protocols/native_lod_exports.md) separates this synthetic local
integration from real production, general decimation, streaming, Godot and AP8
silhouette/runtime qualification. A1–A6/AP1–AP9 remain unstarted.

Repository root `/home/Pooh555/programming/World-Explorer` is the default cwd.
Parent: `5a181ac4d6f76ca7f7f23b28bbb923d79c5661cb`. Eight-file C++ source identity:
`186bb9392820625cc5b6d74f4c0d6507f9d3acace8f8c2fee0c559c9ee6f9bb0`
(`R/source-identity.json` defines the sorted path/hash algorithm). Ignored command/result JSON,
logs and products are under `artifacts/validation/a0-lod-export-20261027/` (`R`).
`python3 R/run_check.py TAG COMMAND...` records actual argv/cwd/exit/elapsed time.

The source fixture reduces a textured mesh from five vertices/four triangles to
four vertices/two triangles, with observed and inferred faces, two material groups
and two shared instances. Tests check every level/certificate, exact selection,
unused-resource substitution, float bounds, source pins, publication recovery,
bundle reload and ordinary scene replacement. Structural bindings remain LOD0-bound.

| Actual checks (exit 0) | Result / command reference |
|---|---|
| GNU16.2.1 native C++26 / C++23 CPU builds | `build-dev-final`, `build-compat-final`; binaries frozen during CTest |
| Full CPU/worker suites | `ctest --preset dev -j2`: **880/880**, 281.13 s; `ctest --preset compat-cxx23 -j2`: **855/855**, 277.15 s |
| LOD export/runtime focus | `build/dev/we_core_tests '[native-lod-export]'`: **10 cases /9,409 assertions** (`focused-final`) |
| Native ASan/UBSan/default LSan | `build/asan/we_core_tests '[native-lod-export],[native-export],[native-structure],[pbr-scene]'`: **33 cases /26,456 assertions**, 215.13 s, no reported sanitizer/leak errors (`asan-native`); options unset |
| Exact LLVM23.1.1 format/tidy + docs/boundaries/whitespace | Eight C++ files pass (`style-final`) |
| Local libc++23.1.1 / Clang23.1.4 C++23 syntax | Export, codec, PBR admission and LOD tests pass (`libcxx-export`, `libcxx-codec`, `libcxx-appearance`, `libcxx-lod-tests-complete`); no native macOS pass inferred |
| Khronos validator 2.0.0-dev.3.10 / Node26.12.1 | **8 documents, zero errors/warnings/infos** (`validators`); original pinned package files and node SHA unchanged |

`product-probe` sets `WE_LOD_EXPORT_PROBE_ROOT=$PWD/R/product` on the actual native
LOD preview test. It writes both 30-file packages and schema-6 bundles; the shared
CLI `we_process validate` passes both bundles (`validate-level0/1`).
`product-oracle-run` independently checks closed inventories/checksums, canonical
definition identity, original receipt geometry/UV/normal/tangent/index arrays,
complete face provenance/certificate bytes, individual/assembly sharing, both
world placements and native binary source pins. Each package retains 5-vertex/
4-face LOD0 and 4-vertex/2-face LOD1, with two instances and eight shared PNGs.
All geometry/certificate bytes match across selections. Regenerated legacy
textured **24 files** and structured **21 files** match their frozen predecessors
byte-for-byte. `product-oracle.json` records package, companion, geometry and bundle
hashes; selected package manifests are
`58263c4f3d799aea66e973fb48d405a69c094d48a0da15d3a3424305dc1451b9`
(LOD0) and `8c6431499e92666fde5872797b6c4b08e7e0593f12893882d3815fda86f02e63` (LOD1).

Native RTX4060 Laptop checks use `we_gpu_pbr R/gpu-levelN
R/product/level-N.wexplore` for N=0/1: **101 frames each, zero Vulkan validation
errors**, initial/replacement/invalidation/deletion. The unchanged independent
`a0-native-structure-20261027/pixel_oracle.py R/gpu-levelN` passes **393 /399 pixels**
over six views each, maximum **one RGB8 level** against preselected tolerance two.
`we_viewer --scene R/product/level-1.wexplore --settings R/viewer-settings.json
--frames 120 --screenshot R/viewer-level1.ppm` passes **120 frames**, four unique
vertices/two triangles/two shared copies, zero validation errors at 1428×868.
Visual inspection of its lossless PNG confirms `Asset 42: LOD 1`, synthetic/draft
labels, both copies, preview controls and walking unavailable. These are local
synthetic rendering checks, without AP8 FPS/silhouette or real A1 qualification.

Failures retained: `build-dev.log` rejects a C++26 ranges comparison with a string
literal; an explicit string repairs it. `build-dev-first.log` catches use of
`std::erase_if` on JSON while its correction was being applied; explicit iterator
erase repairs the test. `focused-first.log` passes eight of nine initial cases:
the ninth fixture exceeds the existing transform budget before testing float
loss. Reducing its pivot/compensating translation keeps the same rounding error
inside the original limits. No production limit or warning was weakened.

Initial and final scans match all 378 protected hashes. Real capture/recipe/resource prerequisites,
collider adoption, producers and full AP gates remain open. Native/reference
platform qualification has not begun.
