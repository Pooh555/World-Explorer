# Certified local mesh cleanup — 2026-10-19 (Asia/Hong_Kong)

M3 remains **IN_PROGRESS**. Separate immutable simplification products now derive
from both full retained meshes and open through the shared native scene path.
This qualifies the bounded local primitive/certificate workflow only. No Q5/N2,
measured scale, collision, complete export, platform or asset gate is certified.
Repository root is the default cwd; parent `bc77b61`.

Ignored evidence root `R=artifacts/validation/m3-fan-cleanup-20261019` owns frozen
policies/source, exact commands, manifests, certificates, reports, logs and native
readbacks. Final 12-file source-map SHA-256:
`ba676c53948008f46fe0ad75caa19d8351ba16780d7d37d76473efd0fe8857c2`.
The [contract](../protocols/mesh_cleanup.md) owns policy, proof, layout, bounds,
failure semantics and CLI/viewer commands.

## Policy, retained failure and real outputs

The frozen first fan-only primitive accepts **zero** real patches; all **4,447 /
11,033** degree-three candidates fail its topology protection. Both meshes stay
unchanged. `frozen-policy.json`, `fan-v1-source/`, its source map/binary, initial
reports and `generation-commands.json` preserve this result. A separate analytic
short-edge fixture preceded the extended `frozen-policy-v2.json` run. It keeps the
same quarter-voxel error, two-voxel diameter, observation/support and boundary
limits; it adds complete interior-star edge contraction and image-edge validation.
No evaluation grid, MVS filter, solver, fit, environment or geometric gate changed.

`generation-v2-commands.json` records both actual `cleanup-fusion` invocations on
the unchanged [admitted parent assets](2026-10-19-fusion-admission.md).
Each `*-final/manifest.json` pins the certificate, complete before/after report and
native scene request. Certificates retain original pose/coverage/unknown sources;
all retained positions/normals copy exactly. No supported component is removed.

| Full real result | Temporal | Parallax |
|---|---:|---:|
| Accepted short-edge patches | 618 | 668 |
| Vertices, raw → derived | 32,074 → 31,456 | 61,598 → 60,930 |
| Triangles, raw → derived | 47,559 → 46,323 | 82,248 → 80,912 |
| Components, raw = derived | 800 | 1,864 |
| Boundary edges preserved exactly | 15,519 | 38,946 |
| Both whole-surface distance upper bounds | 0.0022906133696050787 | 0.005966383446083243 |
| Allowed bound, voxel_size / 4 | 0.002298594435443553 | 0.005971982304056857 |

Distances remain arbitrary camera/world units, relative to the original partial
mesh, without measured geometric truth. Areas/counts do not compare quality
between the different source extents/view sets. Surfaces remain fragmented.

## Independent certificate and complete-denominator check

Existing isolated Python **3.12.15**, NumPy **2.2.6**, SciPy **1.18.1** runs
`R/oracle.py`. Original NPZ bytes are checksum-verified independently of native
interchange. Exact rational arithmetic verifies **all 1,286 patches'** distance
inequalities, orientation, affine image coverage, nonoverlap, topology/support
conditions and source-to-output remapping. Native geometry SHA-256s reproduce:

- Temporal: `8d198c2d47007b4622afcf9cd7b7cf8e7a071bcb3d77a2490a5cd2bc3001c468`.
- Parallax: `72aaad4c281336a24e4d68ddd039e570a265a7381628daad4d5044bd8f9e7c6f`.

Independent graph/area/stencil computations reproduce every before/after
component report and exact integer denominators, boundary edges and Euler
characteristic. Largest floating difference: **2.31e-13**. These are certified
upper bounds over complete surfaces, not sampled-point distances or Q5 results.
`oracle-complete.log` / `oracle.json` retain complete results, including unchanged
original grid coverage/unknown and pose-dependency metadata.

## Actual verification

Linux **7.2.9-arch1-1**, GCC **16.2.1** C++26 development; Clang **23.1.3** C++23
viewer/libstdc++ and CPU/libc++ **23.1.1**. Formatter/tidy **23.1.1**.
Commands run from repository root; all final commands below exit zero.

- `cmake --build --preset dev -j 4`, `ctest --preset dev`: **341/341 / 111.12 s**.
  `cmake --build build/clang-viewer-cxx23 -j 2` and corresponding CTest:
  **341/341 / 91.56 s**. Same commands for `build/ci-libcxx`:
  **318/318 / 132.55 s**. `summary.json` and profile build/CTest logs retain results.
- `R/final_checks.py` / `final-checks.json`: all three profile executables pass
  **29 cases / 513 assertions** under `[cleanup],[fusion],[fusion-components],[mesh]`.
  Both derived requests reproduce their manifest scene hashes; both raw requests
  reproduce the preceding runtime hashes byte for byte on all three profiles.
- Native `cmake --build --preset asan -j 4`, then
  `ASAN_OPTIONS=detect_leaks=1 build/asan/we_core_tests '[cleanup],[fusion],[fusion-components],[mesh]'`:
  **29 / 513**, no reported ASan/UBSan/LSan errors; `asan-native-{build,tests}.log`.
- Pinned `python3 tools/check_cpp_style.py` on `R/cpp-paths.txt`: all **11 C++
  files** pass format/tidy/boundaries/docs/whitespace (`style-v2.log`). Initial
  naming/exception findings were corrected without suppressions; logs retained.
- `R/native.py` / `native-commands.json`: both full raw and derived requests open
  for **90 frames each**; both derived meshes also pass the existing indexed-mesh
  depth/clip/resize/staging/rebase/replacement/invalidation diagnostics. All runs
  report zero Vulkan errors (**36 GPU diagnostic frames each**). All four visible
  captures were inspected: gaps remain, derived surfaces have a proxy label, and
  walking stays unavailable. Device: **RTX 4060 Laptop / 8,188 MiB / driver
  615.78.08**. No FPS/peak-memory/reference-hardware claim.
- `R/preservation.py`: **24,457 protected files unchanged**, original manifests,
  four NPZ files and both `.wefusion` hashes reproduce. No download, solver,
  refit, environment installation, long stress, deletion or upload occurred.
- `R/rejections.py` / `rejections.json`: rehashed understated bounds, altered ring/
  parent and wrong cleanup/pose pins reject with child exit **2**, no stdout;
  existing destination rejects with all original output hashes unchanged. SIGINT
  during input admission returns **130 / 0.106 s**, without a manifest. This does
  not qualify cancellation during simplification/publication or production jobs.

Documentation and diff checks pass; native Windows/macOS confirmation,
current-byte dependency observations after relinking, production job/resume,
persistent hybrid bundles, baked-color export and measured navigation remain open.
