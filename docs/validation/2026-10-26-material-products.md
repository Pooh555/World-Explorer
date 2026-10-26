# A0 bounded material products — 26 October 2026

**VERIFIED local synthetic CPU admission slice; A0/AP0 remain IN_PROGRESS.** The shared
application command owns actual material channel bytes, complete texel provenance,
explicit inference-view light/exposure inputs and original geometry/capture/
receipt bindings. Atomic publication/reload checks the entire material closure.
The [protocol](../protocols/material_products.md) owns fields, budgets and limits.
Generated-static/constant-PBR v1 encodings remain unchanged. No actual fitting,
physical albedo/relighting, renderer/asset adoption or AP gate is qualified.

Cwd: repository root; timezone Asia/Hong_Kong. Parent
`bdca8fb39c41f222d1b76db2079622f455317604`.
`R=artifacts/validation/a0-material-product-20261026` (ignored).
Seven-file implementation/fixture identity: `source-final.json`, aggregate SHA-256
`a88624f6e017d16a7ecd8788b5a8722d313aed187248f954c6e090c2e448d9bb`
(sorted compact JSON of path→SHA-256), covering CMake and six changed/new C++ files.
GNU 16.2.1 uses C++26 for dev/ASan and explicit C++23 for compatibility;
format/tidy tools are LLVM 23.1.1.

## Exercised behavior and corrections

Independent binary fixtures exercise exact little-endian headers, channel samples,
signed unit tangent normals, scalar/color ranges and native provenance. Captured
radiance remains unlit; estimated PBR requires light/exposure inputs and refuses
generated/edited texels. Generated mixed observed/inferred/generated/unknown
partitions and declared edits remain explicit. Rehashed schema/profile/channel/
source/dependency/fit substitutions fail. Missing original handles, foreign
receipt/context, evaluation dependency aliases, absent UV/tangents, bad variants,
oversized/deep/duplicate/NUL JSON and one-past grid bounds fail. The 256×256
six-channel boundary publishes/reloads; it is not AP5 texture-quality evidence.
Owned values survive source lifetime. Cancellation after 1, 7 and 15 staged
records exposes no partial closure; matching retries reopen all 15 records.
Missing channel, provenance or fit records fail even with a committed valid root.

Initial application build passes (`build-first.log`). `build-tests-first.log`
exits 1 because a new test referenced a nonexistent fixture member; it now inspects
the original geometry handle explicitly. `focused-first.log` exits 42, with
12/13 cases passing: the firewall test selected an incorrect held-out image ID.
The corrected case selects the actual frozen held-out image. `style-first.log`
exits 1 with 11 findings: the byte reader now uses explicit 32/64-bit template
widths, and generated integer switches fail on unexpected cases. No warning
suppression or policy threshold was changed.

## Actual checks

Final-source commands; logs below are relative to `$R`:

```sh
cmake --build --preset dev -j 4
cmake --build --preset compat-cxx23 --parallel 4
build/dev/we_core_tests '[material-product],[geometry-product],[asset-edit],[capture-selection]'
build/compat-cxx23/we_core_tests '[material-product],[geometry-product],[asset-edit],[capture-selection]'
ctest --preset dev -j 4
ctest --preset compat-cxx23 -j 4
cmake --build --preset asan -j 4 --target we_core_tests
build/asan/we_core_tests '[material-product],[geometry-product],[asset-edit],[capture-selection]'
```

| Check | Exit / observation | Log |
|---|---|---|
| Dev/compat builds | 0 / both pass | `build-{dev,compat}.log` |
| Dev focused | 0 / 55 cases, 27,918 assertions | `focused-dev.log` |
| Compat focused | 0 / 55 cases, 27,918 assertions | `focused-compat.log` |
| Full dev CTest | 0 / 772 of 772, 125.29 s | `ctest-dev.log` |
| Full compat CTest | 0 / 747 of 747, 135.70 s | `ctest-compat.log` |
| Native ASan/UBSan + default LeakSanitizer | 0 / build and 55 cases, 27,918 assertions; no findings | `build-asan.log`, `focused-asan.log` |
| Format/tidy/whitespace gate | 0 / six C++ files | `style.log` |
| Full new test TU / retained libc++ 23.1.1 | 0 / explicit C++23 syntax only | `libcxx23.log`, `libcxx23-argv.json` |
| Protected data rehash | 0 / 378 hashes, zero mismatches | `preservation.json` |
| Documentation links / diff whitespace | 0 / 1,560 targets, 294 files; clean diff | `docs.log`, `diff.log` |

Style uses `PATH="$PWD/artifacts/tools/llvm-official-23.1.1/bin:$PATH"` and
`python3 tools/check_cpp_style.py` on the six C++ paths in `source-final.json`.
Formatting uses retained ignored `artifacts/validation/m3-frame-quality-20261023/format_cpp.py`.
Probe commands: `python3 "$R/libcxx_probe.py" libcxx23`,
`python3 "$R/check_preservation.py"`. The libc++ probe uses Clang 23.1.3; it does
not resolve the separate Jolt C++17 PCH blocker or qualify native macOS.
Final docs checks are `python3 tools/check_docs.py`, `git diff --check`.
No tracked Python changes. Native sanitizer access preserves default LeakSanitizer
discovery/execution; no sanitizer option is disabled.

## Remaining scope

Evidence is synthetic. No real material solve, source media processing,
model/dependency installation, GPU/native-viewer run or runtime-inventory refresh
ran in this slice. The 378 protected saved-data/product hashes remain unchanged.
Native Windows/macOS reruns and real A1 PBR viewer/Godot validation remain due.
Runtime receipts remain stale after linking. Source/texel physical correspondence,
intrinsic/grid checksum verification, fitted-light authenticity, UV/bake quality,
sRGB texture/export conversion, actor/rights authority and full asset adoption
remain unverified. Existing immutable-store recovery is reused; no separate
crash/disk-full/concurrent-mutation probe was added. Structure/completion/export
contracts and the real capture/recipe freeze remain open. No gate is promoted.
