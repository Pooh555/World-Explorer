# Native PBR scenes and viewer — 26 October 2026

**A0/AP0 remain IN_PROGRESS.** The admitted native textured export now compiles
into source-pinned immutable scene resources, schema-6 bundles and shared Vulkan
mesh/material/instance draws. The ordinary viewer loads these bundles and exposes
preview lights. This is synthetic renderer/resource validation, without producer
execution, a real completed environment, material recovery, physical relighting,
Godot import or A1/AP6/AP7 qualification. M4 has not begun.

Cwd: repository root; timezone Asia/Hong_Kong. Initially clean parent
`dbc1d0fda309eab467a4755778ade18596943bff`.
`R=artifacts/validation/a0-pbr-viewer-20261026` (ignored).
`source-identity.json` contains SHA-256 for CMake, 21 C++ files and two shaders;
the compact sorted path/hash-map SHA is
`8925565d0753f084f3bebc1decf68d945f604f6e86b52f19f5fbf8d29f05bd21`.
The [protocol](../protocols/native_pbr_scene.md) owns limits, color/BRDF policy,
compatibility and failure semantics. No dependency, warning or standard is lowered.

## Integration and checks

Source definition/material/export/scene identities survive compilation, bundle
roundtrip and ordinary scene updates. One geometry/image set is shared by the two
instances; unique geometry counters remain four vertices/two triangles. The new
hash/binary branch is additive; schemas 1–5 retain their encodings. Mutation tests
cover pixels, UVs, group assignments, winding/handedness, sampler, source frame,
labels, pose/epoch, scale, identities and manifest pins; bounded decode/truncation,
replay, cancellation and geometry invalidation are exercised.

Every command/result/elapsed time below has a matching `$R/TAG.json` and `.log`.
Native GPU/sanitizer commands run outside the filesystem sandbox where required.

| Command (from repository root) | Result | Tag |
|---|---|---|
| `cmake --build --preset dev -j 4 --target we_core_tests we_gpu_pbr we_gpu_mesh we_viewer`, followed by incremental source/UI builds | Pass | `build-final`, `build-winding`, `build-source-final`, `build-viewer-final` |
| `WE_PBR_SCENE_PROBE_OUTPUT="$PWD/$R/synthetic-reviewed.wexplore" build/dev/we_core_tests 'native PBR scene compilation preserves actual geometry images instances and source identity'` | Pass / 1 case, 510 assertions; actual export→compile→bundle path | `probe-reviewed` |
| `build/dev/we_gpu_pbr "$R/gpu-reviewed" "$R/synthetic-reviewed.wexplore"` | Pass / 101 frames, initial/replacement/invalidation/deletion, zero Vulkan errors | `gpu-reviewed` |
| `python3 "$R/pixel_oracle.py" "$R/gpu-reviewed"` | Pass / 399 pixels, maximum error 1 RGB8 level against tolerance 2 | `pixel-reviewed` |
| `build/dev/we_gpu_mesh "$R/legacy-mesh"` | Pass / existing captured texture, RGB/UV/unknown, mesh depth/update/clip/rebase checks; zero Vulkan errors | `legacy-mesh` |
| `build/dev/we_viewer --scene "$R/synthetic-final.wexplore" --settings "$R/viewer-settings.json" --frames 120 --screenshot "$R/viewer.ppm"` | Pass / ordinary visible load, both instances, synthetic/draft labels, zero Vulkan errors | `viewer-visible` |
| `python3 "$R/native_controls_second.py"` | Pass / native exposure/intensity/direction clicks and six viewer readbacks, prior focus/cursor restored | `controls-second-check` |

The device is **NVIDIA GeForce RTX 4060 Laptop GPU**, with validation enabled.
`pixel_oracle.py` independently ray-intersects admitted triangles, transforms
instances, interpolates source attributes and samples raw images; double-precision
BRDF/transfer calculations predict actual PPM pixels. Its tolerance was fixed at
**2 RGB8 levels before the first comparison**. Both triangles, both instances and
six key/ambient lighting views are sampled; invalidation must leave a uniform
background. This numerical preview check is not a novel-light quality metric.

Native control readbacks verify zero exposure removes surface radiance, restored
exposure recovers it and key intensity/direction visibly change the viewport.
`native-controls-second.json` retains input coordinates and changed-pixel counts.
The final UI places labels above full-width sliders. Neither run measures FPS,
production budgets, relighting fidelity or native Windows/macOS behavior.

CPU builds use **GCC 16.2.1**, CMake 4.4.4 and Ninja 1.13.2. Final development
CTest (`ctest --preset dev -j 2`, `ctest-dev-final`) passes **847/847**;
explicit C++23 CPU/viewer-off (`ctest --preset compat-cxx23 -j 2`,
`ctest-compat-final`) passes **822/822** after `build-compat-reviewed`.
`focused-final` runs `build/dev/we_core_tests
'[pbr-scene],[native-export],[native-assets],[gltf],[bundle],[bundle_admission],[render]'`
and passes **67 cases /190,265 assertions**. The identical filter passes
**67 cases /190,265 assertions** in the native ASan/UBSan profile
(`build-asan`, `build-asan-reviewed`, `asan-focused`), with default LeakSanitizer
and no reported errors; all sanitizer option variables remain unset
(`sanitizer-options.json`). Clang 23.1.4 with retained libc++ 23.1.1 headers passes C++23 syntax probes
for all four new implementation/test TUs (`libcxx-scene-reviewed`,
`libcxx-application`, `libcxx-core`, `libcxx-test`); this is not a full libc++/Jolt
build or macOS qualification. Exact LLVM **23.1.1** format/tidy repository gates
pass for 19 files (`style-reviewed`) and both updated legacy tests
(`style-bundle-tests`), covering all 21 changed C++ files.

The reviewed bundle manifest SHA is
`cdf6b116853cc91d0939d0bad98b688315f69b79cb1fe3caeb95fec3e7c1a4e6`;
`validate-bundle` reopens scene 90/revision 1, content SHA
`b9b7c23de70eadd26b64de657c9d84eb6b7c68be56254c6434d288e341283da3`.
The earlier `synthetic-final.wexplore` used by native input has identical files.
`artifact-hashes.json` pins both bundles, GPU outputs and ordinary viewer readbacks.
`preservation-check` rehashes **378 protected files with zero mismatches**;
`previous-exports.json` verifies the prior generated GLB/manifest and all 24 files
of the original pinned native textured package. No saved product is replaced.

## Failures and remaining qualification

Retained early build logs show missing direct includes, an accidental private JSON
dependency in render_core, a temporary path lifetime warning and wrong bundle API
names in the test. Repairs use existing APIs/direct dependencies without warning
suppression. `style-first` and `style-final` retain naming/swappable-parameter
findings; named push members and a compile-time word width repair them.

`gpu-first` fails calibration after compositor resize; calibrated GPU diagnostics
now use a fixed-size hidden surface, while the ordinary viewer uses its actual
visible framebuffer. The first pixel reference fails by **180 RGB8 levels**:
clockwise front classification inverted mapped normals. Counterclockwise fronts
repair it; failed `gpu-second/pixel-oracle.json` is retained. No tolerance changes.
The full initial dev/C++23 suites each reject two old tests that use schema 6 as
an unknown future version. Those fixtures now select schema 7, with an additional
mutation requiring a legacy payload falsely declaring schema 6 to fail as invalid
data. The passing final suites retain both error-code and preservation assertions.
The first native input harness misses widgets after moving a different pointer;
its assertion fails and readbacks remain. The passing rerun uses the native
virtual pointer for absolute movement and verifies widget coordinates before
clicking. These failures are not counted as successful checks.

`docs-final` (`python3 tools/check_docs.py`) passes 1,622 local targets in
307 files; `diff-final` (`git diff --check`) passes.

Model installation, inference, real video processing,
Godot and runtime dependency-inventory refresh do not run. Earlier runtime receipts
remain stale after relinking. Existing generated/captured profiles are retained;
real capture/recipe/rights/resource prerequisites and the remaining §11.4 contracts
stay open. The [handoff](../progress.md#current-a0-work) owns the next executable
heterogeneous channel-provenance task.
