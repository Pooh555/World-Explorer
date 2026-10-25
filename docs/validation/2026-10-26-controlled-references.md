# A0 controlled references — 26 October 2026

**VERIFIED local synthetic CPU slice; A0/AP0 remain IN_PROGRESS.** Typed backside,
empty-background and changed-light prop reference sets bind exact selected
inventory/capture/view/condition declarations and optional native lux/kelvin
observations. Each kind is independent. Explicit corpus schema 5 requires matching
private handles; earlier encodings remain unchanged. The
[protocol](../protocols/controlled_references.md) owns semantics and bounds.
No real removal, backside fidelity, lighting calibration, AP4/AP6 pass or asset
production is qualified.

Cwd: repository root; timezone Asia/Hong_Kong. Parent
`b6ad3005d6cf84359f85c7755a55d3732021b9f1`.
`R=artifacts/validation/a0-controlled-references-20261026` (ignored).
Seven-file implementation/fixture identity: `source-final.json`, aggregate SHA-256
`8bcf67f876be820f4eed624940c78f2ffa267e89b1d3cfbf07bf4d73ce562c58`
(sorted compact JSON of path→SHA-256), covering CMake and all six changed C++ files.
GNU 16.2.1 builds dev/ASan C++26 and explicit compatibility C++23; format/tidy tools
are LLVM 23.1.1.

## Exercised behavior

The synthetic fixture uses independently admitted two-view visibility and actual
native pose bytes, a selected inference baseline, visible prop references and a
removed/out-of-view declaration. Strict schemas, original receipt/context/ref
binding, frozen prop identities, full role/view selection, conditions, quantity
units/ranges, missing/partial lighting, dependency aliases and owned recovery are
checked. Evaluation changes preserve narrow inference pixels. Partial corpus
availability remains explicit; metadata agreement does not establish physical truth.

The first application build and three-kind smoke pass (456 assertions in one
case). The expanded builds stop on a shadowed constant in the lifetime test
(`build-{dev,compat}-first.log`). Rename the inner constant. Static analysis also
refuses out-of-range enum casts and unchecked optional access (`style-first.log`).
Use valid wrong-kind values, retain unknown serialized-enum cases, and directly
guard the optional. Warnings/thresholds are unchanged; corrected checks pass.

## Actual checks

Final-source commands; all logs below are relative to `$R`:

```sh
cmake --build --preset dev -j 4
cmake --build --preset compat-cxx23 --parallel 4
build/dev/we_core_tests '[controlled-references],[evaluation-paths],[evaluation-inventory],[asset-corpus],[capture-selection]'
build/compat-cxx23/we_core_tests '[controlled-references],[evaluation-paths],[evaluation-inventory],[asset-corpus],[capture-selection]'
ctest --preset dev -j 4
ctest --preset compat-cxx23 -j 4
cmake --build --preset asan -j 4 --target we_core_tests
build/asan/we_core_tests '[controlled-references],[evaluation-paths],[evaluation-inventory],[asset-corpus],[capture-selection]'
```

| Check | Exit / observation | Log |
|---|---|---|
| Final-source dev/compat builds | 0 / both pass | `build-{dev,compat}-corrected.log` |
| Final-source dev/compat focused | 0 / each 62 cases, 30,641 assertions | `focused-{dev,compat}-first.log` |
| Full dev CTest | 0 / 702 of 702, 113.02 s | `ctest-dev-final.log` |
| Full compat CTest | 0 / 677 of 677, 114.56 s | `ctest-compat-final.log` |
| Native ASan/UBSan + default LeakSanitizer focused | 0 / build and 62 cases, 30,641 assertions; no findings | `build-asan-final.log`, `focused-asan-final.log` |
| Format/tidy/whitespace gate | 0 / six C++ files | `style-corrected.log` |
| Full new test TU, retained libc++ 23.1.1 / explicit C++23 | 0 / syntax only | `libcxx23-final.log`, `libcxx23-final-argv.json` |
| Protected data rehash | 0 / 378 checked, zero mismatches | `preservation.json` |
| Documentation links / diff whitespace | 0 / 1,478 targets, 282 files; clean diff | `docs-final.log`, `diff-final.log` |

The style command uses `PATH="$PWD/artifacts/tools/llvm-official-23.1.1/bin:$PATH"`
and `python3 tools/check_cpp_style.py` on all six C++ paths in `source-final.json`.
Formatting uses retained ignored `artifacts/validation/m3-frame-quality-20261023/format_cpp.py`.
The libc++ command is `python3 "$R/libcxx_probe.py" libcxx23-final` (Clang 23.1.3,
full argv retained). This does not resolve the separate full-libc++ Jolt C++17 PCH
`std::is_trivial` blocker or qualify native macOS. `python3 "$R/check_preservation.py"`
rehashes the original 230 plus 148 completed product records. No tracked Python
changes. Final docs checks: `python3 tools/check_docs.py`, `git diff --check`.
ASan uses native permission for default LeakSanitizer discovery/execution because
sandbox ptrace cannot run it.

## Remaining qualification

Original media/products are preserved. No model install/download, media processing,
GPU/runtime refresh, upload or native Windows/macOS qualification ran. Existing
runtime receipts remain stale after linking. The host remains contracts-only.
Typed capture-envelope and evaluation-protocol freezes, complete rights/runtime/
resource qualification and worker adoption remain A0 work. A1 still needs consented,
independently scaled real pilot/controlled references and actual completed/PBR
native-viewer and Godot output; metadata admission does not advance those gates.
