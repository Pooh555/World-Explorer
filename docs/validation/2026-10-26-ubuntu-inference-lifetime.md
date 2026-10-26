# Ubuntu GCC 13 inference-handle lifetime repair — 26 October 2026

**Local C++26/C++23 checks; native Ubuntu GCC 13 confirmation pending.** The
[supplied Ubuntu build log](https://github.com/Pooh555/World-Explorer/actions/runs/40276244731/job/121419495462#step:11:244)
fails compiling `evaluation_paths.cpp:185` and `capture_envelope.cpp:237` with
GCC 13, C++23 and `-Werror=dangling-reference`. Both range initializers chain
`capture.inference().frames()` through a temporary inference handle.

`inference()` returns a shared ownership handle; `frames()` references its backing
vector. The admitted capture also retains that backing storage, so the old code
does not lose its only owner at the temporary's destruction. Both readers now
retain an explicit local `const auto K_INFERENCE` for the entire function. The
frame-reference lookup maps and their consumers are consequently inside that
handle's scope. Two fixtures with the same range pattern use the same fix.
The scoped audit finds no other such range initializer. Immediate algorithm calls
and value copies remain unchanged. No public API, payload, behavior, warning,
language standard or acceptance threshold changes; no new test cases are added.

Cwd: repository root; date/timezone: 2026-10-26, Asia/Hong_Kong. Parent
`21f6a1517ad7bfb7aef98084623b1a80aeccbcc7`.
`R=artifacts/validation/ubuntu-inference-lifetime-20261026` (ignored).
Four-file source/fixture identity: `source-final.json`, aggregate SHA-256
`4c2382ae3d38a143cabaa06a63235e089a3317a9d1ef6a5bda3e3aeba0753624`
(sorted compact path→SHA-256 JSON). GNU 16.2.1 builds dev C++26 and explicit
compat C++23; exact format/tidy tools are LLVM 23.1.1.

## Actual checks

```sh
cmake --build --preset dev -j 4
cmake --build --preset compat-cxx23 --parallel 4
build/dev/we_core_tests '[evaluation-paths],[capture-envelope],[evaluation-protocol],[asset-corpus],[capture-selection]'
build/compat-cxx23/we_core_tests '[evaluation-paths],[capture-envelope],[evaluation-protocol],[asset-corpus],[capture-selection]'
ctest --preset dev -j 4
ctest --preset compat-cxx23 -j 4
python3 "$R/gcc15_probe.py"
python3 "$R/check_preservation.py"
python3 tools/check_docs.py
git diff --check
```

| Check | Exit / observation | Log under `$R` |
|---|---|---|
| Dev/compat builds | 0 / both pass | `build-{dev,compat}.log` |
| Focused dev | 0 / 64 cases, 50,992 assertions | `focused-dev.log` |
| Focused compat | 0 / 64 cases, 50,992 assertions | `focused-compat.log` |
| Full dev CTest | 0 / 789 of 789, 144.42 s | `ctest-dev.log` |
| Full compat CTest | 0 / 764 of 764, 158.13 s | `ctest-compat.log` |
| GCC 15.3.0 before/after syntax | 0 / both production TUs in both modes | `gcc15-results.json`, `gcc15-*.log`, `gcc15-*-argv.json` |
| Format/tidy/whitespace gate | 0 / four C++ files | `style.log` |
| Protected data rehash | 0 / 378 hashes, zero mismatches | `preservation.json` |
| Documentation / diff whitespace | 0 / 1,570 targets, 297 files; clean diff | `docs.log`, `diff.log` |

The GCC 15 probe derives exact include/define flags from the compatibility compile
database, compiles original parent and repaired source through stdin with explicit
`-Wdangling-reference -fsyntax-only`, and retains every argv/source/log. All
original warning flags, including `-Werror`, remain active. Both versions pass:
GCC 15 does **not** reproduce the supplied GCC 13 warning. No GCC 13 executable or
retained toolchain was found locally; the repair needs the same Ubuntu CI rerun.
No local build/test failure was observed in this repair slice.

The style command uses `PATH="$PWD/artifacts/tools/llvm-official-23.1.1/bin:$PATH"`
and `python3 tools/check_cpp_style.py` on the four paths in `source-final.json`.
Formatting uses retained `artifacts/validation/m3-frame-quality-20261023/format_cpp.py`.
No tracked Python changed. No new sanitizer, GPU/viewer, real media processing,
model installation, runtime-inventory refresh or remote publishing ran. Existing
runtime receipts remain stale after relinking. This build repair preserves A0's
completion-product handoff; A0/AP0 remain IN_PROGRESS and A1–A6/M4 remain unstarted.
