# Real owned reconstruction chain and resume repair — 2026-10-13 Asia/Hong_Kong

Scope: **M1 IN_PROGRESS**, P1/P4/P6/E1 development evidence; no complete milestone
or Q/P/N/E certification. Cwd: repository root. Parent `874d709`; implementation
is the local commit containing this report. Ignored local evidence
`R = artifacts/validation/fresh-chain-20261013`; fresh owned outputs
`J = artifacts/sample2-fresh-chain-20261013`. No original solver/fit rerun,
media upload, remote push, dependency installation or implicit download.

## Real chain and discovered failure

The shared schema-2 command ran fresh metadata inspection, 3 FPS selection of
**63/628 source frames**, native RGB leases/journal, isolated CPU exhaustive-guided
COLMAP, canonical poses/durable map, actual **7,000-step** gsplat fitting and
checked scene publication. All output/owner directories are disjoint from prior
successful work. Input is the authorized 20.948333 s 3840×2160 SDR video, original
1/30 time base, SHA-256
`51263d9991dcbc2136d0809ecba1a8ba1b261896f3b222b7afba19784a153ef6`.
The native journal admits **1,567,642,671 bytes**, within its 2 GiB limit. An
independent post-run audit checks every PPM/RGB digest against its selected source
PNG, frame/calibration IDs, original timestamps and committed stage inventories.
COLMAP returns **one connected model, 63/63 poses**; the native map persists
63 observations, 63 poses, one calibration, epoch 0/revision 1.

SIGINT after reported fit step 1,101 produces acknowledged CANCEL, CLI exit **130**
and a full step-**1,105** checkpoint; no scene/result is published. CPU inspection
verifies six optimizers, scheduler, strategy and CPU/CUDA RNG state. Resume initially
exits **2** before starting a worker: the fresh-output overlap guard incorrectly
rejects `FIT_DIRECTORY/checkpoint.pt` inside its own writable fit directory.

The repair allows that canonical path only for explicit **RESUME**. Other fit
files, destinations and request/backend/worker/executable aliases stay protected.
A disjoint externally selected checkpoint remains supported. Two regression cases
cover owned/external checkpoints with automatic/retained decode, completed-solver
reuse, and eleven rejection sections preserving checkpoint/previous-scene bytes.
Fixtures are explicitly synthetic; the real run separately validates the optimizer.

With the repaired binary, the **unchanged request/checkpoint** resumes, skips the
completed solve, retains the input-manifest/map hashes, and publishes **297,489
splats**. Explicit completed-stage reuse skips optimization and preserves the final
checkpoint and scene manifest. `adopt.json` selects immutable inspection/native
journal stages and completed poses/fit without starting any worker, avoiding an
automatic request's future worker-source identity dependency.

## Native viewer and measurement scope

Native viewer starts with the retained original scene, runs the fresh job's
`reuse.json` on its background thread, and admits the new result through the normal
update/visibility path. It exits successfully after **4,551** submitted frames,
all **297,489 splats** resident, **zero Vulkan validation errors**, final owned GPU
buffer allocation **283,436,128 bytes** under 512 MiB. This is a final allocation,
not aggregate/transient peak VRAM or process memory. The inspected 1428×868 capture
shows the reconstructed room and explicit arbitrary-scale/no-walk labels.

Three full-scene calibrated views at **1280×689** agree with pinned gsplat:
minimum **63.149 dB**, maximum **one color level**, unchanged diagnostic PSNR ≥45 /
eight-level tolerances. This measures renderer agreement, not appearance quality.
Thirteen temporal holdouts yield **15.388–19.981 dB**, SSIM **0.696687–0.892917**,
LPIPS unavailable; Q3 remains unqualified. Scale/gravity/geometry retain their
explicit arbitrary/unknown/unavailable status.

One run, four CPU threads, no LOD/trimming, 2M transient-splat cap, max training
image dimension 1280, native RTX 4060 Laptop / driver 615.78.08 (**not H_R/H_V**).
No warmup/repetition distribution is claimed. Actual stage-event times: source
decode **172.210 s**, journal PNG conversion **132.127 s**, solve/export/undistort
**2,092.566 s**. Fit cancellation observes **42.655 s** at step 1,101; resumed fit
reports **276.722 s**, and its whole native command takes **373.023 s**.
Active initial/resume command time totals **2,896.992 s**; wall to publication
including the validator repair is **3,314.284 s**. Completed fresh-request reuse
takes **46.527 s**. These are local development timings, not the prescribed P1
benchmark; no target is weakened. CPU suites/tidy ran during resume; desktop,
clocks and thermal state were uncontrolled. Fit-reported CUDA peaks are
**779,344,896 allocated / 956,301,312 reserved bytes**, scoped to the resumed fit.
Raw per-stage/event/holdout data remain in `results.json` and original logs.

## Commands and identities

GCC 16.2.1 C++26; Clang/format/tidy 23.1.1 explicit C++23/libstdc++;
Python worker 3.12.15 / pycolmap 4.2.1; fit Python 3.10.22, torch 2.4.1+cu124,
gsplat 1.5.3+pt24cu124, CUDA 12.4. Native command control uses FlatBuffers 2.0.

| Actual command | Result / evidence in R |
|---|---|
| `cmake --build --preset dev -j 4`; `cmake --build build/clang-viewer-cxx23 -j 4` | Exit 0, `build-fixed-dev/clang.log` |
| `build/dev/we_core_tests '[stage-chain]'` | Exit 0, **286 assertions / seven cases**, `focused-fixed.log` |
| `ctest --preset dev`; `ctest --test-dir build/clang-viewer-cxx23 --output-on-failure` | Exit 0, **99/99 each**, `ctest-dev/clang.log` |
| `python3 $R/run_chain.py`; `python3 $R/continue_chain.py` | First supervisor exits 1 on the native resume rejection after expected cancellation; repaired resume/reuse exits 0; both summary/log sets retained |
| `.venv-train/bin/python $R/verify_chain.py`; `build/dev/we_process validate $J/scene.wexplore`; `build/dev/we_process inspect-map $J/map`; `build/dev/we_process reconstruct --request $J/adopt.json` | Exit 0; byte/inventory audit, checked scene/map/adoption logs |
| `python3 $R/run_native_checks.py` | Exit 0; versioned arguments in `native-checks.json`: native calibrated captures, pinned CUDA comparison, viewer background replacement |
| `clang-tidy -p build/dev src/application/reconstruction_job.cpp tests/unit/reconstruction_job_tests.cpp --warnings-as-errors='*'` | Exit 0, no first-party diagnostics, `tidy-final.log` |
| `clang-format --dry-run --Werror` on the two changed C++ files; `python3 tools/check_boundaries.py`; `python3 tools/check_docs.py`; `git diff --check` | Exit 0; format/boundary/docs/diff logs |

Initial regression compilation exposed a Catch INFO conditional-parentheses
mistake; corrected without weakening warnings. Failing build/tidy logs and the
older-binary focused run (no matching new cases, not evidence) remain retained.

Fit identity `20879b8e5b28a2b731405eb77a75ffa99ac39917cbede2d0d3510c18196fb13c`;
immutable PLY `ad8266239d40e32dda43b8874700f5ff28f354eaaf67416946a4095c4b9a1874`;
canonical pose asset `e25d0c01092a9ac29fc924bef2d07028c5b999ab86646d16babb7db4ead20682`;
scene manifest `a2d7ae9eb113faed48e6a7d1ee3c4b7f8819d9049bd183509340a833b4d08720`.
`identity.json` hashes source/binaries, requests, runners and evidence; SHA-256
308a1f7c157212f7196a372792304bd743bce6f2bbd056ad411d8858694e90cf. Before/fixed identities and **329 unchanged protected files** are
recorded; AGENTS, runbook and specification are unchanged. All media, assets,
checkpoints, captures and large logs stay ignored/local.

Remaining: prescribed ≤250k fixture (this full result exceeds it), H_R/H_V/H_M,
native Windows/macOS/AMD/release, complete gates/corpus and independent quality.
GUI processing status still lacks typed stage/progress propagation; full camera
parity, blur/cut/lens/coverage diagnostics and broader failure stress remain pending.
