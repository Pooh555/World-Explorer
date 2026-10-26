# Native textured PBR glTF export — 26 October 2026

**A0/AP0 remain IN_PROGRESS.** The [separate native profile](../protocols/native_exports.md)
converts actual admitted mesh/channel bytes into individual and assembled glTF
documents sharing the same buffers/PNGs. It binds source receipt/context and
definition/instance/provenance identities, checks conversion limits and publishes
an exact closed package through existing immutable recovery. Existing captured
and generated-static profiles remain unchanged. This synthetic format evidence
qualifies no real production, intrinsic PBR/relighting, native viewing or AP gate.

Repository-root cwd: `/home/Pooh555/programming/World-Explorer`. Clean parent
`a24924ba374902802c95bcd6d97e5ad8e669b918`. Final implementation identity
`2e741dd57d602e58191547e7e84f4ff5bda401fb67c0d462553d643222cbc13a`
is SHA-256 of sorted compact path→file-SHA JSON for CMake, five added C++ files
and the extended existing fixture. Ignored evidence root
`R=artifacts/validation/a0-native-export-20261026` contains `source-final.json`,
`environment.json`, exact command arrays/cwd/exits/timings in per-check JSON and
raw logs. No tracked Python changes or dependency/model installation occurs.

## Reproduction and results

Local GNU16.2.1, CMake4.4.4, explicit C++26 development/C++23 compatibility;
workspace LLVM format/tidy23.1.1. The focused filter is
`[native-export],[native-assets],[static-export],[gltf],[asset-edit],[asset-schema]`.

```sh
cmake --build --preset dev -j 4
cmake --build --preset compat-cxx23 --parallel 4
ctest --preset dev
ctest --preset compat-cxx23
cmake --build --preset asan -j 4
build/asan/we_core_tests '[native-export],[native-assets],[static-export],[gltf],[asset-edit],[asset-schema]'
```

| Final check | Observed result / log in R |
|---|---|
| Full development build / CPU-worker suite | Exit 0; **843/843**, 658.99 s; `build-dev.log`, `ctest-dev.log` |
| Explicit C++23 build / CPU-worker suite | Exit 0; **818/818**, 633.73 s; `build-compat.log`, `ctest-compat.log` |
| Focused development / compatibility | Exit 0 each; **65 cases /112,431 assertions** each; `focused-dev.log`, `focused-compat.log` |
| Native sanitizer build / focused execution | Exit 0; **65 cases /112,431 assertions**, no findings; `build-asan.log`, `focused-asan.log` |
| Repo format/tidy/boundary gate | Exit 0, six C++ files; `style-final.log` |
| Three libc++ syntax checks | Exit 0 each; `libcxx-{codec,application,tests}.log` |

Builds, analysis and tests overlapped; these durations are observations, not a
controlled performance benchmark. Native direct commands have recorded exits
without an inferred duration.

Documentation checks pass (1,611 local targets in 305 files); whitespace checks
exit 0. `docs.log` and `diff.log` retain the final results.

Ten new cases inspect actual encoded attributes/indices and patterned channel
pixels, material-group/source-face maps, distinct semantic asset/resource offsets,
instance sharing, deterministic individual/assembly equivalence, manual ancestry,
foreign/stale/altered/oversized/hidden rejection, independent world-error limits,
captured/HDR/negative-normal-Z/instance-override refusals, cancellation, immutable
recovery/replay and protected/new-directory writes. These authored fixtures are
synthetic diagnostics, not reconstructed assets. Scoped Clang23.1.4/libc++23.1.1
syntax checks cover all three new translation units; exact arguments/results are
retained. They are not a full libc++ or native Windows/macOS/GCC13 qualification.

```sh
PATH="$PWD/artifacts/tools/llvm-official-23.1.1/bin:$PATH" \
  python3 tools/check_cpp_style.py \
  include/world_explorer/application/native_export.hpp \
  src/application/native_export_internal.hpp src/application/native_export_codec.cpp \
  src/application/native_export.cpp tests/unit/native_export_tests.cpp \
  tests/fixtures/native_assets_fixture.hpp
python3 tools/check_docs.py
git diff --check
```

## Actual synthetic artifact and external validation

The Linux opt-in hook runs the ordinary prepare/admit/write path:

```sh
WE_NATIVE_EXPORT_PROBE_OUTPUT="$PWD/$R/synthetic-final" \
  build/dev/we_core_tests 'native texture export converts actual channels and shares resources between assets and assembly'
node "$R/validate.cjs" "$R/synthetic-final/environment.gltf" "$R/validator-assembly-report.json"
node "$R/validate.cjs" "$R/synthetic-final/assets/42/asset.gltf" "$R/validator-asset-report.json"
python3 "$R/oracle.py"
```

The probe passes **one case /335 assertions**. Retained Khronos
Validator2.0.0-dev.3.10 under Node26.12.1 reports **zero errors, warnings and infos**
for both documents, loading nine external resources each. Raw reports/resource
loads and current Node/package file hashes are retained; no tool is downloaded.
The package deliberately retains validator/importer NOT_RUN because it predates
these external observations. Godot/godot4 are absent from PATH; no engine import
or native PBR view is performed.

The independent Python standard-library oracle uses struct, CRC32 and zlib to
decode actual PNG/binary bytes, compare known patterned texels, preserved world
positions/attributes/indices and material references, require the exact closed
file inventory and verify all hashes. It confirms one four-vertex/two-triangle
mesh, two materials, two shared-mesh nodes and eight PNGs. `oracle.json` owns exact
artifact hashes/counts; `oracle-check.json` records the command result. The
24-file terminal manifest is `0338e8190b295aaff2a2e62e66b9ee18327a9b0c26f8f105261ff160b0938043`;
`environment.gltf` is `6ce44e184bed1af340daf5486f5f38b401ad4f9b1c65e56c74ecaa262bb54cfc`.

## Failures, preservation and limits

`build-first.log` records ambiguous empty aggregate initializers in a byte array;
explicit byte values repair it. Subsequent test builds reject a string/JSON
comparison; explicit typed strings fix it without changing assertions. Review
adds refusal of alternate instance material selections. The first broad run then
correctly rejects two fixtures still requesting those overrides; the exporter
fixtures now explicitly choose one original selection, with a separate rejection
case for alternatives. Earlier logs remain named `*-before-fixture-policy.*`.

The first native sanitizer launch was not executed: automatic approval review
exceeded its deadline. One permitted retry using the direct CMake command was
approved. Native sanitizer options remain unset, including default leak detection;
no warning/standard/sanitizer is weakened. The oracle command record initially
overwrote its same-named result JSON; distinct result/check names restore both
without changing the artifact or validation outcome.

Saved-data verification checks **378 original hashes with zero mismatches**.
The previous generated-constant GLB/manifest and product readers remain unchanged.
No video, model environment, inference, GPU or real capture is processed. Runtime
dependency receipts remain stale after relinking. Native grids remain ≤256²;
broader profiles, optional native GLB, full producer/rights/capture freeze and real
A1/PBR/Godot validation stay open. The [handoff](../progress.md#current-a0-work)
owns the next native rendering step.
