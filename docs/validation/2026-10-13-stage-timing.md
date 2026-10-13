# Completed stage timings and tile radix width — 2026-10-13 Asia/Hong_Kong

Scope: local M1/M2 renderer development, P2–P4/P6/E1; **IN_PROGRESS**. Optional
bounded timing identifies redundant tile-key sorting passes. Skipping unused high
bits improves short local full-scene throughput while preserving every tested
pixel and contributor. No milestone or Q/P/N/E gate is certified. Cwd: repository
root. Raw evidence root `R`: `artifacts/validation/count-waits-20261013` (ignored,
local). Set `R` to that path for commands below; output paths must be fresh.

## Implementation and measurement

Opt-in timing uses twelve queries per frame slot, **24 total**, independent of
reference partition count. Selected queue reports **64 valid bits, 1 ns period**.
Existing count/frame/idle completion gates query reads; no `WAIT_BIT` or extra
completion waits are added. CPU frame/count/acquire/present waits and six GPU
stages publish only with completed render-frame IDs and extents. The harness
retains all 24 measured samples per phase after eight warmup frames. Pause/empty
frames clear unused stages; failed draws publish no sample. Unsupported timestamp
hardware keeps CPU samples and null GPU results; that native fallback remains
unqualified. [Protocol and primary API references](../protocols/viewer.md) define
stage boundaries, query lifetime, counter wrapping and instrumentation effects.

Tile indexing includes emission, reference sorting and ranges. Before profiling,
it sorts all 32 tile-key bits with eight stable four-bit passes. Tested resolutions
need at most 16 bits. Passes now derive from the validated maximum global tile ID
and round up to even pairs, preserving output-buffer ownership and stable tie
order; zero-only keys need no sort. Float depth keys still use eight passes.
No shader, scene count, reference limit or residency budget changes.

Full **304,867 splats**, no LOD/trimming, max splats 1,000,000, 512 MiB budget,
FIFO/Wayland, native RTX 4060 Laptop / driver 615.78.08 (**not H_R/H_V**).
Three paired baseline/profile repetitions before and after; each phase uses eight
warmup and 24 measured tiny-yaw frames, with final GPU completion awaited.
Quiet repetitions run without project builds/tests/training concurrently; desktop,
compositor, GPU clocks and thermal state are uncontrolled. Initial setup runs
`baseline.json`/`profile-1.json` had concurrent CPU checks and are excluded from
the three-repetition summary, not discarded.

## Observations

Throughput below is the median of three **uninstrumented GPU-completed render
rates**, not physical presentation FPS. GPU/CPU triples are p50/p95/p99 from
72 pooled instrumented completed samples at each identical extent. Instrumentation
can perturb scheduling; these are separate measurements, not additive costs.

| Phase / pixels | Completed frames/s before → after | Owned-buffer maximum bytes |
|---|---:|---:|
| Window / 1280×720 | 59.31 → 59.37 | 228,405,632 |
| 4K / 3840×2160 | 36.64 → 45.80 | 489,622,464 |
| Fullscreen / 2880×1800 | 49.67 → 64.12 | 474,294,528 |
| Restored / 1428×1746 | 58.77 → 59.09 | 364,235,136 |

| Interval ms | Before p50/p95/p99 | After p50/p95/p99 |
|---|---|---|
| 4K GPU tile index | 10.387 / 10.445 / 10.722 | 6.012 / 6.039 / 6.047 |
| 4K GPU composition | 8.213 / 8.251 / 8.684 | 8.401 / 8.429 / 8.432 |
| 4K sum of measured GPU stages | 21.140 / 21.250 / 21.995 | 17.908 / 18.038 / 18.063 |
| 4K CPU count wait | 23.955 / 25.234 / 25.641 | 20.244 / 21.174 / 21.405 |
| Fullscreen GPU tile index | 10.278 / 10.488 / 10.506 | 5.673 / 6.378 / 6.910 |
| Fullscreen CPU count wait | 18.408 / 19.085 / 19.253 | 14.320 / 17.335 / 19.157 |

Owned-buffer maxima are identical before/after, sampled after draws; they exclude
driver query-pool memory, transient/process peaks and total device VRAM. All phases
retain full scene counts and zero Vulkan/sync errors. 4K still submits eleven
counts for six accepted batches; fullscreen three for two. References remain
13,748,984 at 4K and 9,369,155 fullscreen, with each emitted batch bounded.
At 720p acquisition dominates (after CPU p50 14.643 ms); GPU tile-index p50 falls
2.654→1.626 ms while throughput remains FIFO limited. Retain short-run variation:
before profile-2 window reports 70.27 completed frames/s; pooled before window
count-wait p99 is 21.955 ms. Fullscreen after composition p99 rises to 9.219 ms.
`summary.json` retains all phase/stage percentiles and per-run rates, not just gains.

## Checks, identity and limits

GCC 16.2.1 C++26 / Clang 23.1.1 explicit C++23 with libstdc++; CMake 4.4.4;
format/tidy 23.1.1. `tools.log`/`device.log` retain actual versions.
All commands below exit 0.

| Command | Evidence in R |
|---|---|
| `cmake --build --preset dev -j 4`; `cmake --build build/clang-viewer-cxx23 -j 4` | Full native builds, `build-measured-dev/clang.log`; final dev no-op `build-settled.log` |
| `ctest --preset dev`; `ctest --test-dir build/clang-viewer-cxx23 --output-on-failure` | **93/93 each**, `ctest-final-dev/clang.log`; added wrap/scale/overflow and radix-key-width boundary cases |
| `python3 $R/run_quiet_trials.py`; `python3 $R/run_after_trials.py` | Native runner uses `we_gpu_viewer_regression ... [--timings]`; quiet before `baseline/profile-2/3/4.json/log`, after `after-baseline/profile-1/2/3.json/log`; runner logs retained |
| `python3 $R/run_gpu_checks.py` | Native GCC/Clang `we_gpu_smoke ... --timings`: each 51 frames, pause/update/resize/empty/budget/overflow checks; native original/compositing/termination/batches/batches-bounded/real reference commands all append `--timings`; `gpu-checks.log`, per-command logs |
| `.venv-train/bin/python tests/gpu/compare_reference.py $R/<case> [--real]` | **35 passing views**, real uses `--real`; all `compare-<case>.log` and per-case `comparison.json` |
| `clang-tidy -p build/dev` on renderer/device, smoke/reference/regression and two new unit files | No first-party diagnostics; `tidy-final.log` |
| `clang-format --dry-run --Werror` on eleven changed C++ headers/sources; `python3 tools/check_boundaries.py`; `python3 tools/check_docs.py`; `git diff --check` | Format, boundaries, documentation/whitespace pass; corresponding final logs |

Pinned local gsplat/Torch/CUDA environment is unchanged from
[calibrated evidence](2026-10-13-real-reference.md): no downloads/training.
All 35 reference PPMs and fixture JSONs are byte-identical to parent evidence;
all twelve quiet-run 4K screenshots are byte-identical. Comparisons retain the
fixed PSNR ≥45 dB / maximum eight levels; observed maximum **one level**, real
minimum **63.165 dB**. Bounded mode verifies multiple accepted batches/query reuse.

Parent `d50e9b7`. Before source/binary identity and patch are
`before-sort-identity.json`/`before-sort.patch`; retained baseline executable is
`we_gpu_viewer_regression-before` (uses unchanged local shader directory).
Final `after-identity.json` SHA-256
`5132d100ca4406cbbdc0a37485b12d392fb9a3b4b6a02b87b8ac89a6b75dd07b`
records sources, binaries, SPIR-V and runner/summary/protected inventories.
**329 original media/job/fit/bundle files remain byte-identical**, including the
video/fit/scene identities in calibrated evidence. Specification/runbook/AGENTS
and every compiled shader are unchanged. No original solver/fit rerun or stale
decode pointer rewrite occurred.

No functional native failures occurred. Initial tidy findings (enum width,
constexpr naming, argv taint and pre-widening arithmetic) were corrected without
suppressions; the initial log remains. Timestamp-query overhead/frequency and
short-run tails prevent a controlled instrumentation-overhead claim. Reference
hardware, presented intervals, prescribed durations/workloads, native unsupported
timestamp/other-device paths, long lifetime/device-loss stress and the fresh owned
reconstruction chain remain pending. Next work is in [progress](../progress.md).
