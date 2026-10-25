# A0 evaluation-protocol freeze — 26 October 2026

**VERIFIED local synthetic CPU slice; A0/AP0 remain IN_PROGRESS.** Typed sampling, closed
plan metric/review profiles, complete observed/completed masks and frozen light/
configuration identities now bind original capture/receipt selections. Corpus
schema 7 requires matching private handles; earlier encodings stay unchanged.
The [protocol](../protocols/evaluation_protocol.md) owns semantics and limits.
No metric runner, actual sampling/render/review, physical correspondence or AP pass
is qualified. Specification thresholds are unchanged.

Cwd: repository root; timezone Asia/Hong_Kong. Parent
`2b0759308c070f0b8566f8e01e931c5843da24e4`.
`R=artifacts/validation/a0-evaluation-protocol-20261026` (ignored).
Seven-file implementation/fixture identity: `source-final.json`, aggregate SHA-256
`49ac2a7f571bc397b90919bc27be97de469f69a7392e7b84710f02e7010db563`
(sorted compact JSON of path→SHA-256), covering CMake and all six changed C++ files.
GNU 16.2.1 builds dev/ASan C++26 and explicit compatibility C++23; format/tidy tools
are LLVM 23.1.1.

## Exercised behavior

Two synthetic sources/five noninference native views have ten full-grid masks.
Strict track/view completeness, partition arithmetic, eligibility classes, fixed
metric conventions, seeds/config refs, distinct light declarations and reviewer
bounds are checked. Completed/unknown eligible surfaces cannot enter observed
masks. Empty masks stay explicit. Rehashed policy weakening, threshold injection,
stale/foreign selections, inference aliases, dependencies and byte/run/pixel/table
bounds fail; evaluation-only changes preserve inference pixels.

Staged cancellation, exact retry/selected reload, source protection, stale pins,
operation conflicts and owned lifetime are checked. Corpus fixtures cover derived
blockers, empty masks, missing/foreign handles and schema 1–6 byte replay. There is
no separate native crash/disk-full/concurrent-mutation probe; storage reuses the
existing immutable registry. The first application build and one-case smoke pass
(244 assertions). Expanded builds, style and both focused runs pass; no failed
build/test/style run is recorded for this slice. Full and sanitizer results follow.

## Actual checks

Final-source commands; all logs below are relative to `$R`:

```sh
cmake --build --preset dev -j 4
cmake --build --preset compat-cxx23 --parallel 4
build/dev/we_core_tests '[evaluation-protocol],[capture-envelope],[controlled-references],[evaluation-paths],[evaluation-inventory],[asset-corpus],[capture-selection]'
build/compat-cxx23/we_core_tests '[evaluation-protocol],[capture-envelope],[controlled-references],[evaluation-paths],[evaluation-inventory],[asset-corpus],[capture-selection]'
ctest --preset dev -j 4
ctest --preset compat-cxx23 -j 4
cmake --build --preset asan -j 4 --target we_core_tests
build/asan/we_core_tests '[evaluation-protocol],[capture-envelope],[controlled-references],[evaluation-paths],[evaluation-inventory],[asset-corpus],[capture-selection]'
```

| Check | Exit / observation | Log |
|---|---|---|
| Final-source dev/compat builds | 0 / both pass | `build-{dev,compat}-first.log` |
| Final-source dev/compat focused | 0 / each 88 cases, 65,377 assertions | `focused-{dev,compat}-final.log` |
| Full dev CTest | 0 / 728 of 728, 133.46 s | `ctest-dev-final.log` |
| Full compat CTest | 0 / 703 of 703, 136.54 s | `ctest-compat-final.log` |
| Native ASan/UBSan + default LeakSanitizer focused | 0 / build and 88 cases, 65,377 assertions; no findings | `build-asan-final.log`, `focused-asan-final.log` |
| Format/tidy/whitespace gate | 0 / six C++ files | `style-first.log` |
| Full new test TU, retained libc++ 23.1.1 / explicit C++23 | 0 / syntax only | `libcxx23-final.log`, `libcxx23-final-argv.json` |
| Protected data rehash | 0 / 378 checked, zero mismatches | `preservation.json` |
| Documentation links / diff whitespace | 0 / 1,502 targets, 286 files; clean diff | `docs-final.log`, `diff-final.log` |

Style uses `PATH="$PWD/artifacts/tools/llvm-official-23.1.1/bin:$PATH"` and
`python3 tools/check_cpp_style.py` on all six C++ paths in `source-final.json`.
Formatting uses retained ignored `artifacts/validation/m3-frame-quality-20261023/format_cpp.py`.
The libc++ command is `python3 "$R/libcxx_probe.py" libcxx23-final` (Clang 23.1.3,
full argv retained). It does not resolve the separate full-libc++ Jolt C++17 PCH
`std::is_trivial` blocker or qualify native macOS. `python3 "$R/check_preservation.py"`
rehashes the original 230 plus 148 completed product records. No tracked Python
changes. Final docs checks: `python3 tools/check_docs.py`, `git diff --check`.
ASan uses native permission for default LeakSanitizer discovery/execution because
sandbox ptrace cannot run it.

## Remaining qualification

Config blobs are pinned opaque identities, not proof of code/backbone installation
or semantics. Masks/light labels and review settings are declarations; physical
correspondence, novelty/blindness, actual metric behavior and pre-tuning freeze stay
unverified. The existing crop evaluator does not implement this masked protocol.
No installs/downloads, media processing, GPU/runtime refresh, uploads or native
Windows/macOS qualification ran. Runtime receipts remain stale after linking.
Protected media/products stay intact. Broader application/worker integration and
A0 rights/runtime/resource readiness remain open. Real A1 completed/PBR native
viewer/Godot output and AP gates do not advance from this metadata slice.
