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
package. Native Windows/macOS builds have not been run.

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
