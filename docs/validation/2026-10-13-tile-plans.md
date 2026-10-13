# Recounted tile rectangle plans — 2026-10-13 Asia/Hong_Kong

Scope: local M1/M2 renderer development, P2–P4/P6/E1; **IN_PROGRESS**. Cached
geometry reduces rejected count attempts while preserving current-frame admission.
No milestone or Q/P/N/E gate is certified. Cwd: repository root. Raw evidence
`R = artifacts/validation/tile-plans-20261013` is ignored/local; use fresh output
paths. Parent `cbbef3f`; implementation is the local commit containing this report.

## Behavior and correctness

The last successfully submitted partition supplies rectangle candidates only.
Each candidate undergoes GPU count/scan and unchanged dispatch, allocation,
reference-cap and shared residency checks before emission. Overflow splits again;
a single tile still rejects without dropping contributors. No counts or GPU
addresses are retained. Failed/incomplete plans preserve the previous complete
cache; resize clears it. Fixed CPU arrays bound cached/accepted regions to 128
and pending regions to 192. Excessive fragmentation completes coverage and discards
the entire cache. [Viewer protocol](../protocols/viewer.md) defines lifecycle and
fresh-plan diagnostic flags.

Four CPU cases check odd-grid exact coverage/disjointness, changed admission
limits, transaction ordering/failure/retry, copied geometry, reset/extent changes,
289-region fallback and maximum uint32 dimensions. Native labeled synthetic tests
in both compiler profiles exercise density replacement, camera movement,
pause/resume, empty, resize, and 2,048-contributor single-tile rejection under a
1,024-reference cap. At 512×512, eight accepted batches use **15→8** counts after
warmup; 32 batches use **63→32**. Changing eight cached regions to the denser scene
recounts/splits with **56** counts and 32 batches. Five fresh/cached capture pairs
are byte-identical per profile; all expected references/contributors remain and
Vulkan validation reports zero errors.

Sparse transitions currently retain fragmentation: replacing 32 broad splats with
one still counts 32 cached regions versus one fresh region. Automatic merging and
native excessive-fragmentation stress remain unqualified. CPU fallback coverage
is checked; no claim of general camera-route performance is made.

## Full retained scene measurements

Unchanged **304,867 splats**, no LOD/trimming, max splats 1,000,000, 512 MiB
budget; FIFO/Wayland on native RTX 4060 Laptop / driver 615.78.08 (**not H_R/H_V**).
Same executable, reuse enabled versus `--fresh-tile-plan`; three repetitions of
uninstrumented and `--timings` runs, each eight warmup plus 24 measured tiny-yaw
frames per phase. No project builds/tests/training ran concurrently. Desktop,
compositor, clocks and thermal state remain uncontrolled; short rates vary.

| Extent | Counts / batches fresh → cached | Median completed render frames/s | Maximum owned-buffer bytes |
|---|---|---:|---:|
| 1280×720 | 1/1 → 1/1 | 59.65 → 67.31 | 228,405,632 |
| 3840×2160 | 11/6 → 6/6 | 32.69 → 34.03 | 489,622,464 |
| 2880×1800 fullscreen | 3/2 → 2/2 | 49.75 → 49.51 | 474,294,528 |

These are completed render rates, not presented FPS or certification. Unchanged
one-count window work also varies substantially; fullscreen has no measured rate
gain. GPU/CPU triples below are p50/p95/p99 over 72 completed instrumented samples.
Instrumentation and overlapping waits are not additive costs.

| Interval ms | Fresh | Cached |
|---|---|---|
| 4K GPU count/scan | 1.505 / 2.492 / 2.527 | 0.973 / 2.000 / 2.063 |
| 4K CPU count wait | 24.067 / 25.822 / 26.404 | 23.050 / 24.364 / 24.539 |
| 4K sum of GPU stages | 22.362 / 23.629 / 24.068 | 22.485 / 23.688 / 23.930 |
| Fullscreen GPU count/scan | 0.434 / 0.460 / 0.461 | 0.334 / 1.364 / 1.385 |

`summary.json` retains all metrics/rates, including window count-wait p99
5.345→25.878 ms and fullscreen tail increases. Restored extents vary between
1428×1746 and 1428×868; retain per-run metrics without pooling unlike workloads.
Full 4K/fullscreen references remain 13,748,984 / 9,369,155. Owned-buffer maxima
are identical; they exclude fixed CPU metadata, query driver memory,
process/transient peaks and total device VRAM. Twelve 4K captures are byte-identical
to each other and preceding evidence.

## Commands, identities and limits

GCC 16.2.1 C++26; Clang 23.1.1 explicit C++23/libstdc++; CMake 4.4.4;
format/tidy 23.1.1. Actual versions: `tools.log`, `device.log`. Final commands exit 0:

| Command | Evidence in R |
|---|---|
| `cmake --preset dev`; `cmake --build --preset dev -j 4`; `cmake --build build/clang-viewer-cxx23 -j 4` | `configure-dev.log`, `build-*-final.log` |
| `ctest --preset dev`; `ctest --test-dir build/clang-viewer-cxx23 --output-on-failure` | `ctest-final-*.log`: **97/97 each** |
| `build/<profile>/we_gpu_tile_plans $R/<new directory>` | `synthetic-provenance-dev.log` / `synthetic-provenance-clang.log`, reports/captures; `run_provenance_checks.py` records both exits |
| `python3 $R/run_gpu_checks.py`; `python3 $R/run_comparisons.py` | Native smoke in both profiles (51 frames each); original/compositing/termination/batches/batches-bounded/real reference captures with `--timings`; pinned gsplat comparisons: **35 pass**, maximum **one level**, real minimum **63.165 dB** |
| `python3 $R/run_trials.py`; `python3 $R/summarize.py` | All twelve trials pass; `trials.log`, per-run JSON/logs, `summary.json/log`; reference parity inventories |
| `clang-tidy -p build/dev` on tile-plan core/renderer, regression and both new tests | `tidy-first.log` has no core/renderer/regression diagnostics; corrected test diagnostics pass in `tidy-final.log`; explicit provenance addition passes with `--warnings-as-errors='*'` in `tidy-provenance.log` |
| `clang-format --dry-run --Werror` on eight changed C++ files; `python3 tools/check_boundaries.py`; `python3 tools/check_docs.py`; `git diff --check` | `format.log`, `boundaries.log`, `docs.log`, `diff-check.log` |

Development checks caught unused/nodiscard results, unsequenced revision access,
aggregate narrowing, unchecked test optional access and swappable test arguments;
fixed without weakening checks. Retained failures: `build-{wait,sequencing,narrowing}-failure.log`,
`tidy-first.log`, `tidy-narrowing.log`. Initial summary rejected unequal restored
extents; corrected aggregation retains those runs individually.

`identity.json` hashes source, binaries, shaders, configuration, comparison and
runner scripts. SHA-256: b96289672b2657802f9ccf6cc656b041d5d65853f8779da54adbd4211c0fb28e.
**329 protected files**, AGENTS, runbook and specification remain unchanged;
`protected-before/after.json`, `identity.log` record verification. The pinned local
reference environment and protected fit/media identities remain as in
[calibrated evidence](2026-10-13-real-reference.md); no downloads/training or
original successful solver/fit reruns. Thirty-five reference captures and six
fixture JSON files match parent evidence byte-for-byte.

Real fresh owned RGB/solver/fit publication, full camera parity, device-loss,
reference performance, native other platforms and complete gates remain pending.
