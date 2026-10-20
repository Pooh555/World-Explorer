# Sequence captured-color baking — 2026-10-20 (Asia/Hong_Kong)

M3 remains **IN_PROGRESS**. Explicit sequence baking now retains all 120 possible
source candidates, persists them in native bundle schema 4 and displays the full
63-view real result. Legacy schema-3 bytes remain unchanged. Captured lighting
stays unlit; geometry is fragmented, scale arbitrary and walking unavailable.
No Q/P/N/E or asset-track gate is promoted. [Baking](../protocols/mesh_color.md#bounded-native-baked-appearance)
and [schema 4](../protocols/scene.md#sequence-baked-appearance-bundle-schema-4)
own numerical, unknown, compatibility and allocation contracts.

Repository root is default cwd. Parent `02b733343691d99c0a910569e53327923e86013f`;
ignored evidence `R=artifacts/validation/m3-texture-sequence-20261020`.
`source-identity.json` binds the real producer; production files remain unchanged
through verification. `final-source-identity.json` binds final sources/binaries.
Exact argv/cwd/exits/timings are in `real-commands.json`, `native-commands.json`,
`checks-repaired.json` and `final-checks.json`, with named logs and drivers
`real.py`, `native.py`, `checks_repaired.py`, `oracle.py`, `cancel.py`, `final.py`.

| Identity | SHA-256 |
|---|---|
| Original sequence request | `825c893f46a4db2246690d6010fa96fca98ed5e0ab3107b4f56364e4684edfd5` |
| Original native cache union | `a0b311978cfa7e934e3d029db61ca8e4e9dc37198b12f000d0729faae0d52599` |
| Original observed asset | `9225ef7e33be874f3f40538323254e1e4d505327b54a73ebd41b0c4944ea924e` |
| `sequence.wexplore/manifest.json` | `bc49f7d9f5adba0941d1cae8aaffb37fe6fbeae02947ed36e72d17b08fddedf6` |
| Sequence chunk, 130,729,221 bytes | `3ee825086056d955dfa4382201ceaa41077805719d9e8a25d702fddd9de461b9` |

## Checks and real result

GCC 16.2.1 builds retain C++26 dev / explicit C++23 compatibility; format/tidy
23.1.1 and Black 26.5.1 remain pinned. Existing CPython 3.12.15 / NumPy 2.2.6 /
Pillow 11.3.0 provide the independent oracle; no environment is installed or
requalified. No CUDA, pose/fusion/refit, video decoding, download or upload runs.
Calibrated cached RGB is decoded locally by FFmpeg.
Final checks exit 0 except deliberate cancellation (130) and refusals (2).

| Command/check | Observed result |
|---|---|
| `cmake --build --preset dev -j 4` / `compat-cxx23` | Native builds pass |
| `ASAN_OPTIONS=detect_leaks=0 cmake --build --preset asan --target we_core_tests we_process -j 4` | Instrumented build; actual runs enable leaks |
| `build/dev/we_core_tests '[sequence_texture],[mesh_texture],[color_projection],[bundle_admission],[bundle]'` | **48 cases / 17,782 assertions** pass; six new cases cover 19/63/120 views, global blends/lower-wide-ID ties, word-3 owners, unknowns/padding, malformed rehashed persistence, cancellation, mixed schemas and aggregate budgets |
| `ctest --preset dev` | **399/399**, 131.22 s after fixture repair |
| `ctest --preset compat-cxx23` | **375/375**, 129.75 s |
| Same focused selection under native `ASAN_OPTIONS=detect_leaks=1 build/asan/we_core_tests` | **48 cases / 17,782 assertions**, no reported ASan/UBSan/LSan errors |
| `python3 tools/check_cpp_style.py` with all 11 changed C++ paths; required full tracked-Python per-file Black command | Pass, `checks-repaired.json`; no production Python changes |

`we_process bake-mesh-colors REQUEST SHA NEW_BUNDLE` completes the **39,683-vertex /
49,833-triangle / 63-view** bake in **109.705 s**; `inspect-baked` and `validate`
exit 0. Native leak-enabled full bake takes **346.734 s**, no reported
ASan/UBSan/LSan errors; every saved bundle file is byte-identical to the normal
producer (`sanitizer-parity.json`). Concurrent checks/builds make these single
runs unsuitable for performance qualification. Existing-output rejection preserves
all bytes. A full legacy temporal rebake takes **29.269 s** and reproduces its
original schema-3 bundle exactly. Current sequence GLB export exits 2 before
output creation (`glb-rejection.json`); wider portable interchange remains separate.

The independent NumPy/Pillow oracle checks every **1,793,988 surface lattice
sample / 113,021,244 view attempts**. All candidate words and owners match;
maximum linear RGBA difference is **9.313e-10**. Original geometry/source receipt
bytes and all **1,402,940 padding texels** remain exact/zero. Across **490,680**
shared-edge lattice pairs, validity/candidates match; **484,454** both-known
pairs have maximum RGB difference **0**. The atlas has **3,196,928 texels**:
**1,760,212** supported surface samples (98.1173%) and **33,776 unknown**.
This fraction concerns the existing partial mesh and a 36-point lattice, not
room area, continuous coverage or the earlier six-point audit denominator.

Actual RTX 4060 Laptop viewing displays every original vertex/triangle:
`we_viewer --scene BUNDLE --frames 90 ...` submits **793 frames**, zero Vulkan
errors, 209,473,872 tracked allocated bytes including 92,737,536 framebuffer
bytes. The mixed synthetic diagnostic / full-real `we_gpu_mesh` run submits
**71 frames**, zero errors and 145,151,856 allocated bytes; RGB/UV/unknown,
staging, replacement/invalidation, depth/clip/rebase checks pass. These are local
development observations, not reference-device P4/FPS certification. The native
PPM/lossless PNG was inspected: captured color, conspicuous missing surfaces,
explicit unmeasured scale/unknowns and walking disabled.

`cancellation.json` observes an active FFmpeg child: SIGINT returns **130** in
**0.064 s** (6.264 s total), reaps the child and publishes no output. Preservation
receipts check **152 input files**, original bakes, cancelled output absence and
the raw scene's original manifest pin. The common check covers 24,457 protected
files, four earlier bakes and 33 prior audit/bundle files; known 3,982 external
header differences remain. Current dependency runtime receipts need refresh
after native rebuilds; prior source/rights gaps remain open.

## Corrections and remaining work

Initial tidy finds an unchecked optional access in the synthetic test helper;
an explicit mesh check fixes it. The first complete CPU suite passes 397/399:
two old rejection fixtures still use schema 4 as unsupported. Schema 4 is now
implemented, so they test future version 5; a new check rejects a legacy-only
scene relabeled as schema 4. The unsupported-version error expectation is retained,
and no warning, numerical gate or workload is weakened. Failed logs remain in R.
Final documentation and whitespace checks are recorded in `final-checks.json`.

Sequence portable export/import, owned cleanup/bake/export jobs, measured
alignment/collision/walking and geometry/appearance/corpus qualification remain
open. A0–A6 remain NOT_STARTED and M4 has not begun. [Progress](../progress.md#next-executable-step-and-protection-rules)
owns the next executable task and output protection.
