# A0 native-source geometry products — 26 October 2026

**VERIFIED local synthetic CPU slice; A0/AP0 remain IN_PROGRESS.** The shared application
command admits a native high/source mesh, runtime geometry attributes and complete
per-face correspondence/support. Exact source/capture/receipt pins and owned
two-record publication/reload preserve independent admission. The
[protocol](../protocols/geometry_products.md) owns fields, budgets and limits.
Generated-static v1 payloads/edits remain unchanged. No producer, physical support,
measured scale, renderer adoption, PBR or AP/Q/N pass is supplied.

Cwd: repository root; timezone Asia/Hong_Kong. Parent
`cd25fbe129872f80b0e01bab93cf9887fc5b0180`.
`R=artifacts/validation/a0-geometry-product-20261026` (ignored).
Six-file implementation/fixture identity: `source-final.json`, aggregate SHA-256
`170cb3dcd2772b017bbbce4755e2e7af06bdec67ec26b7f0c7c2422484adf636`
(sorted compact JSON of path→SHA-256), covering CMake and all five changed C++ files.
GNU 16.2.1 uses C++26 for dev/ASan and explicit C++23 for compatibility;
format/tidy tools are LLVM 23.1.1.

## Exercised behavior and corrections

Asymmetric synthetic native/runtime triangles exercise exact source transforms,
normal/UV/tangent/group validation, source-frame/pose bindings and both observed
and inferred support. Observed coverage requires exact anchors within one source
triangle; inferred displacement, unknown/generated areas, nullable uncertainty
and transitive manual ancestry remain explicit. Closed tetrahedra, open boundaries,
disconnected components and bow-tie fans exercise topology. Finite local points
cannot overflow the native world transform. Rehashed malformed attributes,
support, dependencies, role aliases, metadata and independent substitutions fail.
Owned handles survive caller/source removal. Staged cancellation, exact retry,
current selected reload, conflicting operations and receipt overlap are checked.

`build-first.log` exits 1: project `Vec3d` has no division operator. Checked scalar
reciprocal multiplication repairs the derivatives. `focused-first.log` then passes
11 cases/5,714 assertions. `style-first.log` exits 1 with 13 findings: optional
access guards and one vector reservation were made explicit without suppressions.
Expanded `focused-second.log` passes 15 cases/6,780 assertions; `style-second.log`
passes. Review also found that the initial 8 MiB admission budget exceeded the
registry's 4 MiB record cap. The new profile now uses 4 MiB, with an exact-limit
root/source publish/reload regression. No failing runtime test is recorded.

## Actual checks

Final-source commands; logs below are relative to `$R`:

```sh
cmake --build --preset dev -j 4
cmake --build --preset compat-cxx23 --parallel 4
build/dev/we_core_tests '[geometry-product],[asset-edit],[capture-measurement],[capture-selection]'
build/compat-cxx23/we_core_tests '[geometry-product],[asset-edit],[capture-measurement],[capture-selection]'
ctest --preset dev -j 4
ctest --preset compat-cxx23 -j 4
cmake --build --preset asan -j 4 --target we_core_tests we_geometry_product_probe
build/asan/we_core_tests '[geometry-product],[asset-edit],[capture-measurement],[capture-selection]'
```

| Check | Exit / observation | Log |
|---|---|---|
| Final-source dev/compat builds | 0 / both pass | `build-{dev,compat}-final.log` |
| Final-source dev focused | 0 / 49 cases, 16,276 assertions | `focused-dev-final.log` |
| Final-source compat focused | 0 / 49 cases, 16,276 assertions | `focused-compat-final.log` |
| Full dev CTest | 0 / 755 of 755, 86.60 s | `ctest-dev-final.log` |
| Full compat CTest | 0 / 730 of 730, 89.58 s | `ctest-compat-final.log` |
| Native ASan/UBSan + default LeakSanitizer | 0 / build and 49 cases, 16,276 assertions; no findings | `build-asan-final.log`, `focused-asan-final.log` |
| Format/tidy/whitespace gate | 0 / five C++ files | `style-final.log` |
| Full new test TU / retained libc++ 23.1.1 | 0 / explicit C++23 syntax only | `libcxx23-final.log`, `libcxx23-final-argv.json` |
| Protected data rehash | 0 / 378 hashes, zero mismatches | `preservation.json` |
| Documentation links / diff whitespace | 0 / 1,552 targets, 292 files; clean diff | `docs-final.log`, `diff-final.log` |

Style uses `PATH="$PWD/artifacts/tools/llvm-official-23.1.1/bin:$PATH"` and
`python3 tools/check_cpp_style.py` on the five C++ paths in `source-final.json`.
Formatting uses retained ignored `artifacts/validation/m3-frame-quality-20261023/format_cpp.py`.
Probe commands: `python3 "$R/libcxx_probe.py" libcxx23-final`,
`python3 "$R/check_preservation.py"`. The libc++ probe uses Clang 23.1.3 and does
not resolve the separate Jolt C++17 PCH blocker or qualify native macOS.
Final docs checks are `python3 tools/check_docs.py`, `git diff --check`.
No tracked Python changes. Native sanitizer permission is needed for default
LeakSanitizer discovery/execution. Automatic approval review initially timed out
before the build ran; the tool-permitted single retry was approved.

## Retained real mesh refusal

The final-source development probe runs:

```sh
build/dev/we_geometry_product_probe artifacts/validation/m3-mesh-bundle-20261019/parallax-raw.wexplore d081bedb87fd8166f949c3f8ec44490defa512de973bd6719da14fff605c49c4
```

The bounded native loader reopens the selected partial M3 mesh, content SHA-256
`8e9c9d71b740a9cb25fd8f74352ba008003eefb34a1c2dbcd9a76b17fd4d1a18`.
The shared command then exits **2** naming missing independent geometry, capture
and receipt pins. All four selected bundle file hashes match the pre-run snapshot.
Exact argv/result are in `real-refusal-final.json`, output in
`real-refusal-final.log`, source pins in `real-source-before.json`. No geometry
product was admitted or published for this real source; this is a prerequisite
refusal and saved-mesh read check, not an observed-quality or A1 pass.

## Remaining scope

Physical source/view support, depth-receipt re-admission, uncertainty calibration,
self-intersections, UV/bake quality, LOD/collider and full asset/material adoption
remain open. No separate crash/disk-full/concurrent-mutation probe is added; the
existing immutable store is reused. No model install/download, media processing,
GPU/render/runtime-inventory refresh, upload or native Windows/macOS qualification
ran. Runtime receipts remain stale after linking. A0 product/material/structure/
completion/export work and real A1 native PBR/Godot validation remain required.
