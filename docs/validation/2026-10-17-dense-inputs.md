# Native dense-input preparation — 2026-10-17 Asia/Hong_Kong

Scope/status: **M3 IN_PROGRESS**. Retained-input preparation is checked locally;
no MVS/depth/fusion, Q5, N2 or full gate is certified. Parent **a39b936**;
implementation is the commit containing this report. Cwd: repository root.
Ignored evidence root **R**: `artifacts/validation/m3-dense-input-20261017`.

## Behavior and real correspondence

The [native-input contract](../protocols/dense_inputs.md) checks completed
pose/decode inventories against the exact canonical asset, reads bounded native
COLMAP image poses without linking the solver, checks every calibrated image's
hash/RGB8 PNG header/grid, and supplies independent depth expectations. Immutable
manifest publication refuses overlaps/existing entries; loading recomputes the
inputs and rejects changed metadata. Preprocessing and the complete selected
depth recipe have separate hashes. Recipe identity/profile checking does not
qualify or execute its backend. PNG pixel decoding stays with the isolated worker.

All **63 saved sample_1 frames**, at **4104×2208**, prepare with no crop, resize,
solve or refit. Source SHA:
`51263d9991dcbc2136d0809ecba1a8ba1b261896f3b222b7afba19784a153ef6`;
canonical `.weposes` SHA:
`d0383cb0510d82c7c92cb8184f1214c688bac11619ab65b232c9ec3d66db3abf`.
Manifest **R/sample_1-dense.json** SHA:
`62ac230c1ad5346b802acd386c11836f7f17e8a1cba9b7163714fb915ca038cc`.
The selected depth recipe is explicitly **UNQUALIFIED_NOT_EXECUTED**, an
identity-only probe, **not a runnable MVS recipe**; exact SHA:
`1305829c09bd2d28da136e36f184e876fab62f1d350f98b29faba56257d4fe3a`.
An actual worker must pin its complete qualified recipe in a new manifest.

Independent installed pycolmap **4.2.1**, Pillow **11.3.0**, NumPy **2.2.6** read
all retained model/image bytes. Intrinsics match exactly; maximum canonical
pose-matrix difference **8.88178e-16**, Brown-Conrady versus backend projection
**4.54747e-13 pixels**. All **126 original/calibrated images** decode. Across
**7,623** bilinear undistortion samples, no point leaves the source grid;
RGB8 maximum/mean/p99 error **0.499913 / 0.117249 / 0.489451** levels. An unequal
per-axis resize calculation preserves rays and detects the incorrect
`cx' = sx*cx` formula; this is mathematical evidence, **not resized output**.
Independent receipt **R/independent-real.json** retains the exact values.

## Actual commands and results

All listed checks exit **0** unless stated. Toolchains: GCC **16.2.1 C++26 Debug**,
explicit GCC **C++23 CPU Debug**, Clang **23.1.1 C++23/libstdc++ Debug**.
No GPU work occurs. Durations include competing builds and are not benchmarks.

| Command | Result / log under R |
|---|---|
| `cmake --preset dev`; `cmake --build --preset dev -j 4` | Native viewer/CLI/core build; `configure-dev.log`, `build-dev-final.log` |
| `ctest --preset dev --output-on-failure -j 4` | Final **303/303**, **25.56 s**; `ctest-dev-final.log` |
| `cmake --preset compat-cxx23 -DWE_BUILD_VIEWER=OFF`; `cmake --build --preset compat-cxx23 -j 4`; `ctest --preset compat-cxx23 --output-on-failure -j 4` | **279/279**, **38.20 s** before the final native-grid case/ID guard; `ctest-cxx23.log`. Final targeted build/check: **8 cases / 178 assertions**, `build-cxx23-final.log`, `dense-cxx23-final.log` |
| `cmake --build build/m2-vma-clang -j 2 --target we_process we_core_tests`; `build/m2-vma-clang/we_core_tests '[dense]' --reporter compact` | Final **8/178**; `build-clang-final.log`, `dense-clang-final.log` |
| Native `cmake --build --preset asan -j 2 --target we_core_tests`; `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 build/asan/we_core_tests '[dense]' --reporter compact` | Final **8/178**, no reported sanitizer errors; `build-asan-native-final.log`, `dense-asan-native-final.log` |
| `python3 R/prepare_real.py`; `.venv-worker/bin/python R/check_real.py` | Checked native CLI manifest, exact argv/exit in `real-preparation-command.json`; independent comparisons in `independent-real-corrected.log` |

Eight synthetic cases cover asymmetric axes/quaternions, every truncated binary
prefix, trailing bytes, unsafe/duplicate identities, nonfinite tracks/invalid
rotations and count bounds; rehashed pose/name/camera/grid failures; publication,
manifest/recipe mutation, cancellation, input preservation, and depth admission
from independently prepared metadata. The 4104-wide case rejects depth's default
4096 cap and admits only with an explicit supported dimension budget; no implicit
resizing or global limit change. **Six additional CLI probes** retain argv/exits
in `cli-checks.json`: Clang and repeated GCC manifests match every byte; occupied,
wrong-source, wrong-recipe and input-overlap requests return expected **2**.

clang-format/clang-tidy **23.1.1** check all eight changed C++ files / five owning
translation units with `--warnings-as-errors=*`; `tidy-checked.log` passes.
Include boundaries, docs and whitespace checks pass. Initial fixture tests lacked
the required `points3D.bin` inventory entry; fixing the synthetic fixture gives
passes without relaxing production admission. Clang catches a signed fixture
index; tidy requires explicit multiplication widths and an unambiguous helper.
The initial independent observer assumed sparse model 0; honoring retained
`model_selection.json` selects model 1. Initial failures/logs remain in R.

Sandbox sanitizer discovery fails with LeakSanitizer's ptrace limitation;
native access passes with leak detection enabled. The default historical
dependency manifest reports changed dependency evidence. The explicitly selected
`third_party/dependency-audit-input-20261016.json --local-evidence` check returns
**2**, `Current runtime payload differs`, after rebuilding; exact command/exit
is in `runtime-current-selected-command.json`. Fresh runtime observations remain
due; no receipts or dependencies were changed to claim a pass.

The final preservation/identity audit is **R/audit.json**: **1,655 protected files**
from all saved runs/sources and both frozen-fixture evidence directories retain
their baseline bytes. No installed package/model changes, solver/optimizer,
long stress, scene publication, upload or push. M3 still requires a qualified
separate dense worker (current pycolmap `has_cuda=false`, no `colmap` executable),
compressed storage, real fusion, measured scale, collision/walking and mesh export.
A0–A6/AP gates remain unstarted; M4 qualification is not begun.
