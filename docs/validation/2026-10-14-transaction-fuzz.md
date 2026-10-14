# Bounded scene transaction fuzzing — 2026-10-14 Asia/Hong_Kong

Scope/status: **M1/E1 IN_PROGRESS**. A second isolated Clang CPU libFuzzer target
exercises production scene commits and replay with fresh synthetic chunks.
The native 60-second campaign completes without crashes or reported
ASan/UBSan/LeakSanitizer errors. Full milestones, other platforms and real
reconstruction gates remain unqualified. Parent
`a029e61b428c87eea48f32d939a45f114e4cea96`;
implementation is the commit containing this report. Cwd: repository root.
`R = artifacts/validation/transaction-fuzz-20261014` holds local logs, exact
argv/environment/exit receipts, seeds, generated corpus and identities.

## Harness and scope

[Harness](../../tests/fuzz/scene_transaction_fuzzer.cpp) and
[seed generator](../../tools/make_scene_transaction_fuzz_seeds.py) follow the
[versioned protocol](../protocols/fuzzing.md). Oracles check caller chunks,
snapshot ownership/immutability, atomic rejection, declared successful effects,
corrected unused operation IDs, exact/conflicting retries and bounded replay.
Content comparisons use the existing canonical scene hash; this is not an
independent hash/fingerprint collision test. Reset only initializes the store.
No private media, saved assets or jobs enter the harness.

Limits: **772 input bytes / 32 steps / six proposed changes per step / nine splats
and five source frames per proposed chunk**. Stored limits: **four chunks /
zero–eight aggregate splats / selected splat count × `sizeof(gaussian)` payload
bytes / four source frames per chunk**. Journal/receipt windows are zero–four /
zero–eight; retained snapshots ≤33. Campaign: **2 s/input, 512 MiB RSS,
128 MiB/single allocation**. Near-exhausted revisions/generations, zero windows,
invalid enums/IDs/metadata, raw numerical bits and multi-change atomicity are seeded.

The **44 synthetic seeds** total **3,104 bytes**, including a full 772-byte
sequence. Manifest SHA-256:
`ccec196fe18a0cfbdcfc9d42c47fa0398383912ae451c0329fcc8cfdd5e032de`.
`R/identity.json` records individual changed source/binary/library/cache/compile
command identities and tool versions. Arch x86-64, Clang/format/tidy **23.1.1**,
CMake **4.4.4**, C++26 RelWithDebInfo; first-party dependencies and harness retain
fuzzer instrumentation, ASan/UBSan and nonrecovering sanitizer checks.
Normal builds, coverage and TSan remain separate.

## Actual checks

| Command (R supplies complete argv) | Exit / result / log |
|---|---|
| `cmake --preset fuzz`; `cmake --build --preset fuzz -j 4` | 0 / both targets built, new harness linked; `configure.log`, `build.log`, final `build-final.log` |
| Native seed replay, `-runs=0`, seed 20261014 and leak detection enabled | 0 / **45 inputs, 117 accepted commits, 45 rejected commits, 40 corrected commits, 346 exact retries, 346 conflicting retries, 122 replay queries**; `seed-0.log`, `seed-commands.json` |
| Native protocol campaign with unchanged options/seed 20261014 | 0 / **30,251 inputs in 61 s** (61.085 s wall), **308 MiB peak RSS**; `campaign-0.log`, `campaign-commands.json` |
| `cmake --preset dev`; build `-j 4`; `ctest --preset dev` | All 0 / GCC C++26 **257/257**, 59.93 s; `dev-{0,1,2}.log`, `dev-commands.json` |
| Compatibility build `-j 4`; `ctest --preset compat-cxx23` | Both 0 / explicit GCC C++23 CPU **239/239**, 62.56 s; `compat-{0,1}.log`, `compat-commands.json` |
| Format, tidy with warnings-as-errors, docs, boundaries, diff check | All 0; `static-commands.json`, `static-{0,1,2,3,4}.log` |

Campaign counters: **215,894 accepted / 276,689 rejected / 178,931 corrected
commits; 1,228,745 exact retries and 1,228,745 conflicting retries; 313,652 replay
queries**. Corrected commits are included in accepted counts; receipt probes are
separate. Final libFuzzer counters: **1,040 edges / 11,433 features**, mutation
length reached the 772-byte bound, 1,229 new units and **1,064 corpus files /
zero campaign or native seed crash files**. These counters are not E1 source
coverage. Dev/compatibility checks ran concurrently on this host; elapsed time
and RSS are campaign observations, not a performance qualification. No production
failure or repair was observed.

Initial sandbox seed replay completed its invariants, then exited **77** because
ptrace blocked LeakSanitizer; `seed-replay.log` and its empty runtime-generated
input in `sandbox-runtime-crashes/` are retained separately. Native replay and
campaign retain leak detection and pass. Initial tidy found two harness warnings;
using the existing saved caller copy and distinct state/snapshot parameter types
repairs them; `tidy.log`, `tidy-final.log`. No code changed after the native runs.
Existing seed output and tracked-directory output reject with exit **2**, and
TSan combination rejects with exit **1**; `negative-commands.json` and logs.

**2,393 protected files and all 94 tracked src/include files remain byte-identical**;
`protected-{before,after,result}.json`, `production-{before,after,result}.json`.
No reconstruction/GPU/environment rerun, schema/recipe/scope/threshold change,
upload or push. Bundle fuzzing, allocation/concurrency/filesystem faults, longer
campaigns, native qualification and complete gates remain pending.
