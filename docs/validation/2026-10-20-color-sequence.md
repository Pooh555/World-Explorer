# Sequence captured-color audit — 2026-10-20 (Asia/Hong_Kong)

M3 remains **IN_PROGRESS**. Native color input admission now checks a complete
cache union and streams its views into one global projection audit. Explicit
schema 2 and `WECLR002` preserve candidates through view 119; legacy schema 1
keeps its 18-view/48M-pixel bounds and original bytes. The unchanged baker
explicitly rejects sequence requests. No geometry, texture, Q/P/N/E or asset
gate is promoted. [The contract](../protocols/mesh_color.md) owns admission,
wire layout and the unchanged projection/score/unknown policy.

Repository root is default cwd. Parent `8bdb3e02f230960d2cad23140e1df7874b8aa859`;
ignored evidence `R=artifacts/validation/m3-color-sequence-20261020`.
`run/source-identity.json` records the working tree during real integration; `final-source-identity.json`
binds final files/binaries. `checks.py`, `real_repaired.py`, `native.py`, `cancel.py`,
`oracle.py` and their JSON command records/logs retain actual arguments, exits
and durations. Final brace-spacing changes are formatting only, followed by
native rebuilds and focused checks in `final-checks.json`.

| Pinned input/output | SHA-256 |
|---|---|
| `run/scene-request.json` | `e78058294b4d04ea79e39d2602416669e7265d73819e733eb78cae8c2c5313b4` |
| `run/sequence-request.json` | `825c893f46a4db2246690d6010fa96fca98ed5e0ab3107b4f56364e4684edfd5` |
| Original fresh cache union | `a0b311978cfa7e934e3d029db61ca8e4e9dc37198b12f000d0729faae0d52599` |
| Original native observed asset | `9225ef7e33be874f3f40538323254e1e4d505327b54a73ebd41b0c4944ea924e` |
| `run/sequence-audit/manifest.json` | `5f1d3e8c4f477e58fe8c533a77fe32939f02abc73911b5e9be4fc16b2279c051` |
| `run/sequence-audit/samples.bin` | `fe55238a4b6b2a831c2844a7ffa5be3c44dc51924ebaa9651146ebb91af9c3cb` |

## Checks and real integration

Toolchains remain GCC 16.2.1, C++26 dev and explicitly selected C++23 compatibility;
format/tidy 23.1.1 and Black 26.5.1. Independent observation uses existing
CPython 3.12.15 / NumPy 2.2.6 / Pillow 11.3.0. No environment is installed or
requalified. This CPU-only slice changes no renderer or solver behavior; no
unchanged GPU/stress, CUDA, pose, fusion, refit, decode-from-video, download or
upload run is performed. Calibrated cached RGB is decoded locally by FFmpeg.

| Actual command/check | Result |
|---|---|
| `cmake --build --preset dev -j 4` / `compat-cxx23` | Native builds pass |
| `ASAN_OPTIONS=detect_leaks=0 cmake --build --preset asan --target we_core_tests we_process -j 4` | Instrumented build; executions enable leaks |
| `build/dev/we_core_tests '[color-sequence],[color_projection],[mesh_texture]'` | **22 cases / 1,027 assertions**; five new cases cover 19/63/120 references, 121 rejection, legacy bounds, rehashed stale/incomplete/type-confused graphs, late corruption, cancellation and all mask-word boundaries/padding |
| `ctest --preset dev` / `ctest --preset compat-cxx23` | **393/393 / 369/369**, 113.19 / 116.50 s |
| Same focused selection, `ASAN_OPTIONS=detect_leaks=1 build/asan/we_core_tests ...` | Same 22 cases / 1,027 assertions; no reported ASan/UBSan/LSan errors |
| `python3 tools/check_cpp_style.py` with the six changed C++ paths | Format/tidy, boundary, docs and whitespace checks pass |
| Required full tracked-Python per-file Black command | Pass; no production Python changes |

New cache-graph tests use synthetic opaque files; they do not qualify depth
decoding. The real checks below consume every native depth/RGB view.

`run/real-commands.json` records `we_process audit-mesh-colors REQUEST SHA OUTPUT`:
the complete **39,683 vertices / 49,833 triangles / 63 views** take **58.053 s**.
A fresh-directory repeat takes **80.100 s** and reproduces every product byte;
this recomputes the audit rather than claiming a resumable/cached job. Concurrent
builds/checks make these single-run timings unsuitable for performance gates.
Native leak-enabled sanitizer audit takes **145.887 s**, with identical products
and no reported errors. Existing-output rejection preserves its bytes; wrong
schema and sequence-bake requests exit 2 without output. A full legacy temporal
audit takes **10.171 s**, reproducing every original diagnostic product exactly.

The independent oracle reads the saved native geometry, original child depth
planes and calibrated RGB. It reproduces **448,497 samples / 28,255,311 view
attempts**, all candidate masks, exclusive rejection ledgers, owners, RGB and
coverage histograms. Owner disagreements and RGB differences are **zero**;
maximum score difference **3.242e-14**. It independently verifies **59,424** known
shared-edge comparisons, unknown zeros, unused wire bits and every contact-sheet
pixel. `run/oracle.json` and `oracle.log` retain the result; the lossless diagnostic
PNG was visually inspected. It is a triangle contact sheet, not a scene texture.

Known interiors are **290,988 / 298,998 (97.321%)**, with **8,010 unknown**;
685 faces have no known interior samples, 1,632 have partial samples. Of 61,335
shared edges, **1,911** comparisons remain unknown. Maximum / mean known seam
RGB L∞ are **0.993771 / 0.006333**; maximum candidate spread is **0.995310**.
These are conditional measurements on fragmented observed geometry, not room
coverage, continuous texture support, appearance-quality improvement or PBR.

`cancellation.json` observes an active owned FFmpeg child: SIGINT returns **130**
in **0.064 s** (3.863 s total), the child is reaped and no output is published.
`run/preservation.json` verifies **152 input files**, legacy/repeated products and
the source asset unchanged; `final-preservation.json` rechecks them after all
verification. The common preservation check retains 24,457 protected files,
four original bakes and 33 prior audit/bundle files; 3,982 known external-header
differences remain. Runtime dependency receipts still need refresh after native
rebuilds; prior source/rights gaps and platform qualification remain open.

## Corrections and remaining work

The initial real audit exits 2 because a new check mistook the dense manifest's
resize preparation hash for the native backend recipe hash. The check was invalid:
the original receipt already pins both recipe files separately. The corrected
admission checks their profiles and immutable inventory pins, retaining all
existing source/pose/coverage checks. `audit.log` and the failed driver remain.
The first focused command used `[mesh-texture]`, which matched no texture cases;
the corrected `[mesh_texture]` selection passes as recorded above. No threshold,
warning or standard is weakened. Final docs/diff checks pass.

The audit does not publish a new scene. Full-sequence atlas baking/persistence,
native textured viewing and export need a wider appearance representation;
measured scale, collision/walking, geometry quality and full gates remain open.
A0–A6 remain NOT_STARTED; M4 has not begun. [Progress](../progress.md#next-executable-step-and-protection-rules)
owns the next executable step and output protection.
