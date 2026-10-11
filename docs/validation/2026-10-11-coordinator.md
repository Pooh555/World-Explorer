# Shared job coordinator — 11 October 2026

Parent commit: `6f97412`. Scope: Arch development on the previously recorded
NVIDIA RTX 4060 Laptop GPU. Source, specification, fixture and result identities
are in [the result manifest](2026-10-11-coordinator.json). No capture, checkpoint,
image, generated bundle or dependency cache is committed. No full milestone or
Q/P/N/E gate is certified; M1 stays IN_PROGRESS.

The GUI and CLI now use the same native continuation command. It selects the
development `local_gsplat_v1` recipe explicitly, verifies a completed pose stage,
starts/resumes fitting or adopts an explicitly selected completed fit, checks
inputs again and publishes the checked appearance scene. GUI jobs load the result
with the renderer's limits and submit it through the normal scene update path.
During processing, appearance projection/sorting/drawing pauses; UI presentation
and cancellation stay active, and resident scene assets remain owned.

Request schema and remaining limitations: [jobs.md](../protocols/jobs.md).
The worker's JSONL diagnostic transport, manual pose preparation, native
frame/pose adapter integration and FlatBuffers remain M1 work. Completed reuse
adopts a trusted local fit identity; C++ does not independently recompute the
Python optimizer identity. The original upstream recipe is unchanged.

## Actual real-capture commands

Ignored request `artifacts/sample2-reconstruct.json` selects the existing
`sample2-job`, `sample2-fit-final` and completed fit identity
`0975ef8365bc402e3533a97c97087a425eb71b8cc0644691c8137f413dc235cb`.
It uses `reuse_completed`, 7000 steps, 1280 px and a 2,000,000 splat fitting budget.
Paths resolve against the request directory. Actual commands, exit 0:

```sh
build/dev/we_process reconstruct --request artifacts/sample2-reconstruct.json
build/dev/we_process validate artifacts/sample2-coordinated.wexplore
build/dev/we_viewer --reconstruction-request artifacts/sample2-reconstruct.json --source-view --max-splats 500000 --frames 120 --screenshot artifacts/validation/coordinator-view.ppm --settings artifacts/validation/settings.json
```

The 63/63 connected pose result was fully inventory-checked, never restarted.
The original video SHA remains
`51263d9991dcbc2136d0809ecba1a8ba1b261896f3b222b7afba19784a153ef6`.
The GUI loaded **304867** resident splats and submitted **4195** frames, including
UI frames while the job ran; `--frames 120` is a minimum before idle/visible exit.
Reported renderer allocation bytes: **220342032**, not total process/driver peak.
**Zero Vulkan/synchronization validation errors**, with no shutdown diagnostics.
The private screenshot was inspected and shows the reconstructed room from the
explicit upright source-view reset. This is not a calibrated reference comparison
or a measured performance benchmark.

Actual coordinator resume command, exit 0:

```sh
build/dev/we_process reconstruct --request artifacts/sample2-reconstruct-resume.json
```

That ignored request selects `resume`, the existing final checkpoint,
`.deps/gsplat`, `.venv-train/bin/python`, `workers/gsplat/fit.py`, a fresh
`artifacts/sample2-fit-coordinator` and output
`artifacts/sample2-coordinator-resume.wexplore`. It restored the completed 7000-step
optimizer/RNG checkpoint, performed **zero additional optimization steps**,
evaluated/exported the real scene and reproduced PLY SHA-256
`8067bacb83ae590974a3425f2625e9f6d8ddfc3a5e5621bf28ef2787d45ea81c`.
It then compiled/published automatically. Private log:
`artifacts/validation/coordinator-resume.log`. Actual optimization after an earlier
cancellation is separately documented in the capture report; this check verifies
the new coordinator path, not another training continuation experiment.

Actual budget-failure check, expected exit **2**:

```sh
build/dev/we_viewer --reconstruction-request artifacts/sample2-reconstruct.json --frames 30 --settings artifacts/validation/settings.json
```

The GUI reports the published output path and `chunk element count exceeds budget`
under its default 250k limit. Subsequent CLI validation of that bundle exits 0.
The scene is retained and never trimmed. This fixture still exceeds the prescribed
≤250k M1 fixture requirement. Private log: `coordinator-budget-failure.log`.

## Checks on this slice

Actual build/test commands, exit 0:

```sh
cmake --build --preset dev -j 4
ctest --preset dev --output-junit ../../artifacts/validation/coordinator-dev.xml
cmake --build --preset clang -j 2
ctest --preset clang --output-junit ../../artifacts/validation/coordinator-clang.xml
cmake --build --preset compat-cxx23 -j 2
ctest --preset compat-cxx23 --output-junit ../../artifacts/validation/coordinator-cxx23.xml
cmake --build --preset asan -j 2
ctest --preset asan --output-junit ../../artifacts/validation/coordinator-asan.xml
cmake --build --preset tsan -j 2
ctest --preset tsan --output-junit ../../artifacts/validation/coordinator-tsan.xml
```

**24/24 tests pass in each profile**: GCC C++26 development, Clang C++26 CPU,
explicit C++23 CPU, ASan/UBSan/LSan CPU and separate TSan CPU. Existing CPU-only
cache settings were inspected; the latter profiles are not native GUI passes.
Sanitizer discovery initially failed in the sandbox with the actual LeakSanitizer
`ptrace` diagnostic; the native rerun passed without disabling leak detection.

New regressions check explicit reuse, changed capture/image/configuration/identity,
extra files/escaping pose pointers, output/input overlaps, strict integer limits,
cancellation before publication and at compilation, backend nonzero exit, false
zero-exit completion, argument boundaries for start/resume and input changes
during worker execution. Subprocess fixtures are explicitly synthetic contract
checks; they do not reconstruct a scene. Real fitting evidence is separate above.

Actual native GPU command, exit 0:

```sh
build/dev/we_gpu_smoke artifacts/validation/coordinator-gpu.ppm
```

Result: overlap pixel **130,4,68**, appearance paused/resumed with resident assets
retained, replacement/deletion/resize passed, **51 frames**, zero validation
errors and no shutdown diagnostics. This uses a labeled synthetic correctness
fixture, not the appearance/performance gate.

clang-format **23.1.1** `--dry-run --Werror` passes on all changed first-party C++
files. clang-tidy **23.1.1** ran on coordinator, validation, scene import, source
hashing, process launch, GUI jobs/entry point, CLI and Vulkan renderer with no
unsuppressed first-party diagnostics. No warning policy was weakened.
`python3 tools/check_boundaries.py` and scoped `git diff --check` pass.
Logs/JUnit XML remain ignored under `artifacts/validation/coordinator-*`.

Missing prescribed reference hardware/native Windows/macOS/AMD runners, ≤250k
real fixture qualification, FlatBuffers, complete map/adapters, rights/metric
locks, full corpus/coverage/fuzzing and later geometry/scaling gates remain
unchanged. M7–M8 are deferred. User specification/runbook edits are untouched.
