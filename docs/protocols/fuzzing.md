# Bounded synthetic parser and transaction fuzzing

The opt-in `fuzz` preset builds `we_fuzz_ply` and `we_fuzz_scene_transactions` with Clang libFuzzer,
ASan/UBSan and C++26. A compile/link probe checks the selected sanitizer runtime.
Viewer, CTest, coverage and other sanitizer selections are rejected in this
profile. Normal builds do not acquire fuzzer instrumentation. The build preset
selects only these targets and their CPU dependencies.

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
regression that reproduces it before fixing production code. Bundle loading and
concurrent filesystem faults are not covered by this PLY target.

## Scene transaction target

`we_fuzz_scene_transactions` calls production `scene_store::commit` and
`changes_since` using a fresh store per callback, with no input/output files.
Setup resets to a labeled synthetic snapshot; its revision can start at zero or
UINT64_MAX−1 and its initial chunk generation at one or UINT64_MAX.
`tools/make_scene_transaction_fuzz_seeds.py` creates 44 public synthetic bytecode
seeds with size/hash identities and the same fresh-output rules as the PLY helper.

Diagnostic bounds: **772 input bytes / 32 steps / six proposed changes per step /
nine splats and five source frames per proposed chunk**. Stored scenes allow
**four chunks / zero–eight aggregate splats / the selected splat count times
`sizeof(gaussian)` payload bytes / four source frames per chunk**. Journal entries
range zero–four; remembered operations zero–eight. The harness retains at most
33 snapshots and eight transaction receipts. Each step issues at most two main
commits, eight receipt retries, eight conflicting retries and one replay query.
Input beyond 772 bytes is skipped; partial records are zero-padded.

The four header bytes select journal length, receipt window, aggregate splat
budget and setup flags. Each 24-byte record maps as follows; selectors are
bounded by the harness, with no allocation sized directly from an integer input.

| Offset | Meaning |
|---|---|
| 0–1 | Create/replace/delete/invalidate/transform, exact/conflicting retry or invalid enum; chunk/receipt index |
| 2–3 | Fault selector; optional payload, duplicate/distinct additional changes |
| 4–8 | Generation mode, splat/frame counts, fresh/zero/reused operation ID, source pose revision |
| 9–12 | Invalid enum/SH degree, scalar axis, replay revision and byte-budget selectors |
| 13–15 | Reserved |
| 16–23 | Little-endian raw double bits; low 32 bits also decoded as a float |

Oracles check caller chunk content before/after every commit; pointer-identical
current state after rejection/duplicate; validated successful revisions and
declared operation effects; unchanged chunks and retained snapshots. Mutable
producer aliases are deliberately corrupted then restored to test deep ownership.
Every remembered operation retries exactly and rejects conflicting content.
Rejected unused nonzero operation IDs are retried with corrected metadata and
empty changes, isolating receipt reuse from chunk-budget failures. Replay queries
check exact payload budgets, bounded journal resync and snapshot metadata.
These checks use the existing canonical scene hash for content comparison; this
is not an independent proof of that hash implementation or fingerprint collision
resistance. Allocation failures, concurrency and bundle/filesystem faults remain
outside this target's scope.

Run from the repository root with a fresh name, after the same fuzz build:

```sh
FUZZ_DIR=artifacts/fuzz/scene-transactions-run-001
python3 tools/make_scene_transaction_fuzz_seeds.py "$FUZZ_DIR/seeds"
mkdir "$FUZZ_DIR/corpus" "$FUZZ_DIR/crashes"
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  build/fuzz/we_fuzz_scene_transactions -seed=20261014 -max_total_time=60 \
  -timeout=2 -rss_limit_mb=512 -malloc_limit_mb=128 -max_len=772 \
  -use_value_profile=1 -print_final_stats=1 -artifact_prefix="$FUZZ_DIR/crashes/" \
  "$FUZZ_DIR/corpus" "$FUZZ_DIR/seeds/corpus"
```

Native sanitizer execution, identities, failure retention and deterministic
reproduction follow the rules above. Report commit/rejection/corrected/retry/
conflict/replay counters separately from libFuzzer's input execution count.
