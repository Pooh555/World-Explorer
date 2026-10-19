# Native captured-color mesh atlases — 2026-10-19

Scope: M3 bounded native captured-color fallback **VERIFIED locally** on Linux /
NVIDIA; M3 remains **IN_PROGRESS**. Portable textured interchange, production
jobs/resume, metric alignment, collision and Q/P/N/E gates remain open. No A-track
implementation or PBR/relighting qualification. Default cwd is the repository root;
raw evidence is `artifacts/validation/m3-texture-20261019/` below.

## Implementation and identity

Parent: `877d96ebd44b8c63554980e58299bddff35e18ff`. `source-identity.json`
records final dirty-source/file and development binary SHA-256 identities;
`runtime-source-before-tidy.json` retains the earlier source receipt. Final
source receipt SHA-256: `90b5a28e23bec19d67c5d53cc9e0417efa471a5ef7779b0a5d122a8b173bccd2`. Individual
bake command receipts retain the producer binary actually used. Header overlays
and their worktree/guarded hashes are in `guarded-header-identities.json`.
GCC **16.2.1 / C++26** native development; Clang **23.1.3 / C++23**, isolated
libc++/libc++abi **23.1.1** CPU; GCC native ASan/UBSan/LeakSanitizer. Formatter/tidy:
**23.1.1**. GPU: **RTX 4060 Laptop**. No Windows/macOS native pass is inferred.

The [implemented contract](../protocols/mesh_color.md#bounded-native-baked-appearance)
uses 36 independently projected lattice points per original triangle, continuous
source vertex normals and convex score-squared linear RGB blending. UV/RGBA,
validity/owner/candidate planes and exact authoring pins are admitted and persisted
under [schema 3](../protocols/scene.md#baked-appearance-bundle-schema-3). The native
shader filters valid lattice triangles, encodes display sRGB and discards unknowns.
Original geometry/support/certificates remain unchanged. There is no exposure
fit, inferred color, hole filling, simplification, scale recovery or walking.

## Commands and observed checks

All listed completed commands exit **0**, except the explicitly retained failures.
Script references identify the exact local command arrays and output identities.

| Command / receipt | Observed result / raw log |
|---|---|
| `cmake --build --preset dev -j 4`; changed-target rebuilds in `build-final*.log` | Native viewer, CLI, tests and GPU diagnostics build; `build-dev-all.log` |
| `ctest --preset dev` | **365/365**, **205.81 s**, `ctest-dev.log` |
| `build/{dev,ci-libcxx,asan}/we_core_tests '[mesh_texture],[color_projection],[bundle_admission],[mesh]'` | Each **44 cases / 15,493 assertions**, final focused logs `tests-{dev,libcxx,asan}-final.log`; native ASan run uses `ASAN_OPTIONS=detect_leaks=1`, no reported sanitizer/leak errors |
| `cmake --build build/ci-libcxx --target we_process we_core_tests -j 2`; ASan `we_core_tests` equivalent | C++23/libc++ and C++26 sanitizer builds pass; `build-libcxx-restored.log`, `build-asan.log`, subsequent final quotient rebuild logs |
| `python3 tools/check_cpp_style.py` with paths from `our-cpp.txt` | **23 files** pass with guarded-header VFS overlay; final subpixel GPU-file and viewer-file gates also pass. `style-final.log`, `style-subpixel-final.log`, `style-viewer-final.log`; warnings policy unchanged |
| `python3 tools/check_docs.py`; `git diff --check` | Pass, `docs.log`; source/module boundaries also pass through the style gate |
| `real_bakes.py`, `verified_bakes.py`, `rebake-equivalence.json` | All four full scenes baked twice; every output file is byte-identical across the recorded producers. `current-bake.json` additionally proves the final CPU producer preserves every parallax proxy bundle byte. `verified-bakes.json` pins requests, commands, metadata and all files |
| `audit_regression.py` | Refactored admission reproduces prior temporal raw `report.json`, `samples.bin` and `atlas.ppm` byte for byte; `audit-regression.json` |
| `artifacts/environments/fusion-cpu/bin/python .../oracle.py` | Independent NumPy/Pillow decoding/projection checks all four complete atlases; `oracle.json`, `oracle.log`, `oracle-final-proxy.log` |
| `build/dev/we_gpu_mesh .../gpu-subpixel-final` | **62 frames**, zero Vulkan errors; RGB, UV orientation, subpixel triangles, unknown discard, replacement/retirement, staged upload/invalidation plus original mesh depth/clip/rebase/resize checks |
| `build/dev/we_gpu_mesh .../gpu-real-temporal-bounded .../verified-temporal-cleaned.wexplore` | **65 frames**, all **31,456 vertices / 46,323 triangles**, zero Vulkan errors, native real capture |
| `native_views.py temporal-cleaned parallax-cleaned`; `native_views_final.py` equivalent | Full native viewer / X11 captures **1428×868**, **104/220 frames**; final viewer **1428×1746**, **135/207 frames**. Both retain original **46,323/80,912 triangles**, zero Vulkan errors; `native-views{,-final}.json`, both `*-final-viewer.log` and lossless `*-final-native.png` captures inspected |
| Native viewer, temporal proxy, `--frames 12 --residency-mib 128` | Expected exit **2**, explicit atomic old/new/staging residency rejection, no indefinite pending ticket; `native-budget-reject.log` |

Unit checks include linear-light blending, exact adjacent-edge samples, view
order/context/cancellation, aggregate budgets, schema compatibility, rehashed
malformed dimensions/counts/UV/NaN/alpha/masks/metadata, and failed-publication
preservation of prior readers/manifests. Native runs require device/display access;
these are development checks, not reference performance measurements.

## Full-source atlas results

The oracle independently decodes original/schema-3 geometry, source RGB and depth,
reprojects interpolated source normals and reconstructs all weights/planes.

| Source | Triangles | Known / 36-point surface lattice | Known fraction |
|---|---:|---:|---:|
| Temporal raw | 47,559 | 1,678,597 / 1,712,124 | 98.0418% |
| Temporal proxy | 46,323 | 1,634,519 / 1,667,628 | 98.0146% |
| Parallax raw | 82,248 | 2,827,113 / 2,960,928 | 95.4806% |
| Parallax proxy | 80,912 | 2,779,504 / 2,912,832 | 95.4227% |

Across **9,253,512 surface samples / 136,145,448 view attempts**, candidate masks,
owners and float32 RGBA match exactly; padding is zero. Original geometry blocks
and exact source/certificate strings match the schema-2 parents byte for byte.
All **2,648,784 shared-edge lattice pairs** have matching validity/candidate masks;
the **2,576,052 both-known pairs** have maximum RGB difference **0**. This proves
sampled shared-edge consistency, not continuous fidelity or visibility correctness.
The prior six-interior-point audit has a different denominator. These fractions
measure the **partial source mesh**, not room or rendered-fragment coverage. Both
native views remain visibly fragmented, with large unknown gaps and arbitrary
scale; captured illumination remains baked appearance.

## Retained failures, repair and preservation

The first GPU smoke used large overlapping synthetic triangles. Full real scenes
subsequently timed out on Wayland and X11; an empty and raw-mesh control passed.
A smaller full-scene harness reproduced the GPU failure. The shader derived tile
origin from interpolated UV and lacked atlas bounds. Flat per-triangle tile origin
and explicit read bounds repair the full-scene failure; helper invocations beyond
small triangles are a plausible mechanism, not a traced GPU fault address. The
permanent subpixel case, complete harness and both full viewer captures pass after
repair. `native-temporal*.log`, `gpu-real-temporal.log` and intermediate logs retain
failures, including timeline timeout/device-loss teardown validation. The viewer
also now reports failed renderer uploads instead of retaining a pending ticket.
The earlier pilot and failing attempts are not final passes.

The ordinary style run rejects existing user `#pragma once` edits. **81 unrelated
header edits remain byte-identical**, with overlapping header edits also preserved.
Validation uses guarded header copies through clang-tidy's VFS overlay, matching
the implementation commit's include guards; no lint rule is disabled. The full
working tree's pragma portability conflict remains unresolved.

The original **24,457-file** preservation assertion correctly fails: **3,982
external `.h`/`.hpp` package files** differ from retained receipts. All **20,475
other protected files** match, including private reconstruction data. No worker
installation is repaired or overwritten. The existing libc++ headers likewise
fail the feature probe with unterminated guards; `libcxx_rebuild.py` extracts the
retained package archives into a separate evidence directory, records their
SHA-256 in `libcxx-packages.json`, then successfully configures/builds/tests that
isolated profile. No download. Worker receipt/header reconciliation remains due
before future CUDA rebuilds. `retained-inputs.json` records exact drift;
`prior-output-preservation.json` checks **33** prior audit/bundle payloads, and
`cleanup-preservation.json` retains all no-op/final cleanup and frozen-source pins.
The first output observer accidentally traversed a retained linked-cache rejection
fixture; scoping it to the four actual audit/bundle outputs fixes that observer.

Next: portable explicitly unlit textured export/import with declared filtering,
validity and quantization semantics, full geometry/pin preservation and actual
local import/viewing. Production jobs, measured geometry and collision stay open.
