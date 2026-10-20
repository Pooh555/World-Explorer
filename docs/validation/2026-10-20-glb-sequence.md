# Sequence captured-unlit GLB — 2026-10-20 (Asia/Hong_Kong)

M3 remains **IN_PROGRESS**. The restricted GLB producer/importer now preserves
sequence ownership and complete source metadata. No measured geometry, walking,
Q/P/N/E, PBR or asset gate is promoted. Default cwd is the repository root;
raw evidence is `artifacts/validation/m3-glb-sequence-20261020/` (`R`).

## Implementation and identities

[GLB V2](../protocols/gltf.md) explicitly tags each ownership layout: uint64 owner
plus four uint32 candidate words for sequences, or one word for legacy chunks in
mixed scenes. All 120 candidate bits and uint64 identities survive. Legacy-only
exports remain V1. The 256-MiB encoded, 16-MiB JSON, 8M aggregate texel and existing
decoded budgets remain unchanged. Sequence portable metadata uses schema 4 /
`CAPTURED_UNLIT_ATLAS_SEQUENCE_PNG_V1`, hash prefix 6; original sequence metadata
schema 3 / prefix 5 and native bundle schema-4 layout stay unchanged. Import also
rejects rehashed original blend/portable-parent substitutions before derivation.

Parent `3520cf0b3d1558d272cc543182189efcb2af4807`.
`source-identity.json` pins the initial eight C++ files and seven binaries; final
identities include the test-only tidy repair. Production sources/binaries remain
identical to those used for real/native/sanitizer product validation.
CLI SHA-256 `5dbff49ae800c333a64401284265d150bf9f6a669fefb73f007c5f9b2051262c`.
`final-source-identity.json` and `script-identities.json` pin documentation and
verification helpers. Profiles: GCC
16.2.1 / C++26 development, explicit C++23 compatibility and ASan/UBSan. Formatting
and tidy use pinned 23.1.1. Linux 7.2.9-arch1-1, native X11/XWayland, RTX 4060
Laptop; viewer device/log evidence is retained. This is local development evidence.

Input is the [previous full 63-view bake](2026-10-20-texture-sequence.md),
`artifacts/validation/m3-texture-sequence-20261020/sequence.wexplore`, manifest
SHA `bc49f7d9f5adba0941d1cae8aaffb37fe6fbeae02947ed36e72d17b08fddedf6`.
Output `R/sequence-export/scene.glb` is **98,164,236 bytes**, SHA
`9becbc3ad3710d1be61a8955db4541d62af1c4f69eddd0ba5bf3804d7811fea2`.
Imported `R/sequence-import.wexplore/manifest.json` SHA
`c081c55b30ab1683429b7fec466d61dd06186419d01a822741f426613f1537a1`.
`real.json` pins command arrays, times, input/output files and exact legacy parity.

## Commands and observed checks

All successful commands exit 0. Full CTest runs preceded the test-only repair.
`checks.py`, `real.py`, `native.py`,
`independent.py`, `adverse.py` and `guard_checks.py` retain dated command
definitions/receipts in R.

| Command / receipt | Observed result |
|---|---|
| `cmake --build --preset dev -j 4`; corresponding `compat-cxx23`; ASan `we_core_tests we_process` targets | All builds pass; `checks.json` / build logs |
| `ctest --preset dev`; `ctest --preset compat-cxx23` | **403/403**, **379/379**; `checks.json` and CTest logs |
| Compatibility and native ASan `we_core_tests '[gltf],[sequence_texture],[mesh_texture],[color_projection],[bundle_admission],[bundle]'` | **58 cases / 109,177 assertions** each; test-helper repair rechecked on all three profiles; leak detection enabled for execution |
| Shared CLI `export-glb` / `import-glb`, `real.py` | Full sequence plus four legacy raw/cleaned scenes pass; every legacy GLB, export manifest and imported bundle file is byte-identical to saved 19 October products |
| Native ASan/UBSan full sequence export/import, `native.py` | **69.423 / 78.028 s**, no reported sanitizer error; GLB and every imported bundle byte match development output (`sanitizer-parity.json`) |
| Independent NumPy/Pillow `oracle.py` | Full geometry/documents, uint64 frames/owners, all candidate words, alpha, UVs and nearest-sRGB8 codes exact; five scenes (`oracle.json`) |
| Retained Khronos validator 2.0.0-dev.3.10 | All five files: **0 errors / 0 warnings**, three informational unused authoring views each (`validator.log`) |
| Rehashed real reserved mask, false original blend and legacy-layout substitutions | Each import exits **2** with no destination (`adverse.json`) |
| Native viewer / `we_gpu_mesh` on imported sequence | **1,082 / 71 frames**, full **39,683 vertices / 49,833 triangles**, zero Vulkan errors; screenshot inspected at **1428×1746** |
| `python3 tools/check_cpp_style.py` with all eight changed C++ paths; docs checker; `git diff --check` | Pass; `style-confirmation.json` / `final-checks-verified.json` |
| Existing protected-input check plus saved bake identities | 24,457 protected files, 152 sequence inputs and all original bake bytes preserved; final preservation receipts |

Units cover 19/63/120-view and all-unknown round trips, wide-ID owner ties, mixed
layouts, reserved/out-of-range bits, absent owners, malformed frame counts,
metadata/layout/budget mutations, cancellation, publication refusal and lossy
re-export refusal. The initial focused run passed 16 cases / 93,607 assertions.
Initial tidy failed on seven test-fixture diagnostics (swappable numeric
parameters and optional access). A typed chunk ID repairs the parameter issue;
`value()` still produces
six access diagnostics. A shared helper with an explicit guard repairs those;
the repeated gate passes without warning suppression. The original
style/final-wait failures remain retained (`tooling-failures.json`). One full
tidy run then reported unterminated comments in two unchanged system headers;
inspected bytes contain complete comments (`system-header-inspection.json`).
The identical gate rerun passes; no header/toolchain/warning changes. The full
tracked-Python Black check also passes (`black.json`); no production Python changed.
Initial screenshot conversion with system Python
failed because Pillow was absent; the retained fusion environment converted it
successfully, without installation or environment changes.

## Fidelity and remaining work

All **3,196,928** texels retain exact validity/ownership. Maximum linear sRGB8
quantization error is **0.004448414**; imported RGB matches independent PNG decode
exactly. Across **2,441,817** equal-area microtriangle interiors, native-known
support falls from **2,375,809** to **2,030,682** under conservative bilinear
masking, with **zero** newly supported interiors. Maximum unquantized filter and
combined color differences are **0.120434056 / 0.121031652**. Partial-mesh areas
and denominators remain in `oracle.json`; these are sampled comparisons, not
continuous guarantees or room coverage. The screenshot shows fragmented captured
surfaces and large gaps, with unlit/portable-loss labels and walking disabled.

No new fusion, CUDA solve, bake, model download, environment repair, upload or
platform qualification occurred. Runtime inventory receipts still need recapture;
253 source/rights gaps and 3,982 pre-existing external-header changes remain.
Owned cleanup/bake/export job integration, measured scale/collision/walking and
quality remain open; A0–A6/AP remain NOT_STARTED. The precise next task is in
[progress](../progress.md#next-executable-step-and-protection-rules).
