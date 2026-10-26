# Generated constant GLB/export manifests — 26 October 2026

**VERIFIED local synthetic export/admission slice; A0/AP0 remain IN_PROGRESS.** New
`WE_GENERATED_CONSTANT_GLB_V1` converts independently admitted static definitions,
actual mesh/material bytes and instances into ordinary GLB plus authoritative
source/provenance/rights/audit sidecars. Source-bound admission checks the complete
canonical package; immutable publication/reload and new-directory materialization
are implemented. The [protocol](../protocols/static_exports.md) owns limits.
Captured GLB and generated-static input formats remain unchanged. No real asset
production, material recovery, relighting, native PBR viewer, Godot or AP pass is
claimed. Expanded native geometry/material conversion and adoption remain open.

Cwd: repository root; timezone Asia/Hong_Kong. Parent
`eea177c4ebebee396a9b7039035fcf9b9b5860f8`.
`R=artifacts/validation/a0-static-export-20261026` (ignored).
Six-file implementation/fixture identity: `source-final.json`, aggregate SHA-256
`e72beed4d045763763a1cb65f6887b503b47fe363d625188221e7fbebbebc379`
(sorted compact path→SHA-256 JSON: CMake and five C++ paths).
GNU 16.2.1 uses dev/ASan C++26 and explicit compat C++23; format/tidy LLVM 23.1.1.

## Exercised behavior and corrections

Fixtures exercise pivot baking, wxyz→xyzw TRS, float32 errors, exact definition
identity, repeated nodes sharing resources, individual/assembly geometry/material
hash equality, complete sidecar closure, declared audits, package corruption,
wrong/missing source/audit/report pins, count/byte bounds, cancellation/replay,
stale commits, missing committed files, new-directory writes and source protection.
Unsupported occlusion/HDR emission/hidden nodes/nonunit rotation/local coordinate
range reject. Validator observations require independent target GLB and tool/report
hashes; reported counts alone cannot bind another payload. All audit outcomes
remain unqualified; synthetic importer declarations are not engine execution.

`build-first.log` fails on a missing direct immutable-record header, repaired in
`build-second.log`. `build-tests.log` catches a missing fixture result helper and
partial initializers; `build-tests-second.log` catches missing stop-token arguments
in test reads. `build-tests-third.log` passes. `style-first.log` rejects a missing
direct span include; it is added. `focused-first.log` passes 16 cases/2,429
assertions. Review adds explicit reported-target SHA and coordinate bounds and
limits the optional environment-based evidence hook to Linux (avoiding an MSVC
CRT dependency); normal tests and exporter remain portable. Earlier successful
build/full-suite/style logs remain `*-before-review.log`. Final checks below use
the final source, without weakened warnings/standards.

## Actual checks

```sh
cmake --build --preset dev -j 4
cmake --build --preset compat-cxx23 --parallel 4
build/dev/we_core_tests '[static-export],[gltf],[asset-edit],[asset-schema]'
build/compat-cxx23/we_core_tests '[static-export],[gltf],[asset-edit],[asset-schema]'
ctest --preset dev -j 4
ctest --preset compat-cxx23 -j 4
cmake --build --preset asan -j 4 --target we_core_tests
build/asan/we_core_tests '[static-export],[gltf],[asset-edit],[asset-schema]'
python3 "$R/libcxx_probe.py" libcxx23-final
python3 "$R/check_preservation.py"
python3 tools/check_docs.py
git diff --check
```

| Check | Exit / observation | Log under `$R` |
|---|---|---|
| Dev/compat builds | 0 / both pass | `build-{dev,compat}.log` |
| Dev/compat focused | 0 / 45 cases, 96,537 assertions each | `focused-{dev,compat}.log` |
| Full dev CTest | 0 / 823 of 823, 186.13 s | `ctest-dev.log` |
| Full compat CTest | 0 / 798 of 798, 184.16 s | `ctest-compat.log` |
| Native ASan/UBSan/default LeakSanitizer | 0 / build and 45 cases, 96,537 assertions; no findings | `build-asan.log`, `focused-asan.log` |
| Repo format/tidy/whitespace gate | 0 / five C++ files | `style-final.log` |
| Additional private encoder header analysis | 0 / no first-party findings | `tidy-codec.log` |
| New test TU / retained libc++ 23.1.1 | 0 / C++23 syntax only | `libcxx23-final.log`, `libcxx23-final-argv.json` |
| Protected data rehash | 0 / 378 hashes, zero mismatches | `preservation.json` |
| Documentation/diff checks | 0 / 1,584 targets, 301 files; clean diff | `docs.log`, `diff.log` |

Style uses exact LLVM tools via PATH and `python3 tools/check_cpp_style.py` on the
five C++ paths in `source-final.json`; retained `m3-frame-quality-20261023/format_cpp.py`
formats them. Additional analysis uses LLVM clang-tidy, `-p build/dev`,
`--warnings-as-errors=*` and header filter
`(/src/application/static_export_glb.hpp|/include/world_explorer/)` on
`src/application/static_export.cpp`. No tracked Python changed. Native sanitizer
access keeps default LeakSanitizer enabled. The libc++ syntax check does not
resolve the separate Jolt C++17 PCH blocker or qualify native macOS.

## Actual portable artifact and independent checks

The Linux-only probe runs the single `static export synthetic external validation
probe` Catch case with `WE_EXPORT_PROBE_OUTPUT="$PWD/$R/synthetic-original"`.
It writes an authored synthetic three-vertex/one-triangle asset reused by two
nodes. The GLB is **1,492 bytes**, SHA-256
`0a00277c98a88668053cc6d9cdb716ad78379bda8046545a7606f676e4d76357`.
`python3 "$R/oracle.py"` independently checks little-endian headers, every vertex/
normal/UV/index, material factors, shared node resources, canonical resource hashes
and all nine package-file hashes (`oracle.json`).

`node "$R/validate.cjs" "$R/synthetic-original/scene.glb" "$R/validator-report.json"`
runs the retained [Khronos validator](https://github.com/KhronosGroup/glTF-Validator),
version **2.0.0-dev.3.10**: **0 errors, 0 warnings, 1 info** (`validator.log`).
The info is UNUSED_OBJECT for retained TEXCOORD_0, intentionally unused by constant
materials. No package/model installation or external resources are involved.
Executable/package hashes are in `tool-identity.json`; package aggregate is
`9034d7101c7266ae7a86d75624718b4dc6ee54ab722fc4b9a896074ad8b31f6e`.

`audit-input.json` binds the actual target hash, tool identities and validator JSON;
the probe normalizes that report's JSON formatting before selecting its bytes.
Re-running the probe with `WE_EXPORT_PROBE_AUDIT="$PWD/$R/audit-input.json"` writes
`synthetic-reported`; final-source output is `synthetic-final` (`probe-final.log`).
Both retain identical GLB bytes and admit REPORTED_PASS. `reported-parity.json`
and `final-export.json` check sidecar hashes; `synthetic-final` has ten files.
The package stays UNQUALIFIED_EXPORT, with importer NOT_RUN. Godot/godot4 are not
on PATH (`environment.json`); no ordinary engine import ran.

All data stays local; no real reconstruction, GPU/viewer run, runtime-inventory
refresh, upload or native Ubuntu GCC 13/Windows/macOS rerun occurred. Runtime
receipts remain stale after relinking. Existing immutable-store recovery is reused;
no new crash/disk-full/concurrent-mutation qualification is added. Real A1
completed/PBR viewing, export/import parity and full AP gates remain required.
