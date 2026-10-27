# Native structural graph adoption — 27 October 2026

**A0/AP0 remain IN_PROGRESS.** The native path now binds independently admitted
partial structure graphs to exact structural definitions, adopted geometry and
all instance copies. It atomically publishes original graph roots and the binding
with asset/scene metadata. An explicit structured export envelope retains their
bytes and native PBR source pins. This synthetic scope qualifies no estimator,
physical opening, closed room, gravity/support, walking, real A1 or AP gate.
M4 has not begun. The [protocol](../protocols/native_structure.md) owns invariants.

Cwd: repository root; timezone Asia/Hong_Kong. Initially clean parent
`d35148fd10eaacbc76caf6135be2900f269cd0f4`.
`R=artifacts/validation/a0-native-structure-20261027` (ignored).
`source-identity.json` contains CMake and all eight changed C++ file hashes;
compact sorted path/hash-map SHA-256:
`db6660ec42ab83392abbb16a65f4980671b86e3a45890e3dad42c429b334eb5f`.
No dependency, standard, warning, threshold or sanitizer setting is weakened.

## Integration and checks

Selections must retain complete graph surface/opening/room/relation sets and every
copy of each structural definition. Wrong receipt/context/geometry/definition,
missing/aliased elements, stale instances, nonstructural targets and forged export
or runtime pins fail. Frames compose instance placement with the rigid pivot;
noncommuting rotation checks preserve original shell placement. Shared exact graph
roots remain one immutable record without merging semantic assets/instances.

Metadata checks exercise policy-only binding conflicts at unchanged instance hash,
scene movement/revision conflicts, cancellation, selected history/reload and exact
retry. Original room/support refs remain opaque in the old native path. Graph
manual ancestry propagates to the structured assembly/runtime aggregate without
relabeling geometry/material regions. Tests retain physical-evidence limits.

Commands below run from repository root. Wrapped commands retain exact argv/exit/
elapsed time in `$R/TAG.json`, with `.log` output. Direct native sanitizer commands
retain output and completion records separately.

| Command | Result | Evidence tag |
|---|---|---|
| `cmake --build --preset dev -j 4` | Pass / all targets | `build-dev-complete` |
| `cmake --build --preset compat-cxx23 -j 2` | Explicit C++23 CPU/viewer-off build | `build-compat-complete` |
| `build/dev/we_core_tests '[native-structure],[native-assets],[asset-channels],[native-export],[pbr-scene],[structure-graph]'` | **56 cases /45,225 assertions** pass | `focused-complete` |
| `WE_STRUCTURE_SCENE_PROBE_OUTPUT="$PWD/$R/synthetic-complete.wexplore" WE_STRUCTURE_EXPORT_PROBE_OUTPUT="$PWD/$R/export-complete" build/dev/we_core_tests 'native structured export retains exact roots maps and ordinary PBR resources'` | Pass / actual graph→binding→export→bundle | `product-complete` |
| `python3 "$R/product_oracle_complete.py"` | Pass / closed 21-file inventory, graph topology, source pins and exact world placement | `product-oracle-final` |
| `build/dev/we_gpu_pbr "$R/gpu-final" "$R/synthetic-complete.wexplore"` | Pass / 101 frames, initial/replacement/invalidation/deletion, zero Vulkan errors | `gpu-final` |
| `python3 "$R/pixel_oracle.py" "$R/gpu-final"` | Pass / 201 sampled pixels, maximum 1 RGB8 level against preset tolerance 2 | `pixel-final` |
| `build/dev/we_viewer --scene "$R/synthetic.wexplore" --settings "$R/viewer-settings.json" --frames 120 --screenshot "$R/viewer.ppm"` | Pass / ordinary visible load, two shells, synthetic/draft labels, zero Vulkan errors | `viewer-product` |

GCC **16.2.1** uses C++26 development and explicit C++23 CPU profiles.
`ctest --preset dev -j 2` passes **862/862** (`ctest-dev-final`);
`ctest --preset compat-cxx23 -j 2` passes **837/837** (`ctest-compat`).
The rebuilt UI test executable also passes **25 cases /645 assertions**
(`viewer-tests`), and the shared CLI validates the final bundle (`validate-product`).
The identical focused filter passes **56 cases /45,225 assertions** natively under
ASan/UBSan with default LeakSanitizer (`build-asan-complete`, `asan-focused`), with
no reported errors and no option overrides (`sanitizer-options.json`).
Clang 23.1.4 with retained libc++ 23.1.1 headers passes C++23 syntax checks for
native structure, export, PBR scene compilation/appearance and the new test
(`libcxx-{structure,export,scene,pbr,test}`); this is not a full libc++/Jolt build.
Exact LLVM **23.1.1** repository formatting/tidy checks pass all eight C++ files
(`style-complete`). No native Windows/macOS/GCC 13 pass is inferred.

The independent Python oracle reads raw glTF buffers, source-face mappings,
original graph/binding JSON and the native binary tail. It confirms eight shared
vertices/eight triangles, one WALL surface, one INFERRED hole opening/room link,
two qualified copies and exact world-frame agreement (maximum component error
**0** on this fixture). Directed triangle boundaries equal both stored loops;
local signed areas are 16 and −4, giving area 12 in arbitrary squared units.
This is numerical/topological evidence, never observed void or metric accuracy.
Original graph classes/confidence stay intact. Runtime stores hashes, rather than
embedding graph roots or adding room/provenance inspection UI.

Graph SHA: `ecfa61c059caa79d0930900076afc702543eafc387eb64f81e86d2564e6c65b3`;
binding: `52da249247bf3b746e25fd19d384e43722f5f63f50de265601c29d58389600fa`;
manifest: `b0aeaffa094603b733fb9d3d9b62eef26192744860060eecb6d8f39ab94169c0`;
bundle manifest: `69ea0672426d01a13af86ad7879d1a5986388cad9d753c50c578a91cc12af74f`.
Final source regenerates identical products to the first probe. The fresh ordinary
native export probe also reproduces **all 24 pre-structured package files
byte-for-byte** (`legacy-export-probe`, `legacy-byte-identity.json`). Existing
mesh/material resources are unchanged within the structured envelope except the
assembly's explicit profile/manual labels and terminal manifest/added sidecars.

## Corrections, preservation and limits

`build-first`/`build-second` reject sorting nlohmann JSON as a random-access range;
a bounded vector of rows fixes canonical ordering. `build-third`/`build-fourth`
reject omitted fixture function fields under `-Werror`; explicit initialization
and named fixture options fix them. `style-first` rejects one map-key constant
name; `K_RECORD_KEY` repairs it without suppression.

The first full CTest overlaps a binary relink and then reports executable-launch
`permission denied` / BAD_COMMAND failures. Those tests are **not** passes. The
retained `ctest-dev` exits 8; the frozen-binary rerun is `ctest-dev-final`, with no
concurrent rebuild of that profile. No product-data failure is hidden by this
harness correction. Default sanitizer/leak options remain unchanged.

`docs-final` (`python3 tools/check_docs.py`) passes 1,647 local targets in 311
files; `diff-final` (`git diff --check`) passes.

`preservation-final` rehashes **378 protected files with zero mismatches**.
No saved source/result is replaced, video processed, model installed, inference
run, Godot imported or runtime dependency receipt refreshed. Earlier runtime
receipts remain stale after relinking. The [handoff](../progress.md#current-a0-work)
records the next native LOD contract step and unchanged real-capture/provisioning
prerequisites. This partial binding never completes a real reconstruction gate.
