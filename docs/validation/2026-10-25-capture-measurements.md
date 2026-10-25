# A0 typed capture measurement evidence — 25 October 2026

**VERIFIED: local CPU contract slice; A0/AP0 remain IN_PROGRESS.** Receipt-backed measurement
records now bind a selected inference view/calibration, full capture source and
independently selected native scene to the existing world-frame anchor reader.
Explicit corpus schema 2 requires private typed scale evidence; schema 1 retains
its opaque behavior and exact encoding. The [measurement protocol](../protocols/capture_measurements.md)
and [corpus protocol](../protocols/evaluation_corpus.md) own the contracts/limits.
All new inputs are synthetic, including fixtures exercising the independent-input
profile. No physical scale, observed correspondence, AP gate or walking is certified.

Cwd: repository root; timezone Asia/Hong_Kong. Parent
`e71e869bb589a0dcd0a241cfef43bbedf77f5274`.
`R=artifacts/validation/a0-capture-measurement-20261025` (ignored).
`implementation.json` pins seven build/source/fixture/test inputs, SHA-256
`5b009eaf6eacf90c999a6ddceab641f27dd402ba24ffe65a7c730df063b5ef45`.
The full suites use `full-suite-implementation.json`, SHA-256
`f79e4b5d17f4de03d9dfd70b2903ea86e204d4f21131f5a9640a78c643d126af`;
only the alignment string declaration subsequently changes to required direct-list
initialization. Final rebuild/focused/sanitizer/style checks cover that correction.
No dependency, standard, benchmark scope or numerical threshold changed.

## Exercised behavior

The reader binds exact capture/receipt/context/view, source hash, scene identity,
native geometry hash and alignment ref. Known evaluation ancestry is refused via
the existing role firewall. All mesh source-frame declarations must belong to the
chosen inference capture/pose group; observed metadata must bind that pose hash.
Geometry metadata strings are bounded before calling the legacy native validator,
which copies some strings while checking them. Finite geometry and actual anchors
are checked using existing native contracts. No source scene/map is changed and
scale remains ARBITRARY. Private copies retain the admitted declarations after
caller aliases or source trees disappear.

Fixtures cover rehashed schema/source/calibration/pose/ancestry substitutions,
scene/geometry changes, invalid anchors, distances/uncertainties/up references,
decoded NUL and oversized records, duplicate raw keys, raw NUL and nesting,
exact/over byte ceilings, stale/evaluation/foreign-pose geometry and bounded native
metadata/counts. Two-record publication is cancelled after each staged blob, then
replayed unchanged. Fresh typed reads require an independent committed registry
pin and repeat source admission; pending, stale and changed sources are refused.
No separate native crash probe, disk-full or hostile concurrent mutation test is
added; the existing immutable registry's narrower recovery evidence is reused.

Corpus fixtures require matching private handles, refuse missing/substituted
evidence and malformed schema-2 flags, exercise selected seal reload, and reproduce
the original schema-1 bytes. Typed scale replaces an opaque structural blocker
with explicit measurement-authenticity and geometry-correspondence blockers;
synthetic measurement additionally remains labeled diagnostic. No corpus is ready
for real qualification by declaration alone.

## Actual checks

| Command | Exit / result; logs under R |
|---|---|
| `cmake --build --preset dev -j 4`; `ctest --preset dev -j 4` | 0; GNU 16.2.1 C++26 native viewer/CLI/core; **663/663**, 96.11 s; `build-dev-final.log`, final fixture rebuild `build-dev-checked.log`, `ctest-dev.log` |
| `cmake --build --preset compat-cxx23 --parallel 4`; `ctest --preset compat-cxx23 -j 4` | 0; explicit GNU C++23 CPU profile; **638/638**, 93.60 s; `build-compat-final.log`, `build-compat-checked.log`, `ctest-compat.log` |
| `build/dev/we_core_tests '[capture-measurement],[asset-corpus],[world-frame],[capture-selection]'`; same under `build/compat-cxx23/` | 0; **44 cases / 9,956 assertions each**; `focused-dev-checked.log`, `focused-compat-checked.log` |
| `cmake --build --preset asan -j 4 --target we_core_tests`; native `build/asan/we_core_tests` with the same filter | 0; **44/9,956**, GNU C++26 ASan/UBSan/default LSan, no reports; `build-asan-checked.log`, `asan-checked.log` |
| LLVM 23.1.1 `python3 tools/check_cpp_style.py include/world_explorer/application/capture_measurement.hpp src/application/capture_measurement.cpp include/world_explorer/application/evaluation_corpus.hpp src/application/evaluation_corpus.cpp tests/fixtures/capture_measurement_fixture.hpp tests/unit/capture_measurement_tests.cpp` | 0; format/tidy, include boundaries and docs/whitespace; `style-checked.log`, final `style-list.log` |
| Repeat all three build commands and focused filters after the final direct-list correction | 0; **44/9,956 each**, sanitizers report no errors; `build-{dev,compat,asan}-list.log`, `focused-{dev,compat}-list.log`, `asan-list.log` |
| `python3 R/libcxx_probe.py libcxx-checked` (expand R) | 0; actual new test translation unit, Clang 23.1.3 with retained libc++23.1.1 headers, explicit C++23 syntax check; `libcxx-checked-argv.json`, `libcxx-checked.log`, `libcxx-checked-summary.log` |
| `python3 R/check_preservation.py` | 0; **378 protected media/product hashes unchanged**; `preservation.json`, `preservation.log` |
| `python3 tools/check_docs.py`; `git diff --check` | 0; `docs.log`, `whitespace.log` |

The first style run rejects six unchecked optional accesses in negative-test
fixtures (`style-first.log`). Checked mesh/observed accessors fix them without
warning suppression; final runs use those sources. Earlier focused passes remain
retained as intermediate evidence. The first native sanitizer build permission
review timed out before starting the command; the allowed single retry succeeded.
The earlier local full-libc++ Jolt blocker and native macOS/Windows reruns remain
pending. This syntax probe is not a native platform build pass.

## Real inputs and remaining limits

The previous [selected pilot inventory](2026-10-25-evaluation-corpus.md#available-real-inputs)
still supplies no consent/independent-measurement/background/changed-light bindings
for the retained arbitrary-scale samples. No media inference, installation,
implicit download/upload, GPU activity, runtime inventory refresh or native
Windows/macOS qualification was performed. Protected media/products are checked
against the existing preservation baseline. The available host stays contracts-only.

Native scene admission checks actual geometry and declared observed metadata; it
does not re-decode an original fusion binary or authenticate source correspondence
or a physical measuring act. Installation/runtime/rights/worker-limit qualification,
typed inventory/visibility and other evaluation evidence remain open. A1 still
needs its own consented independently scaled pilot and actual completed/PBR native
viewer/Godot output. M4 remains NOT_STARTED.
