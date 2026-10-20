# Fresh sequence geometry adapter — 2026-10-20 (Asia/Hong_Kong)

M3 remains **IN_PROGRESS**. The shared fresh geometry entry now admits complete
owned sequence-depth terminals, selecting every reference once and validating
all support halos before native cache export. Real 63-frame completed-depth reuse
passes cache cancellation/resume, exact replay, native scene handoff and
sanitizers. The **39,683-vertex / 49,833-triangle** output is still visibly
fragmented, at arbitrary scale with walking unavailable. No Q/P/N/E or A-track
gate is promoted. [Fusion contracts](../protocols/fusion.md#fresh-depth-through-the-shared-geometry-job)
own compatibility, bounds and failure semantics.

Repository root is default cwd. Parent `9219e114af529f792eb2b71efab914ba4182fa0a`;
ignored evidence `R=artifacts/validation/m3-fresh-sequence-20261020`.
`run/source-identity.json` pins production sources used by the real run;
`final-source-identity.json` binds final sources and native binaries. Exact
arguments/exits/timings are retained in `checks.json`, `native-commands.json` and
`run/real-commands.json`, with matching named logs and drivers `post.py`,
`native.py`, `real_fixed.py` and `compare.py`.

| Identity | SHA-256 |
|---|---|
| Fresh `run/request.json` | `269a76ebde82a91fa5f5fc244d8a1a5a1dfb3c3d6be88701393b3ff0c660726c` |
| Sequence fusion recipe | `6213c2a46b952bd3e385e6148b769c3b589aeebcf927bfacd7669a88082b5c93` |
| Reused dense request | `476b6b6d9c84345bee8bae61a43a7e318365398dfe6a66da41ce0aeb2445487a` |
| MVS recipe | `5072b1d083445795bf1bc7cc31bd7328b7286f53ea2ae8b1cc2aa00ee5c50894` |
| Original native input manifest | `4528cc1c5dfe1a130fb1433e87226dd323302f4e500997271ddce320c9f4869a` |
| Fresh `cache-1/manifest.json` | `a0b311978cfa7e934e3d029db61ca8e4e9dc37198b12f000d0729faae0d52599` |
| Native `observed.wefusion` | `9225ef7e33be874f3f40538323254e1e4d505327b54a73ebd41b0c4944ea924e` |
| Native bundle manifest | `6f84d9ce12f79a899e75d685698c0e58bd94b406eb58814da87ff9fa0c6084de` |

## Actual checks

All final commands exit 0 except the deliberate SIGINT cancellation (130).
GCC 16.2.1 builds retain C++26 development and explicit C++23 compatibility;
format/tidy use pinned **23.1.1**, Black **26.5.1**. Existing clean CPython 3.12.15
dense and Open3D CPU environments are reused unchanged, without reinstall or
new environment qualification.

| Command / invocation | Result |
|---|---|
| `cmake --build --preset dev -j 4` / `compat-cxx23` | Native builds pass |
| `ASAN_OPTIONS=detect_leaks=0 cmake --build --preset asan --target we_core_tests we_process -j 4` | Instrumented build; actual runs enable leaks |
| `build/dev/we_core_tests '[depth-terminal]'` | **4 cases / 1,577 assertions**: synthetic 19/63/120-frame graphs, wide IDs, legacy sparse selection, rehashed malformed/stale graphs, late halo corruption, cancellation and read-only replay |
| `ctest --preset dev` / `ctest --preset compat-cxx23` | **388/388 / 364/364**, 140.30 / 171.58 s |
| `ASAN_OPTIONS=detect_leaks=1 build/asan/we_core_tests '[depth-terminal]'` | Same 4 cases / 1,577 assertions, no reported ASan/UBSan/LSan errors |
| Same sanitizer environment, `build/asan/we_process run-geometry REQUEST SHA JOB` | Full 63-frame fresh replay passes, 32.679 s, no reported sanitizer errors |
| `python3 tools/check_cpp_style.py src/application/geometry_job.cpp src/application/depth_job_outputs.hpp tests/unit/geometry_job_tests.cpp` | Formatting, tidy, repository boundaries and whitespace pass |
| Required tracked-Python per-file Black command from AGENTS | Pass, no production Python change |

The synchronous `seed-final` harness cancels at the pre-child callback in
**2.331 s**, reserving only the new owner. It then copies the existing completed
depth job into that owned destination: **916 files / 11,215,571,462 bytes**.
The producer checks/reuses its seven completed batches; no CUDA solve, decode,
pose solve, refit, download or upload runs. This does not qualify a new sequence
CUDA solve through the fresh entry.

`we_process run-geometry` is cancelled during populated native cache export:
**75.607 s** total, **0.016 s** SIGINT-to-return, exit 130. Its **48 files** remain
in `cache-0`; resume reserves `cache-1`, completes four bounded caches and
publishes the native bundle in **408.461 s**. Exact replay takes **35.915 s**,
skipping cache export/fusion/conversion. Single runs overlap local checks and are
not controlled performance qualification. `run/reuse-preservation.json` verifies
both 916-file depth trees and the cancelled cache unchanged, with no extra depth
files. Ordered IDs/depth pins match all original 63 references; **22,644,987 valid
/ 119,539,461 unknown pixels** remain explicit.

`compare.py` matches all eight sorted voxel planes, sorted mesh positions and
canonical triangles against the prior retained fusion. Maximum sorted normal
difference is **1.1920929e-7**. New receipt/asset hashes reflect fresh ownership.
The actual viewer command in `native-commands.json` starts from the full prior
splat scene, replays the fresh job and displays all mesh geometry: **3,378 frames**
(90 requested after readiness), RTX 4060 Laptop GPU, **zero Vulkan errors**.
`visible.ppm`/lossless PNG were visually inspected: sparse fragmented surfaces,
explicit partial/unmeasured/walk-unavailable UI. The native handoff probe submits
**1,411 frames**, zero validation errors; failed/pre-cancelled jobs preserve prior
pixels exactly, then replay hands off all triangles (`native-handoff/report.json`).

## Corrections and limits

Initial compile errors used the wrong nested frame member and an ambiguous JSON
string comparison; the fix uses the dependency frame and an explicit string.
Tidy's adjacent-path warning is fixed by reordering helper arguments. The first
real template selected a different canonical pose path with the same hash;
existing preflight correctly rejects before output creation. Corrected templates
select the same path; no guard or warning is weakened. Failing build/style/seed
logs remain under R. Automatic native execution review timed out once; its
permitted retry was approved and all actual device/sanitizer checks completed.

`preservation.log` verifies **24,457 protected files**, all four prior bakes and
33 prior audit/bundle files unchanged; the known **3,982 external-header byte
differences** remain. Current dependency runtime receipts still require refresh
after native rebuilds; 253 source/rights gaps remain separately recorded.
`python3 tools/check_docs.py` and `git diff --check` pass (`final-checks.json`).
Sequence captured-color/cleanup/baking/export, measured alignment/collision,
room coverage and corpus/platform/rights qualification remain open. A0–A6 are
NOT_STARTED. [Progress](../progress.md#next-executable-step-and-protection-rules)
owns the next bounded color-union admission/audit task.
