# Independent material-channel provenance — 27 October 2026

**A0/AP0 remain IN_PROGRESS.** Asset-definition schema 2 now binds each original
material channel and region sidecar, admitting heterogeneous masks without a false
material-root summary. Native adoption, owned metadata reload, portable sidecars
and native PBR source pins retain these bindings. Schema 1 remains supported.
This synthetic contract/runtime slice qualifies no producer, measured support,
material recovery, real A1 output, Godot behavior or AP gate. M4 has not begun.

Cwd: repository root; timezone Asia/Hong_Kong. Work began 26 October and finished
27 October; initially clean parent `a6efef1bcebcb1b3f0e0fb675b2c62a55058717b`.
`R=artifacts/validation/a0-channel-provenance-20261026` (ignored).
`source-identity.json` pins all ten changed C++ files. The compact sorted
path/hash-map SHA-256 is
`5bbc0b65b0e9970a6cba31d79d0aee3b925cb7ea631257378f1e903779fbc1c6`.
The [protocol](../protocols/asset_channel_provenance.md) owns field/budget/failure
semantics. No dependency, standard, warning or sanitizer setting is weakened.

## Integration and actual checks

Metadata requires complete role sets, graph-bound direct channel/sidecar
dependencies and independent per-target selection. Native adoption matches the
actual admitted material products. Per-channel lighting/manual restrictions stay
in force. Tests cover heterogeneous classification/confidence, canonical ordering,
strict rehashed JSON, stale/foreign/aliased/missing bindings, oversized input,
manual ancestry, captured/generated modes, v1→v2 revision history, interrupted
publication, replay and export→native bundle persistence.

Commands below run from the repository root. Wrapped commands have exact argv,
exit and elapsed time in `$R/TAG.json`, with output in `.log`. Direct native
sanitizer commands retain their output and completion records separately.

| Command | Result | Tag / raw evidence |
|---|---|---|
| `cmake --build --preset dev -j 4` | Pass / all targets, after incremental corrections | `build-dev-all`; earlier `build-first`, `build-fourth`, `build-dev-complete` |
| `cmake --build --preset compat-cxx23 -j 2` | Explicit C++23 CPU/viewer-off build | `build-compat-complete` |
| `build/dev/we_core_tests '[asset-channels],[asset-schema],[asset-metadata-io],[native-assets],[native-export],[pbr-scene]'` | Pass / **52 cases, 29,486 assertions** | `focused-complete` |
| `WE_CHANNEL_SCENE_PROBE_OUTPUT="$PWD/$R/synthetic-complete.wexplore" WE_CHANNEL_EXPORT_PROBE_OUTPUT="$PWD/$R/export-complete" build/dev/we_core_tests 'channel provenance survives exact portable export and native PBR source bindings'` | Pass / actual admitted product→metadata→export→PBR bundle | `product-complete` |
| `python3 "$R/product_oracle_complete.py"` | Pass / exact inventory, bindings, mask partitions and native source pins | `product-oracle-final` |
| `build/dev/we_gpu_pbr "$R/gpu-final" "$R/synthetic-complete.wexplore"` | Pass / 101 frames, initial/replacement/invalidation/deletion; zero Vulkan errors | `gpu-final` |
| `build/dev/we_viewer --scene "$R/synthetic.wexplore" --settings "$R/viewer-settings.json" --frames 120 --screenshot "$R/viewer.ppm"` | Pass / ordinary visible PBR load, both instances, synthetic labels, zero Vulkan errors | `viewer-product` |

CPU builds use GCC **16.2.1** with C++26 development and explicit C++23 profiles.
`ctest --preset dev -j 2` passes **853/853** (`ctest-dev-final`);
`ctest --preset compat-cxx23 -j 2` passes **828/828** (`ctest-compat`). After the full native rebuild, `build/dev/we_viewer_tests` also
passes **25 cases /645 assertions** (`viewer-tests-final`).
The identical focused filter passes **52 cases /29,486 assertions** natively under
ASan/UBSan with default LeakSanitizer (`build-asan-complete`, `asan-focused`), with
no reported errors. `sanitizer-options.json` records unset options. Exact LLVM
**23.1.1** formatting/tidy repository checks pass all ten changed C++ files
(`style-complete`). Clang 23.1.4 with retained libc++ 23.1.1 headers passes C++23
syntax checks for definition admission, decoder, native adoption and the new test
(`libcxx-{definition,io,adoption,test}-final`). These are syntax-only checks,
not a full libc++/Jolt build. No native Windows/macOS/GCC 13 pass is inferred.

The actual device is **NVIDIA GeForce RTX 4060 Laptop GPU**, with Vulkan validation
enabled. Six calibrated views of two instances pass the independent ray/BRDF/image
oracle: **399 sampled pixels, maximum error 1 RGB8 level against preset tolerance
2** (`python3 "$R/pixel_oracle.py" "$R/gpu-final"`, `pixel-final`). Invalidation
leaves a uniform background. This is renderer correctness, without a performance
or relighting-quality claim. The visible viewer uses an identical earlier bundle;
final product hashes and readbacks are in `artifact-hashes.json`.

The independent Python oracle checks all **22 package files**, canonical v2
catalog identity, exact channel→sidecar mappings, original partitions/confidence
and the existing native binary tail's definition/manifest pins. The normal channel
retains one OBSERVED and three INFERRED texels with confidences 0.25/0.75; other
channels retain four INFERRED texels. Six channel sidecars are shared across two
materials, alongside the geometry sidecar. Runtime bundles pin the exact source
manifest; they do **not** embed mask bytes or add provenance-inspection UI.

Definition SHA: `d9b92631ecdde773be1a6c34b4e46e06f1027d4f6b8cdad0f6752dab439990ba`;
source manifest: `77f888fdc8b5bb6d75d6d8d28620deb5834a40437ff2918e805ff79f75be4ed2`;
bundle manifest: `b1da6a47ea382a6bed9a8a1af4d97f585ad9ba259c8427f190a1ed23bc0c3a1e`.
Final source regenerates identical package/bundle bytes to the first product probe.
The old native-export probe also regenerates **all 24 pre-v2 package files
byte-for-byte**, including the v1 catalog, manifest and glTF resources
(`legacy-export-probe`, `legacy-byte-identity.json`). Original artifacts remain intact. `preservation-final` rehashes **378 protected
files with zero mismatches**; no saved real product is replaced.

## Corrections and limits

Retained `build-second`/`build-third` logs reject a missing explicit blob byte limit
and an unqualified test tree helper. Explicit bounds/import fix them.
`style-first` catches five unchecked optional accesses in test code; a checked
helper repairs them. Review replaces a premature copy of untrusted channel strings
with bounded borrowed references; `style-reviewed` then requires reserving that
small vector. Final ownership copies occur only after receipt binding.

Automatic approval review times out before one sanitizer rebuild executes. The
permitted direct-CMake retry succeeds; the timeout is not a sanitizer failure.
Default ASan/UBSan/LeakSanitizer options remain unset. No suppressions are added.

`docs-final` (`python3 tools/check_docs.py`) passes 1,634 local targets in 309
files; `diff-final` (`git diff --check`) passes.

This runs no model installation, inference, real video processing, Godot import or
runtime dependency receipt refresh. Earlier runtime receipts remain stale after
relinking. Source correspondence and physical truth remain unqualified; declarations
cannot establish consent, real human actions, support or automatic yield. The
[handoff](../progress.md#current-a0-work) owns the next structure-graph adoption
step and unchanged provisioning/real-capture prerequisites.
