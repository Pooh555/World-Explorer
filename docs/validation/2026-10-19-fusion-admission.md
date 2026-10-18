# Native fused-geometry admission — 2026-10-19 (Asia/Hong_Kong)

M3 remains **IN_PROGRESS**. Bounded native admission of both retained partial
TSDF/mesh outputs is **VERIFIED on the Linux CPU development profile**. The
[interchange contract](../protocols/fusion.md#native-partial-geometry-admission)
owns exact bounds and semantics. No scene/renderer/collision publication,
measured scale, Q5/N2, complete mesh export or asset gate is certified.

Repository root is the default cwd. Parent `e4d9f67`; final nine-file source-map
SHA-256 `e64e0f949f058a7ddf8986ac6fdf3920ffbfb47a7f4938301e047004a00c0d00`.
Ignored evidence root `R=artifacts/validation/m3-fusion-admission-20261019`
contains `source-identity.json`, frozen `source/`, command arrays, logs and outputs.
Host: Linux 7.2.9-arch1-1 x86_64, GCC **16.2.1**, C++26 development/viewer build;
explicit C++23 CPU compatibility profile; isolated CPython **3.12.15** / existing
NumPy **2.2.6**. Pinned clang-format/clang-tidy **23.1.1**. No GPU execution,
package installation, backend/environment change or download occurred.

## Implemented boundary and real inputs

The converter checks pinned terminal manifest/request/recipe/receipt/NPZ bytes,
bounds ZIP/NPY expansion and publishes an exclusive immutable `.wefusion`.
The native reader independently checks its external asset/document/pose pins,
epoch/revision, wire/JSON limits, all voxel evidence, ordered full-grid coverage,
finite positions/normals and valid indices. Application admission checks current
canonical observations, tracking/invalidation and composed world camera poses.
`inspect-fusion` returns checked JSON with explicit arbitrary units and walking
false. Original compressed hashes identify provenance; final asset pins and the
independent comparison below establish converted payload identity, without native
NPZ parsing or a second fusion implementation.

Inputs are the [retained final fusion runs](2026-10-18-observed-fusion.md), with
all views/filtering unchanged. `R/conversion-commands.json` contains exact
successful final converter argv/exit records. `R/commands.json` owns the working
C++26 `we_process inspect-fusion` commands; `R/replay-results.json` records the
four C++26/C++23 successful inspections and negative conversion commands.

| Converted output | Temporal | Parallax |
|---|---:|---:|
| Encoded bytes | 8,365,832 | 13,797,036 |
| Observed voxels | 159,135 | 256,592 |
| Vertices / triangles | 32,074 / 47,559 | 61,598 / 82,248 |
| Allocated but unknown voxels | 6,248,033 | 11,154,352 |

Asset SHA-256:

- Temporal: `6b39cb5ee4b8a80a70179e53947ec22d1bf741fab96ca5e1bedf5656108f6836`.
- Parallax: `c247347493ad28818c9468638553c49c2978c6f15731aaccd54e80c870590385`.

Both final conversions reproduce the initial converted bytes exactly. Each
C++23 report matches its C++26 report byte for byte. Complete per-view coverage,
extent/512 resolution, weights/support and optional sigma remain explicit;
allocated space and positive local front bands never establish full free space.
The visibly fragmented raw meshes retain their original quality limitations.

## Commands and observed checks

All following commands exited **0**, unless an expected rejection is specified.
`R/checks.json` records final stable build/test argv, cwd, exits and source identity.

| Actual command / command definition | Result / raw evidence |
|---|---|
| `cmake --preset dev`; `cmake --build --preset dev -j 4`; `ctest --preset dev` | **321/321**, 104.49 s; `configure.log`, `dev-stable-{build,ctest}.log` |
| `cmake --preset compat-cxx23 -DWE_BUILD_VIEWER=OFF`; matching build / `ctest --preset compat-cxx23` | **298/298**, 100.13 s; `compat-configure.log`, `compat-cxx23-stable-{build,ctest}.log` |
| `build/dev/we_core_tests '[fusion]'`; same C++23 executable | **9 cases / 155 assertions** each; `unit-verified.log`, `compat-unit-verified.log`; synthetic malformed/rehashed wire/JSON/coverage/evidence/index inputs, exact limits, cancellation, stale/lost/altered pose context and caller-file preservation |
| Native `cmake --build --preset asan -j 4`; `ASAN_OPTIONS=detect_leaks=1 build/asan/we_core_tests '[fusion]'` | **9 / 155**, no reported ASan/UBSan/LSan errors; `asan-{build,tests}-verified.log` |
| `R/checks.json`: isolated converter unittest command | **5/5**; integer widths, exact scalar bytes, ZIP inventory, huge NPY declarations, type/shape/order/length and object-array rejection; `converter-tests-stable.log` |
| `python3 R/replay.py` | Four real inspections pass; wrong pin/existing destination reject with exit **2** and preserve outputs; `replay-final.log` |
| Isolated fusion Python `R/audit.py` | Eight voxel planes and three mesh arrays match every original value; six independently rehashed real NaN/index mutations reject with exit **2**; **24,457 protected files unchanged**, `audit.json`, `audit-final.log` |
| Pinned `python3 tools/check_cpp_style.py` on six changed C++ files; `python3 tools/check_docs.py`; `git diff --check` | Pass; `style-handoff.log`, `docs.log`, `diff-check.log`; first-party boundaries and warnings-as-errors retained |

Initial compilation caught missing includes/explicit conversions; corrected before
successful builds. Real NPZ triangles are int32, correcting the earlier contract's
int64-only description; conversion now accepts either checked signed width.
The generic native reader's existing 256 MiB cap becomes an explicit native
interchange limit, without coarsening/dropping inputs. Sandbox LeakSanitizer failed
under ptrace during test discovery; native verification passed. Initial style
failures were corrected through typed wire-width helpers and ordinary boolean
assertions, retaining warning policy. All failed logs remain in `R`.

Native scene transactions/renderer display of these partial meshes are next.
Supported-component cleanup, bounded simplification, production ownership/resume,
measured geometry and walking remain open; no unchanged solve/refit was run.
