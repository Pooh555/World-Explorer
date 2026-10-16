# Depth interchange and pose admission — 2026-10-17 Asia/Hong_Kong

Scope/status: **M3 IN_PROGRESS**. The bounded depth interchange/admission slice
is checked locally; no real dense reconstruction, Q5, N2 or full gate is certified.
Parent `173ae19`; implementation is the commit containing this report. Cwd:
repository root. Ignored evidence root **R**:
`artifacts/validation/m3-depth-assets-20261017`.

## Implemented behavior

[Depth V1](../protocols/depth.md) stores little-endian axial camera-Z planes with
explicit validity, relative confidence, observation support and optional standard
deviation. It pins epoch/revision/frame/calibration and pose/original/calibrated
image/recipe hashes. Numeric, size, checksum and raw wire-tag validation precede
allocation/publication. Wrong epoch/revision requests resynchronization; ray
ranges and measured-scale claims are unsupported. Missing uncertainty stays
unavailable; arbitrary pose-camera units and synthetic provenance remain explicit.

The application loads depth against an independently checked canonical pose file
and expected dense-input domain, rejecting missing, invalidated, INITIALIZING or
LOST poses. `we_process inspect-depth FILE SHA256` prints checked format metadata
and labels its narrower scope; it does not admit a pose context or enable geometry.
[ADR 017](../adr/017-depth-interchange.md) retains compressed assets, checked dense
input preparation, MVS/fusion and scale/walking as open work.

Existing durable/exclusive file I/O moves from private map code into `core`, with
a map namespace alias. Independent source comparison confirms only include,
namespace, message and owned staging-name changes. The moved file remains in
`tools/coverage_scope.json`'s package/update scope; no exclusions or thresholds
are weakened. New scene/application headers expose project-owned types only.

## Commands and observed checks

All commands below exit **0**, except the explicitly recorded failures. Existing
profiles use GCC **16.2.1 C++26 Debug**, GCC **C++23 CPU Debug**, and independent
Clang **23.1.1 C++23/libstdc++ Debug**. No GPU execution occurs in this slice.

| Actual command | Result / raw log |
|---|---|
| `cmake --preset dev`; `cmake --build --preset dev -j 4` | Native viewer/CLI/core build succeeds; final `build-dev-final-guard.log` retains the final incremental build |
| `ctest --preset dev --output-on-failure -j 4` | Final **295/295**, **20.55 s**, including **166 worker cases**; `ctest-dev-final.log` |
| `cmake --preset compat-cxx23 -DWE_BUILD_VIEWER=OFF`; `cmake --build --preset compat-cxx23 -j 4`; `ctest --preset compat-cxx23 --output-on-failure -j 4` | **272/272**, **28.26 s** before the final invalidated/tracking-state guard; `ctest-cxx23.log`. Final incremental build plus `build/compat-cxx23/we_core_tests '[depth]' --reporter compact`: **15 cases / 772 assertions**, `depth-cxx23-final.log` |
| `cmake --build build/m2-vma-clang -j 4 --target we_process we_core_tests`; `build/m2-vma-clang/we_core_tests '[depth]' --reporter compact` | Final **15/772**, `build-clang-final.log`, `depth-clang-final.log`; viewer not rerun |
| Native `cmake --build --preset asan -j 4 --target we_core_tests`; `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 build/asan/we_core_tests '[depth]' --reporter compact` | **15/772**, no reported ASan/UBSan/LeakSanitizer errors; `build-asan-native.log`, `depth-asan-native.log` |
| `python3 R/interop_checked.py` | **18** bound CLI commands with actual argv/exits in `R/interop-checked/interop.json`: independent Python fixture read identically by GCC/Clang, 14 rejection probes, both actual saved pose assets read unchanged |
| `python3 R/audit.py` | **1,509 protected files unchanged**, final source/binary/log identities and file-I/O move/scope checked; `audit.json` |

The new 15 synthetic cases cover asymmetric/off-axis Z versus range, unaligned
buffers, wide integer IDs, optional uncertainty, exact independent budgets,
rehashed malformed samples/metadata, every truncated prefix, trailing bytes,
narrowing aliases, stale dependencies, independent pose-file checks, invalidated/
tracking state, cancellation, symlinks and failed/existing publication preservation.
The separate Python producer uses a fixed `struct.pack` layout; its **458-byte**
synthetic fixture SHA:
`fc849f44926ddb93208b3b12d8ff5a1bb83e7ecd8bbbe9232e236ff659de6369`.
Both real retained canonical pose files still inspect as **63 / 80 poses**; this
is a file-I/O regression, not real depth evidence. CPU builds/checks overlap;
durations are command observations, not performance measurements.

clang-format/clang-tidy **23.1.1** pass on all nine changed C++ files / five owning
translation units with `--warnings-as-errors=*`; exact paths are the new core
asset I/O, depth codec, application depth API, CLI and depth tests. Initial tidy
flags oversized enums and Catch's combined FALSE-test flags. Compact enums with
raw uint32 validation before narrowing and equivalent `has_value()==false`
assertions repair them without warning suppression or assertion removal.
Boundary, documentation and whitespace checks pass. Current coverage percentages,
TSan, physical platforms and native viewer/GPU runs are not newly measured.

Initial sandbox ASan build exits **1** during Catch discovery because
LeakSanitizer reports unsupported ptrace (`build-asan.log`). Native process access
passes with leak detection retained. The existing selected-runtime inventory's
`--local-evidence` check exits **2**, `Current runtime payload differs`, after
relinking executables (`runtime-current.log`); old receipts are preserved and
fresh runtime capture remains due. No dependency provisioning occurred.

Final identity/audit SHA:
`135bd332e277cc11c441255e680eefcdc6c226ed119844391af14a8060ce28f7`;
final interoperability receipt SHA:
`21694130a3855396236058c6083909e3799dd2cfa2a02ac67bca1e1cce4da167`.
All saved runs, failed sample_2 stages, source videos, instructions and frozen
fixtures retain prior bytes. No solver, optimizer, long stress, download, upload
or push. Appearance/collision scene publication is unchanged; M3 still needs
qualified dense inputs/worker execution, compressed storage, fusion, independent
scale, supported collision and mesh export.
