# A0 capture envelope — 26 October 2026

**VERIFIED local synthetic CPU slice; A0/AP0 remain IN_PROGRESS.** Typed complete source/view
tables bind native calibration/time/world-camera values and independent frame-count/
condition declarations. Explicit corpus schema 6 requires matching private handles;
earlier encodings remain unchanged. The [protocol](../protocols/capture_envelope.md)
owns semantics and limits. This is not real capture/consent/quality or AP qualification.

Cwd: repository root; timezone Asia/Hong_Kong. Parent
`f83c176b7787b3f5487c0edb01aa83ca8c1b7fdf`.
`R=artifacts/validation/a0-capture-envelope-20261026` (ignored).
Nine-file implementation/fixture identity: `source-final.json`, aggregate SHA-256
`d14d9705f932c166434da689d3176616ef592f8132c29830ab97f7c939644c55`
(sorted compact JSON of path→SHA-256), covering CMake and all eight changed C++ files.
GNU 16.2.1 builds dev/ASan C++26 and explicit compatibility C++23; format/tidy tools
are LLVM 23.1.1.

## Exercised behavior

The fixture uses two sources and six selected native poses across all three roles.
Complete source/view tables, exact source hashes/rights, signed/extreme timestamps,
PINHOLE/OPENCV intrinsics/distortion/image transforms and decoded world poses are
checked. Rehashed omissions, duplicates, altered metadata/conditions, stale refs,
role changes, bounds and known inference aliases fail. Unknown frame counts,
unassessed motion/exposure, dynamic scenes and automatic exposure remain explicit;
changed declarations leave narrow inference pixels unchanged.

Atomic staged cancellation, exact retry, selected committed reads, stale pins,
source overlap, conflicting operations and owned lifetime are checked. Corpus
fixtures cover derived blockers, missing/foreign handles and schema 1–5 byte replay.
The shared private camera checker is exercised by both envelope and existing path
fixtures. No separate crash/disk-full/concurrent-mutation probe is added; immutable
storage remains the existing reused contract.

The first application build and one-case smoke pass (168 assertions). Expanded
builds, style and focused checks also pass; no failed build/test/style run is
recorded for this slice. The full/sanitizer results are recorded separately below.

## Actual checks

Final-source commands; all logs below are relative to `$R`:

```sh
cmake --build --preset dev -j 4
cmake --build --preset compat-cxx23 --parallel 4
build/dev/we_core_tests '[capture-envelope],[controlled-references],[evaluation-paths],[evaluation-inventory],[asset-corpus],[capture-selection]'
build/compat-cxx23/we_core_tests '[capture-envelope],[controlled-references],[evaluation-paths],[evaluation-inventory],[asset-corpus],[capture-selection]'
ctest --preset dev -j 4
ctest --preset compat-cxx23 -j 4
cmake --build --preset asan -j 4 --target we_core_tests
build/asan/we_core_tests '[capture-envelope],[controlled-references],[evaluation-paths],[evaluation-inventory],[asset-corpus],[capture-selection]'
```

| Check | Exit / observation | Log |
|---|---|---|
| Final-source dev/compat builds | 0 / both pass | `build-{dev,compat}-first.log` |
| Final-source dev/compat focused | 0 / each 75 cases, 46,589 assertions | `focused-dev-first.log`, `focused-compat-final.log` |
| Full dev CTest | 0 / 715 of 715, 127.84 s | `ctest-dev-final.log` |
| Full compat CTest | 0 / 690 of 690, 127.46 s | `ctest-compat-final.log` |
| Native ASan/UBSan + default LeakSanitizer focused | 0 / build and 75 cases, 46,589 assertions; no findings | `build-asan-final.log`, `focused-asan-final.log` |
| Format/tidy/whitespace gate | 0 / eight C++ files | `style-first.log` |
| Full new test TU, retained libc++ 23.1.1 / explicit C++23 | 0 / syntax only | `libcxx23-final.log`, `libcxx23-final-argv.json` |
| Protected data rehash | 0 / 378 checked, zero mismatches | `preservation.json` |
| Documentation links / diff whitespace | 0 / 1,490 targets, 284 files; clean diff | `docs-final.log`, `diff-final.log` |

Style uses `PATH="$PWD/artifacts/tools/llvm-official-23.1.1/bin:$PATH"` and
`python3 tools/check_cpp_style.py` on all eight C++ paths in `source-final.json`.
Formatting uses retained ignored `artifacts/validation/m3-frame-quality-20261023/format_cpp.py`.
The libc++ command is `python3 "$R/libcxx_probe.py" libcxx23-final` (Clang 23.1.3,
full argv retained). It does not resolve the separate full-libc++ Jolt C++17 PCH
`std::is_trivial` blocker or qualify native macOS. `python3 "$R/check_preservation.py"`
rehashes the original 230 plus 148 completed product records. No tracked Python
changes. Final docs checks: `python3 tools/check_docs.py`, `git diff --check`.
ASan uses native permission for default LeakSanitizer discovery/execution because
sandbox ptrace cannot run it.

## Remaining qualification

Conditions/counts do not prove a complete real video, static scene, locked exposure,
consent or physical calibration. Existing runtime receipts remain stale after linking.
No model install/download, media processing, GPU/runtime refresh, upload or native
Windows/macOS qualification ran. Protected media/products remain intact. Typed
evaluation-protocol freeze and broader worker/rights/runtime/resource qualification
remain A0 work. A1 still needs real consented independently scaled pilot inputs and
actual completed/PBR native-viewer and Godot output; A1–A6/M4 do not advance here.
