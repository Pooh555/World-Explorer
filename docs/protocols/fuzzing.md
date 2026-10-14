# Bounded synthetic parser fuzzing

The opt-in `fuzz` preset builds `we_fuzz_ply` with Clang libFuzzer,
ASan/UBSan and C++26. A compile/link probe checks the selected sanitizer runtime.
Viewer, CTest, coverage and other sanitizer selections are rejected in this
profile. Normal builds do not acquire fuzzer instrumentation. The build preset
selects only the PLY target and its CPU dependencies.

The harness calls the production `load_gsplat_ply` entry point. Each process
reserves an exclusive directory under `build/fuzz/fuzz-scratch/`, rewrites one
synthetic input file and removes its directory on normal exit. Successful
admission must return scene ID 42 and pass scene validation. Both successful and
rejected admission must preserve every input byte. Violations abort, allowing
libFuzzer to retain the offending input. A crash can leave its scratch directory;
the saved input in the campaign's crash directory is the reproduction artifact.

Harness limits are 65,536 input bytes, 8,192 header bytes, 64 splats, one chunk,
`64 * sizeof(gaussian)` payload bytes and zero source frames. Inputs above the
harness limit are skipped. The production reader also bounds each vertex/trailing
line to 16,384 raw bytes. These are diagnostic budgets, not product limits.

`tools/make_ply_fuzz_seeds.py` creates ten synthetic ASCII inputs: SH degrees
0–3 with LF and CRLF, an excessive header and an invalid quaternion. A manifest
records each input's size and SHA-256. The helper requires a fresh subdirectory
of this repository's ignored `build/` or `artifacts/`, rejects existing outputs
and follows no external source data. Never seed campaigns with private videos,
saved jobs, fits or scene assets.

Run from the repository root with a fresh name:

```sh
cmake --preset fuzz
cmake --build --preset fuzz -j 4
FUZZ_DIR=artifacts/fuzz/ply-run-001
python3 tools/make_ply_fuzz_seeds.py "$FUZZ_DIR/seeds"
mkdir "$FUZZ_DIR/corpus" "$FUZZ_DIR/crashes"
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  build/fuzz/we_fuzz_ply -seed=20261014 -max_total_time=60 -timeout=2 \
  -rss_limit_mb=512 -malloc_limit_mb=128 -max_len=65536 \
  -use_value_profile=1 -print_final_stats=1 -dict=tests/fuzz/ply.dict \
  -artifact_prefix="$FUZZ_DIR/crashes/" "$FUZZ_DIR/corpus" "$FUZZ_DIR/seeds/corpus"
```

Record actual argv, environment, compiler/source/binary/seed hashes, duration,
executions, accepted/rejected counts, sanitizer findings, crash files and peak
RSS in a dated validation report. libFuzzer edge/feature counters are not the
reviewed E1 source coverage measurement. Native execution is needed when sandbox
ptrace prevents LeakSanitizer; retain leak detection. A time-limited campaign
does not certify parser safety or a full milestone.

Replay any retained crash with the same build and sanitizer options by passing
its file path as the sole positional input. Add a deterministic public-API
regression that reproduces it before fixing production code. Bundle/update
fuzzing and concurrent filesystem faults are not covered by this PLY target.
