# A0 shared corpus inspection — 26 October 2026

**VERIFIED local synthetic CPU/application slice; A0/AP0 remain IN_PROGRESS.** The shared
request command and `we_process inspect-corpus` load independent original receipts,
capture/annotation/evidence selections and current registry pins, then repeat
existing corpus admission. Output names the declared track and every derived
blocker. Exit 0 means metadata inspection, not readiness, execution or AP success.
The [protocol](../protocols/corpus_inspection.md) owns request fields and bounds.

Cwd: repository root; timezone Asia/Hong_Kong. Parent
`091c206844c3414c0f75a227b34d92f09939486b`.
`R=artifacts/validation/a0-corpus-inspection-20261026` (ignored).
Six-file implementation/fixture identity: `source-final.json`, aggregate SHA-256
`b6699dc36209b400c1f44b32c82bbed65f6d8ce15872feb5e57e99c28946a4a4`
(sorted compact JSON of path→SHA-256), covering CMake and all five changed C++ files.
GNU 16.2.1 uses C++26 for dev/ASan and explicit C++23 for compatibility;
format/tidy tools are LLVM 23.1.1.

## Behavior and corrections

Fixtures reopen annotated captures and each typed family: measurement/scene,
inventory/controlled references, temporal paths/envelopes and protocol masks/
configs/lights. Admission must reproduce original canonical bytes and blockers.
Stale/foreign receipt/context/product/commit pins, omitted handles, malformed
requests and unknown/overflowing typed selections fail. File snapshots check
preservation through success/refusal; CLI tests check synthetic labeling, every
scoped blocker, incomplete-request refusal and usage. Pre-cancellation refuses
before opening input paths. No separate hostile concurrent mutation, crash or
aggregate-memory saturation probe is added; reused contracts keep their limits.

The first CLI-only build passes. The first test build caught a shadowed variable
(`build-tests-first.log`); it was renamed. `focused-first.log` exits 42 (7 failed
cases/22 assertions): the test snapshot helper incorrectly used a nonempty-asset
reader for empty lock files. It now explicitly hashes empty files. The first style
run exits 1: adjacent interchangeable size refs, an analyzer-unproven optional
guard, const return, missing generated-switch defaults and a copied callback
parameter were repaired without suppressions (`style-first.log`). The next focused
run exits 42 (6 cases/29 assertions): fixture construction assigned its JSON member
before construction. Member order was corrected. Failing logs are retained; these
fixture failures do not establish successful product checks.
The added failure diagnostic also needed parentheses around its conditional
expression inside Catch's streaming macro (`build-tests-fourth.log`); corrected.

## Actual checks

Final-source commands; logs below are relative to `$R`:

```sh
cmake --build --preset dev -j 4
cmake --build --preset compat-cxx23 --parallel 4
build/dev/we_core_tests '[corpus-inspection],[evaluation-protocol],[capture-envelope],[controlled-references],[evaluation-paths],[evaluation-inventory],[asset-corpus],[capture-selection]'
build/compat-cxx23/we_core_tests '[corpus-inspection],[evaluation-protocol],[capture-envelope],[controlled-references],[evaluation-paths],[evaluation-inventory],[asset-corpus],[capture-selection]'
ctest --preset dev -j 4
ctest --preset compat-cxx23 -j 4
cmake --build --preset asan -j 4 --target we_core_tests
build/asan/we_core_tests '[corpus-inspection],[evaluation-protocol],[capture-envelope],[controlled-references],[evaluation-paths],[evaluation-inventory],[asset-corpus],[capture-selection]'
```

| Check | Exit / observation | Log |
|---|---|---|
| Final-source dev/compat builds | 0 / both pass | `build-{dev,compat}-final.log` |
| New dev cases | 0 / 11 cases, 4,581 assertions | `focused-third.log` |
| Final-source dev/compat focused | 0 / each 99 cases, 69,958 assertions | `focused-{dev,compat}-final.log` |
| Full dev CTest | 0 / 739 of 739, 169.84 s | `ctest-dev-final.log` |
| Full compat CTest | 0 / 714 of 714, 163.02 s | `ctest-compat-final.log` |
| Native ASan/UBSan + default LeakSanitizer | 0 / build and 99 cases, 69,958 assertions; no findings | `build-asan-final.log`, `focused-asan-final.log` |
| Native CLI subprocess and preservation | 0 / 1 case, 88 assertions | `cli-dev-final.log` |
| Format/tidy/whitespace gate | 0 / five C++ files | `style-final.log` |
| Full new test TU / retained libc++ | 0 / syntax only | `libcxx23-final.log`, `libcxx23-final-argv.json` |
| Protected data rehash | 0 / 378 hashes, zero mismatches | `preservation.json` |
| Documentation links / diff whitespace | 0 / 1,521 targets, 288 files; clean diff | `docs-final.log`, `diff-final.log` |

Style uses `PATH="$PWD/artifacts/tools/llvm-official-23.1.1/bin:$PATH"` and
`python3 tools/check_cpp_style.py` on all five C++ paths in `source-final.json`.
Formatting uses retained ignored `artifacts/validation/m3-frame-quality-20261023/format_cpp.py`.
Probe commands: `python3 "$R/libcxx_probe.py" libcxx23-final`,
`python3 "$R/check_preservation.py"`, `build/dev/we_core_tests '[corpus-cli]' -s`.
Final docs checks: `python3 tools/check_docs.py`, `git diff --check`.
ASan build/discovery and execution use native permission for default LeakSanitizer
process inspection; sandbox ptrace cannot run it. No sanitizer was disabled.

The retained libc++ probe uses Clang 23.1.3 with libc++ 23.1.1 and explicit C++23;
syntax-only success does not resolve the separate full-libc++ Jolt C++17 PCH
`std::is_trivial` blocker or qualify native macOS. No tracked Python changes.

## Existing real sample refusal

`python3 "$R/real_sample_refusal.py"` selects existing
`artifacts/sample_1/source.json` (SHA-256
`099be1925739d457733db828d4b979e3a1ec9bcedf881eb88f2b178090a771be`), binding
`data/local/sample_1.mp4` SHA-256
`51263d9991dcbc2136d0809ecba1a8ba1b261896f3b222b7afba19784a153ef6`.
No independent A0 corpus/capture/recipe pins were supplied for that reconstruction.
The explicit empty-prerequisite request SHA-256 is
`2ceafd40c6c465f9bca1933b64e81ee1b13a67f04d7adb829f9e53806718f2f3`.
The actual native CLI exits **2**, naming independent corpus/seal, capture/commit
and recipe prerequisites; media hash is unchanged. Exact argv and scope are in
`real-sample-scope.json`; output in `real-sample-refusal.log`. This checks refusal
for missing inputs; no real corpus was admitted, and no media was decoded.

## Remaining qualification

The command reads metadata only. It does not qualify physical evidence, source
rights, sampling/metrics, renderer/PBR behavior, runtime closure or resource fit.
No installation/download, reconstruction, GPU/runtime inventory refresh, upload
or native Windows/macOS qualification ran. Relinked runtime receipts remain stale.
A0 still needs its contract/capture/recipe/installation-plan exit audit and exact
candidate rights/size/device gaps resolved or recorded before provisioning.
A1 still needs real consented independently scaled inputs and actual completed/PBR
native-viewer/Godot output. A1–A6/M4 and AP gates do not advance here.
