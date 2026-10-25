# A0 frozen temporal blocks and inspection paths — 25 October 2026

**VERIFIED local synthetic CPU slice; A0/AP0 remain IN_PROGRESS.** Typed complete selected
frame partitions and a separate-source held-out path bind exact decoded camera
values and declared pose uncertainty. Explicit corpus schema 4 requires matching
private handles; schemas 1–3 retain their encodings. The
[protocol](../protocols/evaluation_paths.md) owns limits and semantics. No real
capture, pose accuracy, AP pass, worker execution or product output is qualified.

Cwd: repository root; timezone Asia/Hong_Kong. Parent
`91c15b416d228ade1a084c807e23e3b0313539a6`.
`R=artifacts/validation/a0-evaluation-paths-20261025` (ignored).
Seven-file implementation/fixture identity: `source-final.json`, aggregate SHA-256
`851b2a723d3c4a079b29c9e5d5ff00c56501bf0c3a93656b88b21840cc9ce8e2`
(sorted compact JSON of path→SHA-256). This covers CMake and all six changed C++
headers/sources/fixtures. GNU 16.2.1 builds dev/ASan C++26 and explicit compatibility
C++23; format/tidy tools are LLVM 23.1.1.

## Exercised behavior

The synthetic native pose fixture selects training, contiguous temporal holdouts,
an independent two-view inspection source and an evaluation-only frame. Rehashed
schema/context/camera/time/transform mutations, complete-partition omissions,
reused frames/IDs, same-role splits, wrong roles, native timestamp regressions,
negative/extreme int64 PTS, time-base changes, ordering, source-hash aliases,
inspection correspondence and dependency aliases are checked. Native capture
admission retains inference pose-fit exclusion. Declared uncertainty does not
change inference pixels or become measured covariance.

Atomic two-record publication, cancellation at either staging point, exact replay,
selected committed reload, stale pins, source overlap, operation conflict, pre-cancel
and lifetime ownership are checked. Corpus fixtures cover flags, paired refs,
missing/foreign handles, selected seal reload and earlier-schema byte replay. No separate
native crash/disk-full/concurrent-mutation probe is added; immutable storage remains
the existing reused contract.

The first application build and first test build fail on a renamed reload field
(`build-application.log`, `build-first.log`). Correcting `m_paths` to the selected
`m_temporal` ref fixes compilation (`build-corrected.log`). The one-case smoke run
passes 111 assertions (`focused-smoke.log`); final verification uses rebuilt expanded
tests, rather than treating this early smoke as full coverage.

## Actual checks

All logs below are relative to `$R`. Final-source commands:

```sh
cmake --build --preset dev -j 4
cmake --build --preset compat-cxx23 --parallel 4
build/dev/we_core_tests '[evaluation-paths],[evaluation-inventory],[asset-corpus],[capture-selection]'
build/compat-cxx23/we_core_tests '[evaluation-paths],[evaluation-inventory],[asset-corpus],[capture-selection]'
ctest --preset dev -j 4
ctest --preset compat-cxx23 -j 4
cmake --build --preset asan -j 4 --target we_core_tests
build/asan/we_core_tests '[evaluation-paths],[evaluation-inventory],[asset-corpus],[capture-selection]'
```

| Check | Exit / observation | Log |
|---|---|---|
| Final dev/compat builds | 0 / both pass | `build-{dev,compat}-final.log` |
| Final dev/compat focused | 0 / each 49 cases, 18,481 assertions | `focused-{dev,compat}-final.log` |
| Full dev CTest | 0 / 689 of 689, 179.75 s | `ctest-dev-final.log` |
| Full compat CTest | 0 / 664 of 664, 182.64 s | `ctest-compat-final.log` |
| Native ASan/UBSan + default LeakSanitizer focused | 0 / build and 49 cases, 18,481 assertions; no findings | `build-asan-final.log`, `focused-asan-final.log` |
| Format/tidy/whitespace gate | 0 / six C++ files | `style-final.log` |
| Full new test TU, retained libc++ 23.1.1 / explicit C++23 | 0 / syntax only | `libcxx23-final.log`, `libcxx23-final-argv.json` |
| Protected data rehash | 0 / 378 checked, zero mismatches | `preservation.json` |
| Documentation links / diff whitespace | 0 / 1,466 targets, 280 files; clean diff | `docs-final.log`, `diff-final.log` |

The style command uses `PATH="$PWD/artifacts/tools/llvm-official-23.1.1/bin:$PATH"`
and `python3 tools/check_cpp_style.py` on all six C++ paths in `source-final.json`.
Formatting uses the retained ignored
`artifacts/validation/m3-frame-quality-20261023/format_cpp.py`. The libc++ probe
is `python3 "$R/libcxx_probe.py" libcxx23-final` (Clang 23.1.3, full argv retained).
It does not resolve the separate full-libc++ Jolt C++17 PCH `std::is_trivial`
blocker or qualify native macOS. `python3 "$R/check_preservation.py"` rehashes the
original 230 plus 148 completed product records without modifying them. No tracked
Python source changes. Final docs checks: `python3 tools/check_docs.py` and
`git diff --check`.

Automatic permission review times out on the first native ASan build request;
the explicitly allowed single retry is accepted. Native permission preserves
default LeakSanitizer discovery/execution; sandbox ptrace cannot run it.

## Remaining qualification

Temporal blocks are contiguous only at selected-frame resolution. Source hashes
cannot prove physical independence or honest provenance; declarations cannot prove
pre-tuning freeze, pose accuracy, sampling novelty or absent undisclosed fitting.
A0 needs typed controlled-light/background evidence and broader worker/runtime/
rights/resource qualification. Real consented independently scaled pilot inputs
and actual completed/PBR native viewer and Godot outputs remain required for A1.
No model installation/download, reconstruction, GPU/runtime refresh, upload or
native Windows/macOS qualification ran. Existing runtime receipts remain stale
after linking; no current runtime-closure pass is claimed.
