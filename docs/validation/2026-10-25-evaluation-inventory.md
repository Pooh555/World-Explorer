# A0 typed inventory and visibility — 25 October 2026

**VERIFIED local CPU contract slice; A0/AP0 remain IN_PROGRESS.** Bounded native inventory
and complete item/view visibility records now bind independently selected tracks,
types, required flags and admitted evaluation annotations. Corpus schema 3 requires
matching private handles; schemas 1/2 retain their encodings. The
[protocol](../protocols/evaluation_inventory.md) owns exact limits and semantics.
All fixtures are synthetic. No real inventory, segmentation, observed coverage,
AP gate, asset production or automatic yield is qualified.

Cwd: repository root; timezone Asia/Hong_Kong. Parent
`15ece735ac6dae64e0dd7dfe3a8a27075d309e6e`.
`R=artifacts/validation/a0-evaluation-inventory-20261025` (ignored).
Final nine-file implementation/fixture identity is `source-final.json`, aggregate
SHA-256 `c0fe398dda8b07f4afbfcb5c10a636ce7828ffb94516a10fd64c51191f6de87a`
(sorted compact JSON of path→SHA-256). It includes CMake and all eight changed
C++ headers/sources/fixtures. GNU 16.2.1 builds dev/ASan C++26 and explicit
compatibility C++23; format/tidy tools are LLVM 23.1.1.

## Exercised behavior

Tests bind all five item kinds, repeated labels on distinct tracks, required and
optional items, all visibility states, and one/two actual pose-admitted evaluation
views. Complete Cartesian tables, selected annotation use, single accepted track,
class/mask consistency, strict schemas/byte limits and independently selected
refs are checked. Hash/recipe aliases into selected inference are refused by an
additional evaluation-root separation check. Evaluation-only edits leave narrow
inference pixels unchanged; no new worker input conversion exists.

Cancellation after each of the two staged records leaves no committed metadata;
exact replay and selected committed reload recover. Wrong pins and source/output
overlap fail; owned metadata survives source removal and caller edits. Corpus
tests cover missing/foreign private handles, paired refs, malformed flags, selected
seal reload, explicit missing prerequisites and original schema-1/2 byte replay.
No separate native crash, disk-full or hostile concurrent mutation probe is added;
the existing immutable registry supplies the reused storage behavior.

The first build fails on a test switch case whose local optional crossed later
case labels (`build-first.log`). A scoped case fixes it. The attempted focused run
against the preceding executable finds no new cases (`focused-first.log`, exit 2),
so it is not counted as verification. Corrected runs use the rebuilt executable.
The first style run refuses a missing reservation, indirect optional guard and
adjacent same-type callback parameters (`style-first.log`). Reserve the known
parent count, guard the optional directly and separate the JSON arguments with
the typed selection argument; warnings and thresholds are unchanged.

## Actual checks

All output files below are relative to `$R`. Final-source builds use:

```sh
cmake --build --preset dev -j 4
cmake --build --preset compat-cxx23 --parallel 4
build/dev/we_core_tests '[evaluation-inventory],[asset-corpus],[capture-selection]'
build/compat-cxx23/we_core_tests '[evaluation-inventory],[asset-corpus],[capture-selection]'
ctest --preset dev -j 4
ctest --preset compat-cxx23 -j 4
cmake --build --preset asan -j 4 --target we_core_tests
build/asan/we_core_tests '[evaluation-inventory],[asset-corpus],[capture-selection]'
```

| Check | Exit / observation | Log |
|---|---|---|
| Final dev/compat builds | 0 / both pass | `build-{dev,compat}-verified.log` |
| Final dev/compat focused | 0 / each 35 cases, 10,130 assertions | `focused-{dev,compat}-final.log` |
| Full dev CTest | 0 / 675 of 675, 146.90 s | `ctest-dev-final.log` |
| Full compat CTest | 0 / 650 of 650, 145.54 s | `ctest-compat-final.log` |
| Native ASan/UBSan + default LeakSanitizer focused | 0 / build and 35 cases, 10,130 assertions; no findings | `build-asan-final.log`, `focused-asan-final.log` |
| Format/tidy/whitespace gate | 0 / eight C++ files | `style-final.log` |
| Full new test TU with retained libc++ 23.1.1, explicit C++23 | 0 / syntax only | `libcxx23-final.log`, `libcxx23-final-argv.json` |
| Protected data rehash | 0 / 378 checked, zero mismatches | `preservation.json` |
| Documentation links / diff whitespace | 0 / 1,454 targets, 278 files; clean diff | `docs-final.log`, `diff-final.log` |

The style command uses `PATH="$PWD/artifacts/tools/llvm-official-23.1.1/bin:$PATH"`
and `python3 tools/check_cpp_style.py` on all eight C++ paths in
`source-final.json`; the formatter is the existing ignored
`artifacts/validation/m3-frame-quality-20261023/format_cpp.py`. The libc++ command
is `python3 "$R/libcxx_probe.py" libcxx23-final` (Clang 23.1.3, full argv retained).
This does not repair or bypass the existing Jolt C++17 PCH `std::is_trivial`
blocker in the separate full-libc++ profile and is not native macOS qualification.
`python3 "$R/check_preservation.py"` rehashes the original 230 plus 148 completed
product records without modifying them. ASan uses native permission for default
LeakSanitizer discovery/execution because sandbox ptrace cannot run it.
Final docs checks: `python3 tools/check_docs.py` and `git diff --check`.
No tracked Python source changes.

## Remaining qualification

Frozen declarations do not establish that labels/visibility match real images or
that the physical inventory is complete. Required real pilot scope, source consent,
independent scale and novel-light/background references remain open; the prior
[selected input inventory](2026-10-25-evaluation-corpus.md#available-real-inputs)
is not promoted. Existing protected media/products are checked separately.
No media processing, model installation, implicit download/upload, GPU/runtime
inventory work or native Windows/macOS qualification ran. The host remains
contracts-only. Typed temporal/path and controlled-light/background evidence,
worker adoption, full runtime/rights qualification and real A1 viewer/Godot output
remain required before platform qualification.
