# Observed mesh runtime and native view — 2026-10-19 (Asia/Hong_Kong)

M3 remains **IN_PROGRESS**. Both retained partial fusion meshes now pass shared
application admission and render in the native Vulkan viewer. Runtime immutable
mesh transactions, normal-color inspection and scoped Linux checks are verified;
fragmentation/unknown gaps remain visible. No measured geometry, collision/walk,
Q5/N2, hybrid occlusion, complete export or asset gate is certified.

Repository root is the default cwd. Parent `ed0af6d` separately records the
[Windows JSON repair](2026-10-19-windows-fusion-json.md); native Windows rerun
remains pending. Final 25-file source-map SHA-256:
`fe31c5c73ca5e8518888403bb89214f9dd3fa4755a6781cb5f17ae93e6bdaf20`.
Ignored evidence root `R=artifacts/validation/m3-mesh-view-20261019` owns frozen
`source/`, `source-identity.json`, exact command arrays, logs and readbacks.
The [runtime contract](../protocols/scene.md#observed-mesh-runtime) owns layout,
limits, failure behavior and working CLI/viewer commands.

## Implemented behavior and real inputs

Admitted raw positions/normals/indices and complete source/coverage metadata enter
immutable scene chunks. Scene/transaction/renderer fingerprints include mesh
content and dependencies, with an unambiguous version prefix preserving legacy
splat-only digests. Producer aliases cannot mutate committed geometry. Budgets
include mesh arrays/metadata; stale geometry is hidden after invalidation. Bundle
schema 1 refuses mesh snapshots before filesystem writes, preventing silent loss.

The shared staged-upload path now carries indexed geometry with floating-origin
packing and timeline retirement. Mesh-only inspection uses checked FP32 color/D32
depth targets, optical projection, depth-tested two-sided triangles and normal
colors. Targets and staging are charged against actual allocation requirements.
The viewer labels partial surfaces/arbitrary scale/unknown gaps and disables
walking. Open scene supports an observed-geometry request; CLI startup is the
native interaction path exercised here. Existing splat appearance remains a
separate representation; textures/materials and hybrid clipping are absent.

Inputs are the unchanged [native fusion assets](2026-10-19-fusion-admission.md).
Final request SHA-256:

- `R/temporal-request.json`: `ba925e67a2456c2e9035e2641b388feda275e4468e922a68fed525ecdf08cc08`.
- `R/parallax-request.json`: `e23787283b0628b6fd027822a4018630acc8633893c039154879a285455d9d24`.

| Observed native result | Temporal | Parallax |
|---|---:|---:|
| Full vertices / triangles | 32,074 / 47,559 | 61,598 / 82,248 |
| GPU diagnostic frames / visible GUI frames | 36 / 90 | 36 / 90 |
| Vulkan validation errors, both paths | 0 | 0 |
| GUI last reported owned allocation bytes | 104,007,680 | 105,132,512 |

Both full native screenshots were inspected: surfaces remain fragmented with
large gaps. Counts are identities, not a quality comparison between different
view sets/resolutions/extents. GUI readbacks: `*-viewer-stable2.ppm`; GPU checks:
`*-gpu-stable2/`. These are correctness observations, not FPS/P4/reference-machine
benchmarks; reported allocation counts exclude driver/internal allocations and
are not sampled peak RSS/VRAM. No solve/refit, filtering change, environment
installation, download or upload occurred.

## Checks and commands

Linux 7.2.9-arch1-1 x86_64; GCC **16.2.1**, C++26 development/viewer and explicit
C++23 CPU profiles. Native GPU: **RTX 4060 Laptop / 8,188 MiB / driver 615.78.08**.
Existing isolated CPython **3.12.15** / NumPy **2.2.6** runs the independent oracle.
All listed commands exit **0**, except the explicitly retained initial failures.

| Actual command / exact command definition | Result / logs |
|---|---|
| `cmake --build --preset dev -j 4`; `ctest --preset dev` | **327/327**, **95.60 s**; `dev-stable-{build,ctest}.log` |
| `cmake --build --preset compat-cxx23 -j 4`; matching CTest | **304/304**, **95.71 s**; `compat-cxx23-stable-{build,ctest}.log` |
| Both profile executables `we_core_tests '[mesh],[fusion]'` | **15 cases / 272 assertions** each; immutable ownership/retry/hash/context/budget checks, malformed geometry/request rejection and cancellation; `*-stable-focused.log` |
| Native ASan build; `ASAN_OPTIONS=detect_leaks=1 build/asan/we_core_tests '[mesh],[fusion]'` | **15 / 272**, no reported ASan/UBSan/LSan errors; `asan-final-{build,tests}.log` |
| `R/native-stable-commands.json`: `we_gpu_mesh`, both requests | Depth ordering independent of index order; near/far clip, 12 replacements, 800×600 resize/restore, staged 800k-vertex upload, ±billion-unit rebase, invalidation/erase and full real load pass; **36 frames/run**, zero Vulkan errors |
| Same native command record: `we_viewer`, both requests | **90 frames/run**, full counts, labeled ready state inspected; no physical input/HiDPI/platform qualification |
| Same record: existing `we_gpu_smoke` | **51 frames**, splat overlap/replace/delete/resize/acquire retry/budget/post-create recovery pass; zero Vulkan errors. Compositor denies minimize/restore; that check is not qualified |
| Both `we_process inspect-fusion-scene REQUEST SHA` profiles | Corresponding scene hashes/output match byte for byte; `*-temporal-inspect.log`, `*-parallax-inspect.log` |
| Isolated Python `R/raster_oracle.py R/<name>-gpu-stable2` | Independent full-grid optical ray/triangle reference: **1,228,643 pixels/run**, four symmetric/asymmetric/clip images, maximum **one RGB8 level**. Exactly **157/1,228,800** samples excluded within 0.01 pixel of raster-quantized edges; complete denominators in `raster-oracle.json` |
| `python3 R/preservation.py` | **24,457 protected files unchanged**, both source manifests/four NPZ files/two `.wefusion` hashes reproduce; `preservation.json/log` |
| Pinned `python3 tools/check_cpp_style.py` on changed C++; final hash-source recheck | **22 files** and final one-file check pass with format/tidy **23.1.1**, boundaries/warnings unchanged; `style-final.log`, `hash-style.log` |
| `python3 tools/check_docs.py`; `git diff --check` | Pass; `docs.log`, `diff-check.log` |

Final CPU command record `stable-checks.json` SHA:
`c59e18b6e756b8d18ef26e08849ffbdae817edf863a49ee62c0052b787cd15d0`;
native record SHA:
`6694af6ca20acf43939cdb93066a766a1ce25313a4955a93928dc39d188ebb91`.

Initial visible-window fixed-camera test rejected a compositor-sized framebuffer;
fixed-size hidden native GPU readbacks now test calibration, separately from the
visible GUI runs. Initial synthetic transaction setup lacked provenance; corrected
its epoch/provenance fixture. Style findings were fixed with explicit optional
checks and typed Vulkan bitmasks; warnings were retained. Earlier logs and the
pre-hash-prefix source/check snapshot remain intact.

Next: bounded supported-component analysis/cleanup and error-bounded
simplification, preserving original raw meshes, complete support denominators
and unknown gaps. Production jobs, persistent hybrid bundles, baked-color export,
metric alignment, collision and full release/native requirements remain open.
