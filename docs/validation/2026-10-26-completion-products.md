# A0 bounded completion products — 26 October 2026

**VERIFIED local synthetic CPU admission slice; A0/AP0 remain IN_PROGRESS.** The shared
command compares candidate geometry with original observed faces, accounts for
every output face, pins method inputs and retains independently ranked/rejected
alternatives with recomputed consistency. The
[protocol](../protocols/completion_products.md) owns fields/budgets/limitations.
This runs no completion algorithm or real solve and qualifies no hidden geometry,
rights, producer execution, asset adoption, completed environment or AP/Q/N gate.
Existing geometry/material/structure/static schemas and the Ubuntu repair remain
unchanged; Ubuntu GCC 13 CI confirmation is still pending.

Cwd: repository root; timezone Asia/Hong_Kong. Parent
`21d10cae6cee1f7fc5a0d90085307a3ed80d8cf9`.
`R=artifacts/validation/a0-completion-product-20261026` (ignored).
Five-file implementation/fixture identity: `source-final.json`, aggregate SHA-256
`f63db611bb2891f83877cd9c0734a74aa0759f4a72a88a4a14fe27ee2dddd21e`
(sorted compact path→SHA-256 JSON; CMake and four C++ paths).
GNU 16.2.1 uses C++26 for dev/ASan and explicit C++23 for compatibility;
exact format/tidy tools are LLVM 23.1.1.

## Exercised behavior and corrections

An authored synthetic wall-ring fixture supplies eight observed source triangles;
two additional candidate triangles fill its opening with explicit inferred,
generated, unknown or edited support. No algorithm generated this diagnostic.
Tests compare actual positions/normals/groups/native anchors/view sets/uncertainty,
including cyclic corner mapping. Previously inferred faces cannot become selected
observed output. A selected candidate must pass; truthfully failed rejected
alternatives remain inspectable. Complete protected/region accounting, actual
bounds and independent extent limits reject misleading declarations.

Object/shell geometry subjects, all three method profiles, seeds, prompt text,
opaque library/rights inputs, ranks/reasons, dependency closure and manual ancestry
are exercised. Geometry subjects do not establish asset-definition identity or
adoption. Corrupt/foreign/missing/stale handles, rehashed schema/profile/payload
changes, evaluation aliases, invalid ranges and duplicate/NUL/deep JSON fail.
Eight candidates and exact 1 MiB root/library and 16 KiB configuration boundaries
pass. The aggregate test publishes six distinct 1 MiB libraries plus a 1 MiB root;
the eight-library case refuses before publication at the 8 MiB owned-byte cap.
Cancellation after 1/3/6 staged records reveals no committed partial batch; exact
retry, current reload, missing method bytes, stale commits and protected roots
are checked. Owned data survives caller/source changes.

`build-first.log` passes. `build-tests.log` exits 1 because a fixture's designated
aggregate initializer omitted two function fields under `-Werror`; explicit empty
initializers repair it (`build-tests-second.log`). `focused-first.log` then passes
17 cases/15,812 assertions, and `style-first.log` passes. The aggregate-byte test
adds one case before final-source verification. No warning or gate was weakened.

## Actual checks

Logs are relative to `$R`:

```sh
cmake --build --preset dev -j 4
cmake --build --preset compat-cxx23 --parallel 4
build/dev/we_core_tests '[completion-product],[structure-graph],[geometry-product],[material-product],[asset-edit],[capture-selection]'
build/compat-cxx23/we_core_tests '[completion-product],[structure-graph],[geometry-product],[material-product],[asset-edit],[capture-selection]'
ctest --preset dev -j 4
ctest --preset compat-cxx23 -j 4
cmake --build --preset asan -j 4 --target we_core_tests
build/asan/we_core_tests '[completion-product],[structure-graph],[geometry-product],[material-product],[asset-edit],[capture-selection]'
python3 "$R/libcxx_probe.py" libcxx23
python3 "$R/check_preservation.py"
python3 tools/check_docs.py
git diff --check
```

| Check | Exit / observation | Log |
|---|---|---|
| Dev/compat builds | 0 / both pass | `build-{dev,compat}.log` |
| Dev focused | 0 / 90 cases, 55,360 assertions | `focused-dev.log` |
| Compat focused | 0 / 90 cases, 55,360 assertions | `focused-compat.log` |
| Full dev CTest | 0 / 807 of 807, 163.03 s | `ctest-dev.log` |
| Full compat CTest | 0 / 782 of 782, 158.84 s | `ctest-compat.log` |
| Native ASan/UBSan + default LeakSanitizer | 0 / build and 90 cases, 55,360 assertions; no findings | `build-asan.log`, `focused-asan.log` |
| Format/tidy/whitespace gate | 0 / four C++ files | `style-final.log` |
| New test TU / retained libc++ 23.1.1 | 0 / explicit C++23 syntax only | `libcxx23.log`, `libcxx23-argv.json` |
| Protected data rehash | 0 / 378 hashes, zero mismatches | `preservation.json` |
| Documentation / diff whitespace | 0 / 1,577 targets, 299 files; clean diff | `docs.log`, `diff.log` |

Style uses `PATH="$PWD/artifacts/tools/llvm-official-23.1.1/bin:$PATH"` and
`python3 tools/check_cpp_style.py` on the four C++ paths in `source-final.json`.
Formatting uses retained `artifacts/validation/m3-frame-quality-20261023/format_cpp.py`.
Clang 23.1.3/libc++ syntax does not resolve the independent Jolt C++17 PCH blocker
or qualify native macOS. No tracked Python changed. Native sanitizer permission
preserves default LeakSanitizer discovery/execution. Automatic approval review
initially timed out before the build ran; the tool-permitted single retry was approved.

## Remaining scope

No real media processing, model/dependency installation, GPU/viewer execution,
runtime-inventory refresh, native Ubuntu GCC 13/Windows/macOS rerun or remote
publication ran. All 378 selected protected saved-data/product hashes match.
Runtime receipts remain stale after linking. Physical source support, observed
remeshing/subdivision protection, asset-definition adoption, actual library/
algorithm correspondence, execution/rights review, perceptual completion quality,
full export contracts and the real A1 completed/PBR viewer/Godot pilot remain open.
Existing immutable-store recovery is reused; no separate crash/disk-full/concurrent
mutation probe was added. No asset or platform milestone is promoted.
