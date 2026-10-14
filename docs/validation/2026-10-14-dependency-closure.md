# Selected dependency provenance — 2026-10-14 Asia/Hong_Kong

Scope/status: **M1/E2 IN_PROGRESS**. Checked source/archive/notice gap inventory;
no rights, release, reconstruction, GPU or full-gate qualification. Cwd for all
commands: `/home/Pooh555/programming/World-Explorer`. Parent `09a7074`; implementation
is the local commit containing this report. Raw ignored evidence
`R = artifacts/validation/dependency-closure-20261014`; `R/identity.json` binds
checker/test/generator/runner, CLI and build-cache bytes. The
[manifest](../../third_party/dependency-audit-20261014.json) SHA-256 is
`54b8d83802cf0347fb1dee281a681baa76da948b430151e150c7cae61966e5f5`.

## Checked inventory and limits

| Profile | Actual recorded scope |
|---|---|
| Native development archives | **4/4** SHA-256 pins match selected local archives; selected notices match retained text. JSON's existing first-line trailing space was removed in the retained copy; both hashes and that exact one-byte normalization remain explicit |
| Training / pose / CPU metrics | **37 / 3 / 16 distributions**, respectively **70 / 6 / 43 notice files**. Requirement/selection/metadata/RECORD/inventory hashes bind the retained raw receipts; selected current metadata and notice bytes are rehashed. Original wheel/source bindings remain absent; metric installer hashes are allowed archives, not recovered selected-wheel bytes |
| Linux system/build tools | **12 package observations**, four selected local notice snapshots, explicit source/archive/transitive gaps. FFmpeg's actual configuration enables GPL and version3; package metadata says GPL-3.0-only. This differs from the proposed minimal LGPL release build |
| Release-vcpkg / metric models | **8 unresolved direct port declarations / 2 model resources**. Immutable registry identity, flatbuffers override and existing model/package hashes are retained; release resolution and model/dataset/transitive terms remain unqualified |

Total: **82 components / seven profiles / 208 bound inputs / 249 component gap
entries**. Gap entries can recur across profiles; they are not a count of defects.
The [checker](../../tools/check_dependency_manifest.py) validates offline
consistency/completeness and rejects stale evidence. It grants no clearance.
[Protocol](../protocols/dependency_sources.md) defines bounds and separate exit
statuses; the inventory remains a selected audit, not a complete SBOM.

Triton `v3.0.0` resolves through the official tag API to
`55a4ab051c88ba2baa031e520a339d3fded6468f`. Exact-commit
[LICENSE](https://github.com/triton-lang/triton/blob/55a4ab051c88ba2baa031e520a339d3fded6468f/LICENSE),
`python/triton/__init__.py` and `python/setup.py` bytes are retained locally.
The [supplemental source notice](../../third_party/notices/triton-3.0.0-LICENSE.txt)
hashes to `92640fb97222fd0a698ff28ce0c3782c172623f8d6c609b557636a80f28fb946`.
The installed `__init__.py` exactly matches selected source SHA-256
`ded80ef47a6837162e4f6b2f74ab59c991d89b620d337d1abff9e56b39925e3b`.
This is one-file parity, not an entire-wheel build attestation. Installed RECORD
still has no separate notice. Its NVIDIA `ptxas`/`cuobjdump`/`nvdisasm` and `cuda.h`
identities remain explicit, with LLVM/AMD/build dependency and redistribution terms
unreviewed. No package/model installation or environment provisioning occurred.

## Actual commands and results

Arch Linux x86-64, GCC **16.2.1**, CMake **4.4.4**; Python **3.14.8 / 3.12.15 /
3.10.22**. `R/run_checks.py` and each `*-commands.json` retain full argv/cwd/exits.

| Commands | Result / raw evidence |
|---|---|
| `python3 tools/check_dependency_manifest.py`, then `--local-evidence` | Both exit 0; consistent inventory, rights false; `manifest-{0,1}.log` |
| Same local check with `--require-closure` | Expected exit **3**; open gaps remain; `manifest-2.log` |
| `python3 R/run_checks.py focused`; `workers` | **11/11 new / 79/79 worker cases** on each interpreter, no local skips; `focused-{0..2}.log`, `workers-{0..2}.log` |
| `cmake --preset dev`; `cmake --build --preset dev -j 4`; `ctest --preset dev` | All exit 0; GCC C++26 **260/260**, 60.61 s; `dev-{0..2}.log` |
| `cmake --preset compat-cxx23 -DWE_BUILD_VIEWER=OFF`; compatibility build `-j 4`; compatibility CTest | All exit 0; explicit C++23 CPU **242/242**, 59.94 s; `compat-{0..2}.log` |
| Docs, boundaries, `git diff --check` | Exit 0; `static-commands.json` / `static-*.log` |

Synthetic regressions cover stale/missing/duplicate/unsafe evidence, omitted
profiles and incomplete worker selections, incorrect versions, rehashed notice edits, raw receipt digest
deviations, archive notice ambiguity/links/budgets and exact source comparisons.
Strict closure and invalid evidence have distinct exits. Archive scans use a
checked in-memory snapshot and extract no files. C++ production is unchanged;
format/tidy/sanitizer/GPU campaigns were not rerun for this Python audit slice.
Build suites ran concurrently; durations are test observations, not performance
qualification. Final path/byte-count guards are separately rechecked through all
79 worker cases on three interpreters and each profile's `worker_integration` CTest
(exit 0, **1/1**); `final-worker-commands.json` binds their exact source hashes. Initial notice comparison found the JSON whitespace difference;
the manifest records it rather than changing existing notice text. An initial
test insertion misplaced an assertion; `focused-initial-0.log` retains its failure,
and corrected tests pass on all three interpreters. Sandbox DNS failure preceded
authorized native retrieval of the small source/notice files; no model fetch.

## Current saved-run reconciliation and preservation

The old 2,393-file inventory no longer matches the saved workspace: **639 absent /
103 different / 334 newly present** entries. `historical-state-delta.json` retains
the differences; this audit does not attribute their cause or restore old data.
The current sample_3 source hash is
`4448a1d9eb93f8b7fc787f9ea037d282128438a2512fad8eb8df8544f53906da`, superseding
the earlier `31fa05e1...` source. Saved results report **80 poses, 7,000 fit steps,
648,332 splats**, fitting time 398.945 s and sequential command time 1,020.200 s;
these are retained-run timings, not a newly measured reconstruction or P1 pass.
Sample_1 retains **295,952 splats**; sample_2 retains exit 2 / POSE_DISCONNECTED
and no published scene. `current-runs.json` binds current source/request/result/
manifest identities and keeps terminal status distinct from verification.

`build/dev/we_process validate artifacts/sample_1/scene.wexplore` and the same
command for sample_3 both exit **0**: revision 1, one chunk, scene IDs
`149348248299280656` / `17008048767614358375`. `current-run-commands.json` and
`current-sample{1,3}-validate.log` retain those actual read-only checks. This is
structural bundle validation; sample_3's native viewer/holdout is not exercised.
Current inventories before/after those validations retain **2,088 protected files
byte-identically**, including all three videos, current saved jobs/fit/scenes and
AGENTS/runbook/specification. All **94 tracked src/include files** match the parent.
No reconstruction/retry, checkpoint change, deletion/restoration, upload or push.
Native runtime closure, full source/archive/rights and platform/reference gates
remain pending; next executable work is in [progress](../progress.md).
