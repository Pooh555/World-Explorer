# Bounded full-sequence fusion — 2026-10-20 (Asia/Hong_Kong)

M3 remains **IN_PROGRESS**. All 63 real sample_1 depth references now feed one
global extent and sparse TSDF, retained-job cancellation/resume/replay, native
pose-bound admission and visible mesh handoff. The raw **39,683 vertices / 49,833
triangles** remain visibly fragmented across **1,625 components**. No measured geometry, walking, Q/P/N/E or
asset-track gate is promoted. [Fusion contracts](../protocols/fusion.md#bounded-full-sequence-cpu-worker)
own resource, unknown, compatibility and publication rules.

Repository root is the default cwd. Parent `02f1db2e4ca2`; ignored evidence root
`R=artifacts/validation/m3-sequence-fusion-20261020`. `source-identity.json` pins
the initial worker attempt; `owned-source-identity.json` pins the owned run.
`final-source-identity.json` binds the final sources and native binaries.
Native union SHA-256 is
`3362938dda673311e36739c3eed5c3ece8b303cd8c29cd0f1343c28300b2cf26`.
Owned recipe SHA-256 is
`6213c2a46b952bd3e385e6148b769c3b589aeebcf927bfacd7669a88082b5c93`;
request SHA-256 is
`e1723b6a8b47cf8ef61dc2d80daf43cb8ed61acb9909727bc33edd353a6f5106`.
Existing CPython 3.12.15 / Open3D CPU 0.19.0 / NumPy 2.2.6 / Pillow 11.3.0
environment verifies **78 distributions / 16,802 listed files**, zero differences
from its pinned receipt. No CUDA solve, decode, pose solve, refit, download or
upload ran.

## Real CPU and native checks

`verify.py`, `post.py`, `owned.py`, `oracle.py`, `native.py` and their JSON command
records retain exact arguments, exits, timings and logs under R. The initial
request uses the earlier implementation pin, before successful-scratch cleanup
was added; its complete outputs and scratch remain immutable.

| Command / invocation | Observed result |
|---|---|
| Isolated fusion interpreter, `-m unittest discover -s tests/integration -p worker_fusion_test.py -v` | 15 final contracts, no skips: pooled exact percentiles, cross-cache support/late occlusion, bounded scratch/maps, sigma/unknown preservation and late-child owner rejection |
| Same interpreter, pattern `worker_admitted_visibility_test.py` | Eight reader contracts pass |
| Same interpreter, `workers/fusion/run.py R/request.json REQUEST_SHA R/attempt-0` | Exit 0; 134.73 s, peak sampled child RSS 768,053,248 bytes |
| `build/dev/we_process run-geometry R/owned-request.json REQUEST_SHA R/pipeline` | Populated stopped-child cancel exits 130 in 2.12 s, all three descendants gone; resume exits 0 in 272.30 s; exact replay 10.43 s without fusion launch |
| `build/dev/we_process validate R/pipeline/stages/native/stages/attempt-0/scene.wexplore` | Native saved bundle valid |
| `build/dev/we_viewer --scene BUNDLE --frames 90 --settings R/viewer-settings.json --screenshot R/visible.ppm` | RTX 4060 Laptop: all vertices/triangles resident, 90 frames, zero Vulkan validation errors |
| R/native_probe, prior splat scene + completed request/job + new report directory | 766 initial / 713 rebuilt native frames, zero Vulkan errors; failed/cancelled geometry preserves prior pixels exactly; replay hands off all triangles |

The owned worker takes **257.57 s**, peak sampled RSS **768,741,376 bytes**.
Both executions retain 300 s / 4 GiB RSS / 32,768 blocks / 512 MiB product limits;
reserved scratch is **2,534,223,240 bytes**, below its separate 4 GiB cap.
Builds, static analysis and other local checks overlap the owned attempt. These
single-run observations are not controlled performance or P4 qualification.

Exact pooled extent uses **22,644,987** positive-weight points:
**12.97811346497069** arbitrary camera units, voxel size
**0.02534787786127088**. Of **142,184,448** input pixels, **22,644,987** are valid,
**119,539,461** unknown and **10,158,960** accepted. All 63 ordered view rows remain.
The grid has **16,826 blocks**, **270,065 observed / 8,344,847 allocated-unknown
voxels**, **137,532** observed front-band voxels and **zero available-sigma voxels**.
Front-band evidence is not full-ray free space. An independent world-transform
calculation reproduces pooled percentiles within 1e-12; **4,032** independently
projected real samples reproduce all-source acceptance, and all mask counts and
unknown pixels are checked. Native conversion preserves every voxel plane and
mesh array exactly. Direct/owned runs also match every voxel and canonical mesh
position/triangle; maximum sorted normal difference is 1.19e-7. The
13,546,751-byte asset SHA-256 is
`ecab86951d03826d461700019276450f9ecbbf0ee4d30713dcf735531e573541`.

Final commands/results are in `repair-commands.json`, with matching named logs:

| Command | Final result |
|---|---|
| `cmake --build --preset dev -j 4` | Native C++26 build, `build-component-repair.log` |
| `cmake --build --preset compat-cxx23 -j 4` | Explicit C++23 build |
| `ASAN_OPTIONS=detect_leaks=0 cmake --build --preset asan --target we_core_tests we_process -j 4` | Instrumented native build; actual checks enable leak detection |
| `ctest --preset dev` / `ctest --preset compat-cxx23` | **384/384 / 360/360**, 101.29 / 96.53 s |
| `ASAN_OPTIONS=detect_leaks=1 build/asan/we_core_tests '[fusion],[fusion-components]'` | **18 cases / 300 assertions**; full real `inspect-fusion` also passes |
| `python3 tools/check_cpp_style.py` with the five changed C++ paths | Format/tidy **23.1.1**, boundaries, docs and whitespace pass |
| Required per-file Black check for every tracked Python file, plus new `sequence.py` | **Black 26.5.1** passes; exact command in `repair-commands.json` |

## Corrections and verification scope

The first component audit rejects the sequence with `Invalid component voxel
evidence`: its diagnostic still assumed 18 integrating views. The repair derives
the sequence inventory during native admission and keeps backend support's
existing native 1,000,000-per-observation cap separate. Legacy diagnostic bounds
and their rejection cases remain intact. `components.log` and the failed audit
driver remain retained. The repaired development build passes **26 cases / 441
assertions** across fusion, component and cleanup contracts. Final cross-profile
checks pass as recorded above.

Two multi-file Black invocations stalled in the sandbox and were terminated;
per-file formatting and required full tracked-file checks pass. Existing fusion
thresholds, resource limits, warnings and image/depth selections are unchanged.
`python3 tools/check_dependency_manifest.py --local-evidence` exits **2**,
`Current runtime payload differs` (`inventory-current.log`): runtime receipts
need recapture after native rebuilds. The installed fusion environment check is narrower than dependency/release closure.

Cancelled worker scratch and 258 owned-attempt files survive resume/replay.
All **151 admitted-cache files**, **139 earlier depth-attempt files**, **24,457
protected files**, four original bakes and 33 prior audit/bundle files retain
their observed bytes. The 3,982 pre-existing external-header differences remain.
Sequence cleanup/baking/export and the fresh-depth terminal adapter remain separate work.
Scale/alignment, supported collision/walking, corpus quality and rights remain
open; A0–A6 remain NOT_STARTED. The exact next task is in [progress](../progress.md#next-executable-step-and-protection-rules).
