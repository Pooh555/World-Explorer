# Clang mesh hashing and component support — 2026-10-19 (Asia/Hong_Kong)

M3 remains **IN_PROGRESS**. The reported Clang signedness error is repaired and
read-only component/support analysis is implemented. Native hosted linux-clang
and macOS confirmation remains **IMPLEMENTED_UNVERIFIED**. No Q5/N2, cleanup,
simplification, collision, measured scale or asset gate is certified.

Repository root is the default cwd. Parent: `93141b9`. Ignored evidence root
`R=artifacts/validation/m3-mesh-components-20261019` retains frozen source,
`source-identity.json`, exact command arrays, before/after failures, reports,
independent oracle, viewer captures and preservation inventory.
Final eight-file source-map SHA-256:
`b55ceeda9e342e81f5288560a6233e98c14297b4ea8cd10228890db3cf85def3`.

## Repair and implemented analysis

`scene_store.cpp` iterated a character string as unsigned characters. Clang's
`-Wconversion -Werror` rejects the implicit signedness change. Iteration now uses
`char` with an explicit unsigned-byte conversion before hashing, preserving the
original byte stream. `R/reproduction.json` records C++23 syntax checks using
Clang **23.1.3** with libstdc++ and isolated libc++ **23.1.1**: original source
exits **1** with the reported diagnostic; repaired source exits **0** in both.
No warning suppression, dependency patch or default-standard downgrade.
This is Linux verification, not a native Clang 19/macOS runner pass.

The [component contract](../protocols/fusion.md#read-only-component-and-support-analysis)
owns the CLI, bounds and semantics. The shared native loader checks all source/
pose dependencies before analysis. Vertex-index connectivity, every component's
area/triangle/vertex totals, exact zero-area triangles, all stencil-coverage bins,
weights/support/front/optional sigma and original full-grid coverage survive in
the JSON report. Cancellation/failure emits no partial report and changes no
caller data. No position welding or geometry editing occurs.

## Complete retained real meshes

`R/analysis-commands.json` records both full commands using the unchanged
[admitted fusion assets](2026-10-19-fusion-admission.md). The input request pins
used to reopen the viewer are unchanged from the [runtime evidence](2026-10-19-observed-mesh-view.md).

| Reported diagnostic | Temporal | Parallax |
|---|---:|---:|
| Vertices / triangles | 32,074 / 47,559 | 61,598 / 82,248 |
| Components / unused vertices / exact zero-area triangles | 800 / 0 / 0 | 1,864 / 0 / 0 |
| Total area, arbitrary units² | 1.4227648452782662 | 16.270551449244497 |
| Vertices with eight observed corners | 23,678 / 32,074 | 40,846 / 61,598 |
| Triangles with any unknown stencil corner | 12,261 / 47,559 | 30,389 / 82,248 |
| Vertices with zero or one observed corner | 0 | 0 |
| Input sigma availability | Unavailable | Unavailable |

All vertices have at least two observed stencil corners. This diagnostic provides
no basis for deleting fragments solely by size; incomplete stencils also do not
establish false surfaces. Different input view sets/extents/resolutions prohibit
area/count quality comparisons. Frozen reports retain every component, source
coverage and unknown denominator. Raw meshes remain fragmented and unchanged.

`R/oracle.py` independently reads original checksum-verified NPZ arrays, uses
SciPy graph traversal, NumPy triangle areas and a scalar lattice dictionary.
It matches **all 2,664 components, 93,672 referenced vertices, 129,807 triangles
and 749,376 corner queries**, including each component's sums/min/max/histogram.
Largest absolute floating difference: **2.17e-13**; integer counts match exactly.
No solve, MVS filtering change, refit, environment installation or download.

## Actual verification

Linux **7.2.9-arch1-1**, GCC **16.2.1** C++26 development, Clang **23.1.3** C++23
viewer/libstdc++ and C++23 CPU/libc++ **23.1.1**; formatter/tidy **23.1.1**.
Independent oracle: existing isolated fusion CPU Python **3.12.15**, NumPy
**2.2.6**, SciPy **1.18.1**; exact version commands in `R/environment.json`.

- `cmake --build --preset dev -j 4` and both Clang builds exit **0**.
  `R/checks.json` / `R/checks.py` own exact build/CTest/focused command arrays:
  Clang C++23 viewer **333/333 / 97.28 s**, CPU libc++ **310/310 / 89.91 s**.
  GCC C++26 **333/333 / 87.66 s**. All three focused profile runs pass
  **21 cases / 372 assertions** each; complete logs remain in R.
- `R/cross-profile-analysis.json`: both full reports also reproduce under the
  Clang viewer/libstdc++ and CPU/libc++ executables, with exact integer/metadata
  parity and floating agreement within relative **1e-11** / absolute **1e-12**.
- `build/dev/we_core_tests '[fusion-components]'`: **6 cases / 100 assertions**,
  including analytic areas, connected/coincident/unused vertices, negative floors,
  unknown corners, optional sigma, exact budgets, malformed inputs and cancellation.
- `R/native.py` reopens both original requests via native `we_viewer` for **90
  frames each**, full counts and **zero Vulkan validation errors**. Both screenshots
  inspected: partial normal-color surfaces, arbitrary scale, unknown gaps and
  walking-disabled labels remain visible. Device: RTX 4060 Laptop; allocation
  totals **104,007,680 / 105,132,512 bytes**, matching the prior view. These are
  correctness observations, not performance/peak-memory/reference qualification.
- `R/preservation.py`: **24,457 protected files unchanged**; original manifests,
  four NPZ outputs and two `.wefusion` hashes reproduce. Prior evidence is retained.
- Native `cmake --build --preset asan -j 4`, then
  `ASAN_OPTIONS=detect_leaks=1 build/asan/we_core_tests '[fusion-components],[fusion],[mesh]'`:
  **21 cases / 372 assertions**, no reported ASan/UBSan/LSan errors. The sandbox
  discovery attempt failed because LSan cannot operate under ptrace; native
  build/discovery/test succeeds (`asan-{native-build,native-tests}.log`).
- Pinned `python3 tools/check_cpp_style.py` on the seven paths in
  `R/cpp-paths.txt` passes format/tidy/boundaries/docs/whitespace unchanged;
  `style-complete.log`. `python3 tools/check_docs.py` and `git diff --check`
  pass (**858 local targets / 177 files**); `docs.log`, `diff-check.log`.

The initial style run found a Catch2 `REQUIRE_FALSE` expansion triggering the
enum-range analyzer; explicit expected-value checks retain all assertions without
suppression. Its failing log remains `style-initial.log`. Native Windows/macOS
workflow reruns and current-byte loader/mapping receipts after relinking remain
due. Next: freeze an explicit error bound and conservative topology/support policy
for separate immutable simplification outputs, with whole-surface distance
validation and unchanged raw comparisons. No small-component removal is qualified.
