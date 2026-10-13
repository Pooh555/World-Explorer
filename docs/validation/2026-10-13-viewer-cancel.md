# Owned viewer Cancel/reopen — 2026-10-13 Asia/Hong_Kong

Scope: **M1/E1/P6 IN_PROGRESS**, actual owned-thread cancellation/reopening and
pending scene admission on the local native profile. No complete milestone or
Q/P/N/E certification. Cwd: repository root; parent `ea05a4c`; implementation is
the local commit containing this report. Raw ignored evidence
`R = artifacts/validation/viewer-cancel-20261013`. No worker-source/recipe change,
solver/optimizer rerun, dependency installation, upload or remote push.

A new synchronous progress observer reports checked state outside the viewer
mutex and supplies the job stop token. Tests gate the actual coordinator at exact
stages with stop-aware condition variables and bounded deadlines, without timing
sleeps or fake job completion. The existing tiny synthetic reconstruction fixture
is shared with viewer tests; its data/backend limits and admission checks remain.

## Found failure and repair

The first owned-thread run passes cancellation but fails **seven assertions**
across pending load/worker/reconstruction sections: once `busy` clears, a new job
can overwrite a successful publication's status/start time before renderer
admission. All three starts now check both busy and pending snapshot under the
state mutex. Rejection preserves snapshot, status, error and start time. The GUI
Cancel button now signals the common command after releasing the mutex, allowing
synchronous stop callbacks to acquire it safely.

Two new cases cover **seven boundaries** (validation, inspection, poses, map,
completed-fit verification, compilation and post-publication loading), repeated
Cancel, stale-token-free reopening, active/pending rejection and retained bytes.
Cancellation before publication clears active state and leaves prior output;
after atomic publication, the checked scene still awaits renderer admission.
Corrected focused run: **228 assertions/two cases**. Synthetic contract assets do
not certify capture quality or another actual training run.

## Actual checks

GCC 16.2.1 C++26; Clang/format/tidy 23.1.1 explicit C++23/libstdc++.
Linux/Wayland native RTX 4060 Laptop, not H_R/H_V or another native platform.

| Command | Exit / result; raw evidence in R |
|---|---|
| `cmake --build --preset dev -j 4`; `cmake --build build/clang-viewer-cxx23 -j 4` | 0; before/fixed/final build logs; native diagnostic target included |
| `build/dev/we_viewer_tests '[owned-job]'` | First exit 42, **214/221 assertions**; fixed exit 0, **228/228**; `owned-before/fixed.log` |
| `ctest --preset dev`; `ctest --test-dir build/clang-viewer-cxx23 --output-on-failure` | 0; **107/107 each**, `ctest-dev/clang.log` |
| `clang-tidy -p build/dev apps/we_viewer/job_state.cpp apps/we_viewer/main.cpp tests/ui/job_state_tests.cpp tests/unit/reconstruction_job_tests.cpp tests/gpu/reconstruction_handoff.cpp --warnings-as-errors='*'` | 0, no first-party diagnostics; `tidy.log`; final completed-only diagnostic guard rechecked in `tidy-final.log` |
| `clang-format --dry-run --Werror` on changed C++; `python3 tools/check_boundaries.py`; `python3 tools/check_docs.py`; `git diff --check` | 0; format/boundary/docs/diff logs and exact argv in `checks.json` |
| `python3 $R/run_native.py`; `python3 $R/run_native_adopt.py` | 0; exact arguments, native binary hashes and exits in `native*-command.json`; captures/report in `native/` and `native-adopt/` |

The diagnostic accepts only explicit completed-fit reuse with existing completed
poses/fit. The final guarded binary also exercises the worker-independent checked
adoption path. CPU suites/builds/tidy compete with an uncontrolled desktop; no
frame-rate, response-latency or peak-memory certification is inferred.

## Real preserved scenes

Native diagnostic uses the actual viewer job state, the same Cancel command and
normal renderer update/visibility tickets. It loads the retained **304,867-splat**
scene, pauses appearance at checked pose verification, requests stop, then reopens
the fresh job and admits all **297,489 splats**. No ImGui overlay or mouse input is
automated; captures come only from renderer readback on a hidden **1280×720**
surface. The processing capture is deliberately appearance-paused; GPU residency
remains intact. Prior/post-Cancel PPM bytes match exactly, SHA-256
`8d35ccd868f09d5525efef9d8d8a643a29e41c474ce98de44ac94dc730becf57`.
Reopened captures were inspected; appearance, arbitrary scale and absent geometry
retain their established limits. Both native runs report **zero Vulkan errors**
and checked pending admission. Reuse-run final owned buffers **246,352,000 bytes**,
framebuffer **29,491,200 bytes**, 2,778 submitted frames are diagnostic counters,
not aggregate/peak device memory or presented FPS.

Before/after audits preserve **670 identical files**: original media/job/fit/scene,
fresh RGB/stages/map/fit/checkpoints/requests/scene and AGENTS/runbook/specification.
Mutable fresh `capabilities.json` is excluded because cached workers refresh it.
Original scene content is
`35ed89b5abb2ac9eb98b110f1bcd4dab53d1aed70c133623e764ffeffcba01d0`;
fresh content is
`d2470478b71ad072e6d6491b3be49c17e27f45b2105d665d609d9087e821fe2a`.
Fit/manifest/canonical identities remain those in the [fresh-chain report](2026-10-13-fresh-chain.md).
`identity.json` binds current sources, binaries, requests, runners and evidence.
Media/captures/builds remain ignored/local.

Remaining: native GUI mouse/HiDPI/latency, bounded but internally uninterruptible
PLY/bundle operations, broader crash/worker failure stress, prescribed ≤250k
fixture/reference hardware, independent corpus/quality, other platforms and full
gates. These tests do not undo the atomic publication commit boundary.
