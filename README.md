# World Explorer

Native C++/Vulkan scene viewer and local video reconstruction work in progress.
**M1 is incomplete; this is not a Phase 1 release.** See [progress](docs/progress.md)
and the [requirement matrix](docs/requirements.md) for verified behavior and gaps.

## Build on the tested Arch development host

Requires CMake ≥3.30, Ninja, a compiler/library supporting C++26 and `std::expected`,
OpenSSL, SDL3, Vulkan headers/loader and `glslc`. The development preset downloads
SHA-256-pinned ImGui, JSON and Catch2 sources unless present in `.deps/`.

```sh
cmake --preset dev
cmake --build --preset dev -j 4
ctest --preset dev
build/dev/we_viewer
```

`cpu` builds without SDL/Vulkan; `compat-cxx23` explicitly selects C++23.
`clang`, `asan` and `tsan` provide separate verification configurations.
`release-vcpkg` is an unqualified release dependency configuration, not a certified
package. Native Windows/macOS viewer and release profiles remain unqualified.

The `native-builds` workflow checks the explicit C++23 CPU profile with GCC 13
and Clang 19/libstdc++ 13 on Ubuntu, MSVC with the runner's OpenSSL SDK on Windows,
and LLVM 20 with its matching libc++ and Homebrew OpenSSL on macOS. Selecting
C++23 alone does not provide missing standard-library APIs. The macOS CPU job
does not qualify Apple Clang or the Vulkan viewer; see the
[toolchain decision](docs/adr/002-native-ci-toolchains.md) and
[CI repair checks](docs/validation/2026-10-11-ci.md) and
[follow-up portability checks](docs/validation/2026-10-11-ci-followup.md).

GUI and CLI share a command to continue completed poses through explicit local
fitting, checked publication and viewer loading:

```sh
build/dev/we_process reconstruct --request artifacts/reconstruction.json
build/dev/we_viewer --reconstruction-request artifacts/reconstruction.json --max-splats 500000
```

See the [job schema](docs/protocols/jobs.md) for fresh fitting, checkpoint resume
and explicit completed-fit reuse. Schema 2 can run automatic local metadata
inspection in an owned job or adopt a checksummed retained inspection, then check
metadata/current output storage before preparing poses. Explicit fresh decode/solve
actions use owned jobs, immutable requests and checked RGB replay on restart.
Fresh end-to-end real-chain qualification remains pending. The development recipe
does not certify quality.

## View a scene

```sh
build/dev/we_process fixture artifacts/diagnostic.wexplore
build/dev/we_process validate artifacts/diagnostic.wexplore
build/dev/we_viewer --scene artifacts/diagnostic.wexplore
build/dev/we_gpu_smoke artifacts/overlap.ppm
```

The fixture is synthetic and tests rendering only. For a real scene, `compile`
accepts the explicitly annotated gsplat-1.5.3 ASCII PLY profile emitted by our worker:

```sh
build/dev/we_process compile /path/to/appearance.ply artifacts/capture.wexplore
```

Click the viewport to capture the mouse. WASD moves, Space/Shift moves vertically,
Ctrl accelerates, the wheel changes speed and Escape releases the mouse. Focus
loss releases capture. Settings remap keys and reset panels. Walking is disabled
because independent scale and supported collision geometry are not implemented.
Bookmarks are session-local. Settings persist but input capture never does.

The interface has open/import dialogs, background scene loading, worker logs and
cancellation. Import currently accepts a configured job JSON under Advanced;
the full planned preflight and review UI remains incomplete. Dear ImGui currently
has no screen-reader integration in this application.

## Process a local video

FFmpeg/ffprobe must be on PATH. Install `workers/pose-requirements.txt` into an
isolated Python 3.12 environment. This installs official pycolmap 4.2.1; a separate
COLMAP executable is not required. Dependency installation is explicit, and
processing does not upload media or silently install packages.

```json
{
  "schema_version": 1,
  "source": "/absolute/path/to/video.mp4",
  "job_dir": "/absolute/path/to/new-job",
  "pose_python": "/absolute/path/to/venv/bin/python",
  "sample_fps": 3,
  "preset": "standard",
  "stop_after": "poses"
}
```

```sh
build/dev/we_process run --request job.json --worker-python /path/to/venv/bin/python
```

Completed stages are checksummed and reused by compatible requests. Failed stages
remain explicitly incomplete; committed output is preserved. A recovery request
can set `pose_recipe` to `exhaustive_guided`; retain the original result and record
the extra cost. Neither recipe guarantees reconstruction for low-parallax footage.

The native frame-source inspection path can replay a committed decode stage:
`build/dev/we_process replay-frames DECODE_STAGE VIDEO`. It checks image references
and prints original timestamps and RGB digests without rerunning reconstruction.
See the [capture contract](docs/protocols/capture.md). Shared worker commands now
use verified [FlatBuffers 2.0 START/CANCEL commands and events](docs/protocols/worker.md).
Cancellation requests the existing checkpoint hook before enforcing process-tree
termination; direct Python invocation retains JSONL diagnostics. Native protocol
1 workers must be upgraded explicitly. Existing completed assets remain readable.

`build/dev/we_process replay-poses POSE_JOB VIDEO [--decode-stage DIR]` checks
completed poses/calibration, replays real frame leases through the native estimator
contract and publishes an in-memory map snapshot. It is labeled completed-output
replay. The optional stage selection
handles a stale decode pointer without changing it. See [pose replay](docs/protocols/poses.md).

`build/dev/we_process export-poses POSE_JOB VIDEO OUTPUT.weposes [--decode-stage DIR]`
verifies retained poses and writes a canonical FlatBuffers asset without running
a solver, decoder or optimizer. The destination parent must exist; existing files
are preserved. Save the reported SHA-256 and use
`build/dev/we_process inspect-poses OUTPUT.weposes SHA256` to reload the checked
immutable snapshot and print JSON inspection. Images stay in the original local
job. Scale remains arbitrary and gravity unknown; this file is pose interchange,
not a resumable map-store log.

Persistent native maps now retain snapshots and retry receipts across restart:

```sh
build/dev/we_process init-map NEW_MAP_DIRECTORY
build/dev/we_process import-map INPUT.weposes SHA256 MAP_DIRECTORY
build/dev/we_process inspect-map MAP_DIRECTORY
```

Use a fresh output directory with an existing parent. The checked canonical source
is copied into the store; the original capture/job is preserved. Repeating an import
is idempotent, and corrupt/missing manifests never silently reset the map. See the
[map contract](docs/protocols/maps.md) for writer ownership, cancellation, retention
budgets and recovery limits. Scale remains arbitrary; walking is unavailable.

`build/dev/we_process prepare-poses POSE_JOB VIDEO MAP_DIRECTORY [--decode-stage DIR]`
combines checked retained-pose import and durable map preparation. Repeating it
reuses the identical committed dependency; corrected/different maps require a new
output or explicit resync. The shared GUI/CLI reconstruction coordinator now
performs this step before fitting, using optional `map_directory` or the default
sibling `FIT_DIRECTORY.map`. It checks fit camera poses and attaches map/source
dependencies to the scene. See the [job contract](docs/protocols/jobs.md).

`build/dev/we_process solve-poses DECODE_STAGE VIDEO NEW_JOB --pose-python EXECUTABLE`
runs a fresh isolated CPU COLMAP solve from native RGB leases. Use a fresh output
directory; accepted frames are journaled locally, and failures retain diagnostics.
The command publishes canonical poses and a durable map at the fresh sibling
`NEW_JOB.map` and does not fit appearance or modify
the original completed job. An explicit `--recipe exhaustive_guided` selects the
existing recovery recipe. Automatic video-to-fit chaining remains pending.

The explicitly named local development fit now works with real gsplat output;
see [worker setup](workers/gsplat/README.md) for its pinned environment and native
`we_process fit` / `compile-fit` commands. Open a resulting scene with
`we_viewer --scene OUTPUT.wexplore --source-view --max-splats 500000`.
“Source view” is an explicit upright spectator reset; it does not follow capture
poses. Renderer budgets are configurable, and a larger scene must be admitted
under an explicit budget. The default remains 250k splats/512 MiB.

The prescribed upstream benchmark adapter still needs its separate environment
and reviewed metric weights. Standard is 7k steps; quality is 30k. The development
fit's explicit resize and temporal holdout do not certify Q2/Q3 or reference
hardware/platform targets. The Q2 unlimited benchmark recipe remains unimplemented.

Sources and intermediates remain local until the user removes their selected job
directory. `sample.mp4`, dependency caches, generated scenes and `artifacts/` are
ignored by Git. Raw captures and their diagnostic images must never be committed.

The full design and acceptance gates are in [the specification](docs/technical_plan.md).
