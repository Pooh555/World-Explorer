# Bounded scene bundle fuzzing — 2026-10-14 Asia/Hong_Kong

Scope/status: **M1/E1 IN_PROGRESS**. A third isolated Clang CPU libFuzzer target
exercises production `load_bundle` with fresh synthetic bundles. Its native
60-second campaign reports no crashes or ASan/UBSan/LeakSanitizer errors. This
bounded result does not certify M1/E1, other platforms or real reconstruction
gates. Parent `67d6a34`; implementation is the commit containing this report.
Cwd: repository root. `R = artifacts/validation/bundle-fuzz-20261014` retains
exact argv/environment/exits, logs, seeds, generated corpus and identities.

## Harness and limits

[Harness](../../tests/fuzz/bundle_fuzzer.cpp) and
[seed generator](../../tools/make_bundle_fuzz_seeds.py) implement the
[fuzzing protocol](../protocols/fuzzing.md). Each process reserves a private build
scratch directory and creates a labeled synthetic two-chunk bundle through the
production writer. Mutations target raw/typed manifest values, index entries,
both chunks and provenance. Optional hash/length repair reaches admission beyond
checksum rejection. Arbitrary references never become harness write paths.
Oracles compare the entire caller tree before/after loading, validate admitted
snapshots and check the previous successful snapshot survives later file edits.
Snapshot hashes use production hashing, not an independent hash implementation.

Bounds: **4,104 input bytes / 4,096 mutation payload bytes**; default loader
budgets **8,192 manifest/provenance bytes / two chunks / eight splats / 8,192
aggregate encoded chunk bytes / four source frames per chunk**. Budget mutations
include smaller limits and overflowing chunk-limit rejection. Each template
chunk has one SH3 splat and two frames. One previous snapshot is retained.
Campaign limits: **2 s/input / 512 MiB RSS / 128 MiB single allocation**.
No private media, jobs or scene assets seed this target.

**196 seeds / 7,653 bytes** cover typed fields/root values, path references,
hashes, rehashed binary counts/numerics, truncation/trailing bytes and budgets.
Seed manifest SHA-256:
`cf59bdfbf34256631c733e411d9818735e90cedbb6b8b0867cb23e0e84d60177`.
`R/identity.json` records individual implementation, seed, binary, libraries,
compile-command/cache and local runner identities. Arch Linux x86-64,
Clang/format/tidy **23.1.1**, CMake **4.4.4**, GCC **16.2.1**. The isolated
RelWithDebInfo C++26 build instruments first-party dependencies and harness
with libFuzzer, ASan/UBSan and nonrecovering sanitizer checks. Coverage, TSan,
CTest and viewer remain separate; normal builds acquire no fuzz target.

## Actual checks

| Command (complete argv in R) | Exit / result / log |
|---|---|
| `cmake --preset fuzz`; `cmake --build --preset fuzz -j 4` | Both 0, all three targets build; `build-{0,1}.log`, `build-commands.json` |
| Native seed replay, protocol options with `-runs=0` | 0, **197 callbacks: 16 accepted / 181 rejected**, including empty input; **11 index / 40 chunk / 6 provenance rehash attempts**, `seed-0.log` |
| Native protocol campaign, seed 20261014 | 0, **55,915 inputs in 61 s** (61.092 s wall), **339 MiB peak RSS**; `campaign-0.log`, `campaign-commands.json` |
| `cmake --preset dev`; build `-j 4`; `ctest --preset dev` | All 0, GCC C++26 **257/257**, 66.07 s; `dev-{0,1,2}.log` |
| Compatibility build `-j 4`; `ctest --preset compat-cxx23` | Both 0, explicit GCC C++23 CPU **239/239**, 65.47 s; `compat-{0,1}.log` |
| Format/tidy warnings-as-errors, docs, boundaries, diff | All 0; `static-commands.json`, final documentation receipts |
| Existing seed output / tracked-directory output / TSan combination | Expected **2 / 2 / 1**; `negative-commands.json`, `negative-{0,1,2}.log` |

Campaign counters: **3,612 accepted / 52,303 rejected**; **3,272 index / 7,902
chunk / 2,178 provenance rehash attempts**. Rehash counters do not prove each
edit reached a particular validation branch. Final libFuzzer counters: **2,400
edges / 15,589 features**, 3,110 new units, mutation length reaches **4,104**;
**2,551 saved corpus files / zero native seed or campaign crashes**. These are
not E1 source-coverage measurements. Dev/compatibility checks ran concurrently;
campaign time/RSS are observations, not performance qualification. Cleanup error
is zero. No production failure or repair was observed.

Initial sandbox seed replay checked its invariants but exited **77** at shutdown:
ptrace blocks LeakSanitizer. `sandbox-seed.log` and its empty runtime-generated
input in `sandbox-runtime-crashes/` are retained separately. Native seed replay
and campaign keep leak detection enabled and pass. Initial tidy rejected a
missing switch default and signed-byte XOR; the corrected harness passes final
tidy before the native campaign. `initial-static-1.log` retains the diagnostics.

**2,393 protected files and all 94 tracked src/include files remain byte-identical**;
`protected-{before,after,result}.json`, `production-{before,after,result}.json`.
All seed sizes/hashes were rechecked. No reconstruction, GPU, environment, recipe,
schema, threshold or coverage-scope changes; no upload/push. Publication faults,
symlinks/missing assets, allocation failures, concurrent filesystem changes,
longer campaigns and complete hardware/platform gates remain unqualified.
