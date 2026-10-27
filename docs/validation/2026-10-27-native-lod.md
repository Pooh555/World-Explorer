# Certified native LOD admission — 2026-10-27

Scope: A0/AP0 remains **IN_PROGRESS**. The application admits an independently
pinned exact affine fan certificate between actual base/candidate geometry
handles, binds every LOD face's provenance, and incorporates the certificate in
definition/instance identity and owned metadata reload. The
[protocol](../protocols/native_lods.md) defines the bounded dyadic proof and
explicit downstream export refusal. This synthetic contract slice does not
produce real assets, implement runtime LOD selection/streaming, measure AP8
silhouette/performance or qualify another platform. A1–A6/AP1–AP9 remain unstarted.

Repository root `/home/Pooh555/programming/World-Explorer` is the default cwd.
Parent: `77f561f4395f9e6b07377ea1d4ded2d7dc6f99c7`. Source identity (CMake + eight C++ files):
`06a3fd9f8bf1ac62a521c680e13675722f1b608edb198611023e0051d06740a0`;
`R/source-identity.json` defines the sorted path/hash algorithm. Ignored logs and command/result
JSON are under `artifacts/validation/a0-native-lod-20261027/` (`R` below).
`python3 R/run_check.py TAG COMMAND...` records argv, cwd, exit and elapsed seconds.

The fixture reduces a textured asymmetric runtime mesh from five vertices/four
triangles to four vertices/two triangles. Three observed faces become their exact
affine outer triangle; a separate inferred face and both material groups remain.
Rehashed invalid certificates test omitted faces, malformed fans, changed UVs,
materials/evidence, unsafe weights, schema/context/source mismatch and manual
ancestry. Independent inputs, cancellation, exact arithmetic refusal, shared
instance invalidation, atomic reload/replay and retained export refusal are
exercised through the actual public APIs.

Final focused verification: `build/dev/we_core_tests '[native-lod]'` passes eight
cases / 12,838 assertions (`focused-complete`, exit 0). GNU16.2.1 C++26 development
and explicit C++23 CPU builds pass (`build-dev-complete`, `build-compat-complete`).
Local libc++23.1.1/Clang23.1.4 C++23 syntax probes pass for the LOD reader, native
asset reader and LOD tests (`libcxx-lod-complete`, `libcxx-assets`,
`libcxx-tests-complete`); these do not certify native macOS.

`product-probe` runs the actual publication/reopen test with
`WE_NATIVE_LOD_PROBE_OUTPUT=$PWD/R/product`. Its independently reopened receipt
and owned metadata retain two shared instances. `product-oracle-run` checks all
receipt/metadata blob hashes, every original face, 2,244 exact-rational position/
UV/normal/tangent interpolation samples and both instance definition hashes.
Receipt SHA: `2dda5be5497ec7c9aafb68e2b8c41717ec0ea9a47470c270d1ae416bb58908da`;
certificate SHA: `83345324a8913a86a1fd9fbde325b0c072bb9160cbd2815378c5347ce9a4bf04`;
metadata manifest SHA: `caf8625daba0e9b276e6a5e30adf99ea25ae4dd222b68884d054e4bb8c87b4ef`.
These are synthetic development assets, not portable real reconstruction output.

`legacy-export-probe` regenerates the previous native textured profile; all 24
files match the frozen hashes byte-for-byte (`product-oracle.json`). The native
command `build/dev/we_gpu_pbr R/gpu-regression
artifacts/validation/a0-native-structure-20261027/synthetic.wexplore` passes on
RTX 4060 Laptop: 101 frames, six fixed views, initial/replace/invalidate/delete,
zero Vulkan validation errors (`gpu-regression`). The unchanged independent
`a0-native-structure-20261027/pixel_oracle.py R/gpu-regression` checks 201 pixels:
maximum one RGB8 level versus preselected tolerance two (`pixel-oracle`). This
regresses the supported no-LOD PBR path, not LOD rendering or performance.

Exact LLVM23.1.1 format/tidy plus documentation/whitespace gate passes eight C++
files (`style-complete`, exit 0). `ctest --preset dev -j2` passes 870/870 in 311.18 s;
`ctest --preset compat-cxx23 -j2` passes 845/845 in 306.96 s (`ctest-dev`,
`ctest-compat`, exit 0). Both binaries remained frozen during their full suites.
The final native ASan/UBSan build passes (`build-asan-complete`).
`build/asan/we_core_tests '[native-lod],[native-assets],[native-export],[native-structure]'`
passes 37 cases / 35,552 assertions in 196.71 s, with no reported sanitizer/leak
errors (`asan-native`, exit 0). ASan/UBSan/LSan options remain unset; default leak
detection is retained (`sanitizer-options.json`). Native device/sanitizer access
was used only for these scoped checks; no environment or dependency was changed.

Failures retained: `style.log` reports nine unchecked-optional-access findings;
explicit prerequisite branches, the existing selected-value helper and explicit
test selections repair these without warning suppressions. `style-final.log`
then reaches clean clang-tidy but fails documentation links while this report
was not yet present. `style-complete` passes after adding this report, with no suppressions.

The initial and final protected-data scans verify all 378 selected hashes. Original media,
captures, solver/fit outputs and worker environments remain protected. Real
capture/recipe/resource prerequisites, general decimation, native LOD export and
preview, colliders, Godot and full AP gates remain open.
