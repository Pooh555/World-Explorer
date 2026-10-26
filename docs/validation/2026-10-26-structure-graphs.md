# A0 bounded structure graphs — 26 October 2026

**VERIFIED local synthetic CPU admission slice; A0/AP0 remain IN_PROGRESS.** The shared command
admits actual mesh-bound patches/boundaries, declared thickness, planar openings,
room links and computed plane/anchor/AABB residuals. Original independent geometry,
capture and receipt pins remain required. Owned atomic publication/reload supplies
no estimator, physical room closure, observed void, gravity/support/collision or
AP/Q/N qualification. The [protocol](../protocols/structure_graphs.md) owns fields,
budgets and limitations. Existing geometry/material/static encodings are unchanged.

Cwd: repository root; timezone Asia/Hong_Kong. Parent
`7314fb46672b63e9ef3b9f5790bad375bc59e2cb`.
`R=artifacts/validation/a0-structure-graph-20261026` (ignored).
Six-file implementation/fixture identity is in `source-final.json` (CMake and five
C++ paths); sorted compact path→SHA-256 JSON aggregate SHA-256:
`e2f82ff2e8822efd1e3745773d1cdb52385ddc25446b14d0574e00a0d698814a`.
GNU 16.2.1 uses C++26 for dev/ASan and explicit C++23 for compatibility;
format/tidy tools are LLVM 23.1.1.

## Exercised behavior and corrections

An eight-triangle synthetic native wall ring supplies an exact planar opening;
native source anchors and all face-support denominators remain intact. Tests
exercise all six signed dominant axes, an actual nonplanar mesh patch, two-room/
exterior links, unknown confidence/thickness, separate declared thickness sources,
manual ancestry and positive computed plane residual. Split patches exercise
native contact/support anchors, directed cycles and AABB containment failure.
Rehashed missing/reversed/repeated/foreign boundaries, disconnected faces,
incorrect coverage, wrong profile/context/receipt/IDs, evaluation aliases,
understated residuals and unsupported observed void/gravity claims fail. Excess
counts/bytes and duplicate/NUL/deep JSON fail; an exact 256 KiB root publishes and
reloads. Owned values survive source lifetime. Staged cancellation, exact retry,
selected stale commit, conflicting base and source-tree overlap are exercised.

`build-first.log` exits 1 on an ambiguous boolean grouping under `-Werror`;
an explicit straddle predicate repairs it. `build-tests.log` exits 1 because the
fixture supplied `Vec3d` to native float-array vertices; explicit exact fixture
conversions repair it. `build-tests-second.log` passes, followed by
`focused-first.log`: 12 cases/8,620 assertions. `style-second.log` exits 1 on an
unchecked optional in a test; an explicit guard fixes it. The expanded
`style-before-report.log` completes C++ analysis but exits 1 because this new
report's links were temporarily unresolved. No warning/check was suppressed.

## Actual checks

Final-source commands; logs are relative to `$R`:

```sh
cmake --build --preset dev -j 4
cmake --build --preset compat-cxx23 --parallel 4
build/dev/we_core_tests '[structure-graph],[material-product],[geometry-product],[asset-edit],[capture-selection]'
build/compat-cxx23/we_core_tests '[structure-graph],[material-product],[geometry-product],[asset-edit],[capture-selection]'
ctest --preset dev -j 4
ctest --preset compat-cxx23 -j 4
cmake --build --preset asan -j 4 --target we_core_tests
build/asan/we_core_tests '[structure-graph],[material-product],[geometry-product],[asset-edit],[capture-selection]'
```

| Check | Exit / observation | Log |
|---|---|---|
| Dev/compat builds | 0 / both pass | `build-{dev,compat}.log` |
| Dev focused | 0 / 72 cases, 39,119 assertions | `focused-dev.log` |
| Compat focused | 0 / 72 cases, 39,119 assertions | `focused-compat.log` |
| Full dev CTest | 0 / 789 of 789, 208.63 s | `ctest-dev.log` |
| Full compat CTest | 0 / 764 of 764, 214.07 s | `ctest-compat.log` |
| Native ASan/UBSan + default LeakSanitizer | 0 / build and 72 cases, 39,119 assertions; no findings | `build-asan.log`, `focused-asan.log` |
| Format/tidy/whitespace gate | 0 / five C++ files | `style-final.log` |
| Both new test TUs / retained libc++ 23.1.1 | 0 / explicit C++23 syntax only | `libcxx23*.log`, `libcxx23*-argv.json` |
| Protected data rehash | 0 / 378 hashes, zero mismatches | `preservation.json` |
| Documentation / diff whitespace | 0 / 1,567 targets, 296 files; clean diff | `docs.log`, `diff.log` |

Style command: `PATH="$PWD/artifacts/tools/llvm-official-23.1.1/bin:$PATH"`
`python3 tools/check_cpp_style.py` on the five C++ paths in `source-final.json`.
Formatting uses retained `artifacts/validation/m3-frame-quality-20261023/format_cpp.py`.
Libc++ commands: `python3 "$R/libcxx_probe.py" libcxx23` and
`python3 "$R/libcxx_probe.py" libcxx23-orientation tests/unit/structure_orientation_tests.cpp`.
Clang 23.1.3 syntax probes do not resolve the independent Jolt C++17 PCH blocker
or qualify native macOS. Final data/docs commands are
`python3 "$R/check_preservation.py"`, `python3 tools/check_docs.py`, `git diff --check`.
No tracked Python changed. Native sanitizer access preserves default LeakSanitizer
discovery/execution. Automatic review initially timed out before the build ran;
the tool-permitted single retry was approved.

## Remaining scope

No real structure solve, source-media processing, dependency/model installation,
GPU/viewer execution, runtime-inventory refresh or native Windows/macOS rerun ran.
Saved data/products retain all 378 selected protected hashes. Runtime receipts
remain stale after linking. Partial patches and AABB/anchor declarations do not
certify physical containment, room closure, intersection freedom, measured units,
load-bearing support or observed free space. Complete structure production,
producer/asset adoption, completion/export records, real capture/recipe freeze and
A1 native completed/PBR viewing and Godot import remain open. Existing immutable
store recovery is reused; no separate crash/disk-full/concurrent-mutation probe
was added. No asset, platform or AP/Q/N gate is promoted.
