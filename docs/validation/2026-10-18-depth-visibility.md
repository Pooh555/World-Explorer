# Pose-admitted depth visibility — 2026-10-18 Asia/Hong_Kong

Scope/status: **M3 IN_PROGRESS**. Native checked depth/RGB export and its reader
work on the retained runs; **27 assets** preserve complete planes exactly.
No solve, ranking change, fusion, measured scale, mesh, Q5/N2 or walking claim.
Parent **11d7937**; implementation is the commit containing this report. Cwd:
repository root. Ignored evidence **R**: `artifacts/validation/m3-depth-visibility-20261018`.

## Implemented behavior and real inputs

The [visibility contract](../protocols/depth.md#pose-admitted-visibility-export)
owns request/receipt schema, command, bounds, paths and failure semantics.
`we_process export-depth-visibility REQUEST SHA NEW_DIRECTORY` independently
loads checked resized inputs and admits every selected `.wedepth` against
canonical poses at default limits. It publishes copied immutable dependencies,
RGB and bit-identical depth assets, then the terminal receipt. This shares the
application API; no backend imports or solver code enter the native application.

The consumer `admitted_visibility.py` checks pinned bytes/domain metadata, all
planes, decoded RGB and wxyz optical transforms, preserving validity, actual
support, recipe-relative confidence and optional uncertainty separately. It
requires unit camera-to-world scale for its current analysis transform; world
units remain arbitrary. The receipt is an observed native admission result,
not a signature or quality certificate. Private photometric maps have no fallback
path into this reader. Native PNG checking pins bytes/headers; the consumer also
checks complete RGB8 decoding.

| Exported input | Views | Native receipt SHA-256 |
|---|---:|---|
| Retained temporal recipe | 9 | `c4b4ca96fe5668a012ac0477e3fffbeeb0a19559be73de2a4f1101b81d8a6cb0` |
| Retained experimental parallax recipe | 18 | `040064ada4d454820ec060c985974b9823dd62e6d83c7abacd54a9b23eace30d` |

Each receipt copies the exact resized manifest, pose asset and native/depth
recipes, and binds their original selected hashes. Both use native manifest
`495084082f7e1f5e272c700aa836d1143f2352320a1e85d8b8221fd7276e48f8`;
the different depth recipes remain distinct. Request hashes/actual argv live in
`export-commands.json`; complete immutable file inventories live in each exported
`manifest.json`. Final C++26 temporal and C++23 parallax exports reproduce their
corresponding initial native receipts byte-for-byte. No source/view was discarded
and no backend option/filter changed.

## Fixed-pair analysis: preserve missing coverage

`analysis-policy.json` freezes the same existing **42 temporal camera pairs**
and **128×96 centers/pair** before analysis. This is fixed diagnostic replay,
not another ranking experiment or independent held-out validation. RGB/grid/
intrinsics/frame/pose identities of the first nine views match. An independent
observer compares original/exported bytes and manually extracts all four planes
for all **27 assets**, with exact equality. The real assets have no sigma plane;
synthetic tests separately check optional sigma and valid zero-weight samples.

| On identical 516,096 pair/reference samples | Temporal depth | Parallax depth |
|---|---:|---:|
| Unknown reference depth | 474,641 | 496,003 |
| Known source depth with positive returned Z | 21,513 | 12,291 |
| First failure: round trip exceeds one pixel | 2,293 | 230 |
| Fixed-nine full-plane valid observations | 1,537,092 | 790,453 |

Conditional return agreement cannot conceal unknown coverage. First-failure
counts are ordered accounting, not causal diagnoses. Camera-pair observations
repeat reference positions; they are not unique scene points or ground truth.
All 18 parallax views contain 2,806,753 valid observations, which is **not**
compared with nine views to claim improvement. Full per-pair/class/tile/joint
denominators remain in `analysis.json`; no source ranking uses the result.
Both caches remain partial observed input, with geometry quality unqualified.

## Actual checks

Linux x86-64; GCC **16.2.1**, native C++26 and explicit C++23; prescribed
format/tidy **23.1.1** from existing `artifacts/tools/llvm-23.1.1` (`T`). Existing
isolated Python **3.12.15**, NumPy **2.2.6**, Pillow **11.3.0**; no installs/downloads.
CPU times below are development observations with concurrent checks, not P1 or
memory/performance qualification. No GPU/native viewer/platform run occurred.

| Actual command (repository root) | Result / logs under R |
|---|---|
| `cmake --build --preset dev -j 4`; `ctest --preset dev --output-on-failure -j 4` | **311/311**, **71.48 s**, `build-dev-complete.log`, `ctest-dev.log` |
| `cmake --build --preset compat-cxx23 -j 4`; `ctest --preset compat-cxx23 --output-on-failure -j 4` | **288/288**, **64.40 s**, `build-cxx23.log`, `ctest-cxx23.log` |
| `build/dev/we_core_tests '[visibility]'` | Three synthetic native cases / **72 assertions**, `native-unit.log`: exact dependency/plane copy, all-invalid/optional sigma, zero-weight validity, ten mutation/inventory/overlap variants and cancellation |
| `python3 artifacts/validation/m3-depth-visibility-20261018/export_real.py` | Nine/18-view native export exits 0 in **11.796 / 21.277 s**; exact argv in `export-commands.json`, per-cache logs |
| `python3 artifacts/validation/m3-depth-visibility-20261018/check_native.py` | Final C++26/C++23 receipt parity; seven native rejects and caller-byte preservation, exact argv/exits in `native-check-commands.json`. SIGINT exit **130**, **0.0073 s** signal-to-reap, no forced kill/terminal manifest, `native-cancellation.json` |
| `.venv-dense/bin/python artifacts/validation/m3-depth-visibility-20261018/analyze.py` | Exit 0; complete-plane/copy parity and fixed-pair analysis above, `analysis.log`, `analysis.json` |
| `.venv-dense/bin/python -m unittest discover -s tests/integration -p 'worker_dense*_test.py' -v` | **47/47**, no skips, **51.310 s**, `dense-tests.log`; existing source diagnostics remain unchanged |
| `.venv-dense/bin/python -m unittest discover -s tests/integration -p 'worker_admitted_visibility_test.py' -v` | Final **5/5**, no skips, **0.154 s**, `reader-tests-complete.log`: synthetic plane/pose layout, wxyz inversion, changed/rehashed malformed bytes/domain/counts, cancellation and symlinks. Pose-file placeholder fixtures are consumer-integrity tests, not native admission evidence |
| `ctest --preset dev -R worker_integration --output-on-failure` | Final portable test guard/quaternion case rechecked, **1/1**, **15.94 s**, `worker-final.log` |
| `cmake --build --preset asan -j 4 --target we_core_tests`; `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/asan/we_core_tests '[visibility]'` | Native execution: **3 cases / 72 assertions**, no reported ASan/UBSan/LSan errors; `build-asan-native.log`, `asan-native.log` |
| `LD_LIBRARY_PATH="$PWD/$T/usr/lib" PATH="$PWD/$T/usr/bin:$PATH" python3 tools/check_cpp_style.py src/application/depth_visibility.cpp include/world_explorer/application/depth_visibility.hpp apps/we_process/main.cpp tests/unit/dense_input_tests.cpp` | Pinned **23.1.1** format/tidy, warnings-as-errors, boundaries/docs/diff checks pass for four changed files, `style-final.log`; no warnings weakened |
| `.venv-dense/bin/python tools/audit_worker_environment.py --requirements workers/dense/environment.txt --output artifacts/validation/m3-depth-visibility-20261018/environment.json` | Exit 0; six packages / 1,188 listed files match exact requirements, `environment.log`; rights remain open |
| `python3 tools/check_dependency_manifest.py --local-evidence` | Existing current-byte failure remains exit **2**, `Changed evidence: cmake/dependencies.cmake`, `dependency-current.log`; relinked loader/mapping/owner refresh remains due |

Initial compilation failed on a Catch2-decomposed string/JSON comparison; explicit
string extraction fixes the assertion (`build-dev.log`, successful retry logs).
The first style check rejected the digit-separated size literal in its lexical
check; spelling the identical 48-million bound as `48000000uz` passes. Final
formatting uses prescribed 23.1.1 rather than installed 23.1.3. Initial ASan
Catch2 discovery failed because LeakSanitizer cannot operate under the sandbox's
ptrace; the native rerun above retains leak checking. These failures remain in
`style-initial.log` and `build-asan.log`; no parser/threshold expectation changed.

Publication errors/cancellation can leave owned files without a terminal receipt;
these are not consumed or resumed. Seven real probes reject bad request/depth
hashes, a rehashed wrong-frame asset, occupied/overlapping/dangling outputs and
unknown request fields. Missing geometry stays unknown; no hole filling or
implicit confidence-to-uncertainty conversion occurs.

`python3 tools/check_docs.py` passes **806 local targets / 169 files**;
`git diff --check` passes. `python3 artifacts/validation/m3-depth-visibility-20261018/audit.py`
checks **23,061 protected files**, zero changed/missing, source/binary/observer
hashes and the unchanged GPU worker bundle (`audit.json`). Saved jobs, media,
failed stages, frozen fixtures and earlier caches remain unchanged; no refit,
pose retry, long-stress rerun, push or upload. [Progress](../progress.md#next-executable-step-and-protection-rules)
owns bounded observed-depth fusion next, including its component/recipe/resource
freeze and known-surface/unknown-boundary checks. Full reconstruction, compressed
storage, measured scale, collision/walking, mesh export and Q/P/N/E gates remain open.
