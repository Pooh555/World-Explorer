# World Explorer

Native C++/Vulkan viewer and local video reconstruction. **M3 is active; this is
not a Phase 1 release.** M1/M2 have scoped Linux development exits;
[progress](docs/progress.md) and [requirements](docs/requirements.md) track gaps.

The [approved asset goal](docs/technical_plan.md#11-offline-asset-production-track)
adds reusable objects/structure, hidden-surface completion and relightable PBR
exported as glTF/GLB: A0–A5 follow M3 before M4, first visible result in A1;
A6 engine packaging follows M6. All are NOT_STARTED. Current splat/captured
appearance does not provide these assets; M7–M8 live work remains deferred.

## Build on the tested Arch host

Requires CMake ≥3.30, Ninja, C++26 compiler/library with `std::expected`, OpenSSL,
SDL3, Vulkan headers/loader and `glslc`. Dev downloads SHA-256-pinned ImGui, JSON
and Catch2 unless cached in `.deps/`. CPU/worker tests need Python 3 and
FFmpeg/ffprobe on PATH, including native PNG-to-RGB admission tests.

```sh
cmake --preset dev
cmake --build --preset dev -j 4
ctest --preset dev
build/dev/we_viewer
```

| Preset | Scope |
|---|---|
| `cpu` | Omits SDL/Vulkan |
| `compat-cxx23` | Explicit C++23 compatibility |
| `clang`, `asan`, `tsan` | Independent verification configurations |
| `release-vcpkg` | Unqualified release dependency configuration; Windows/macOS viewer/release profiles remain unqualified |
| `coverage` | Isolated Clang C++26 CPU instrumentation; after building, `cmake --build --preset coverage --target we_coverage_report` runs fresh tests and emits LLVM line/branch/package-update reports. [Protocol](docs/protocols/coverage.md) defines exclusions, evidence and `we_coverage_check` enforcing unchanged E1 targets |
| `fuzz` | Isolated Clang C++26 libFuzzer + ASan/UBSan PLY diagnostic; [protocol](docs/protocols/fuzzing.md) defines bounded synthetic campaigns and crash retention under ignored directories |

`native-builds` checks explicit C++23 CPU builds: Ubuntu GCC 13 or Clang 19 with
libstdc++ 13; Windows MSVC/runner OpenSSL SDK; macOS LLVM 20/matching libc++ and
Homebrew OpenSSL. C++23 alone cannot supply missing library APIs. macOS CI does
not qualify Apple Clang or Vulkan. All jobs check FFmpeg/ffprobe; Windows installs
Chocolatey FFmpeg 7.1.1. See [toolchain decision](docs/adr/002-native-ci-toolchains.md),
[initial repairs](docs/validation/2026-10-11-ci.md) and
[portability follow-up](docs/validation/2026-10-11-ci-followup.md).

## Reconstruct and review a video

`python3 tools/new_video_job.py data/local/room.mp4` accepts a video anywhere in
`data/`, creates/prints `artifacts/<video-name>/request.json` and starts no work.
Use its printed request with the shared GUI/CLI coordinator:

```sh
build/dev/we_process reconstruct --request artifacts/reconstruction.json
build/dev/we_viewer --reconstruction-request artifacts/reconstruction.json --max-splats 500000
```

`build/dev/we_viewer --review-request REQUEST_PATH` reviews without reconstruction
while viewing stays active. Import shows actions/limits/current output free space;
selected retained inspection supplies checked metadata, while automatic inspection
leaves metadata pending until Run. The full planned preflight UI is incomplete.
The [artifact protocol](docs/protocols/artifacts.md) defines retained scenes,
limits and repeat-attempt naming; `data/` and `artifacts/` are Git-ignored.

The [job schema](docs/protocols/jobs.md) defines fresh fitting, checkpoint resume
and explicit completed-fit reuse. Schema 2 owns automatic metadata inspection or
adopts a checksummed inspection, checks metadata/storage before pose preparation,
and supports explicit fresh decode/solve with immutable owned jobs and checked
RGB replay on restart. Fresh end-to-end real-chain qualification remains pending;
the development recipe does not certify quality.

## View a scene

```sh
mkdir -p artifacts/diagnostics/gaussian-fixture artifacts/diagnostics/gpu-overlap
build/dev/we_process fixture artifacts/diagnostics/gaussian-fixture/scene.wexplore
build/dev/we_process validate artifacts/diagnostics/gaussian-fixture/scene.wexplore
build/dev/we_viewer --scene artifacts/diagnostics/gaussian-fixture/scene.wexplore
build/dev/we_gpu_smoke artifacts/diagnostics/gpu-overlap/readback.ppm
```

These are synthetic rendering checks. The separate
[frozen portable fixture](docs/protocols/viewer.md#frozen-portable-viewer-fixture)
uses `we_viewer_fixture`/`we_gpu_portable_fixture`: 250,000 splats, MIT notice,
fixed camera and hashes; load/readback only, no FPS measurement. Outputs belong
under `artifacts/`. For real data, `compile` accepts the worker's explicitly
annotated gsplat-1.5.3 ASCII PLY profile:

```sh
build/dev/we_process compile /path/to/appearance.ply artifacts/capture.wexplore
```

Click the viewport to capture the mouse; Escape/focus loss releases it.
New/reset bindings: WASD movement, Space/Ctrl vertical, Shift acceleration, wheel
speed. Saved bindings persist; the overlay shows active keys, and Settings remaps
keys/resets panels. Capture never persists. Session bookmarks restore pose/FOV;
UI scale follows the display and resized Import stays within the viewport.
Open/import, background loading, logs and Cancel are available. Dear ImGui has no
screen-reader integration here. Walking needs independent scale and supported
collision geometry, both unavailable.

## Pose workers and retained data

Install `workers/pose-requirements.txt` explicitly in isolated Python 3.12:
official pycolmap 4.2.1 needs no separate COLMAP executable. FFmpeg/ffprobe must
be on PATH. Processing never uploads media or silently installs dependencies.

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

Compatible requests reuse checksummed completed stages; failed stages stay
incomplete and committed output is preserved. Recovery can select
`pose_recipe: exhaustive_guided`; retain the original result and extra cost.
Neither recipe guarantees low-parallax reconstruction.

Shared workers use verified [FlatBuffers 2.0 START/CANCEL](docs/protocols/worker.md).
Cancellation requests checkpointing before process-tree termination; direct Python
invocation retains JSONL diagnostics. Upgrade native protocol-1 workers explicitly;
existing completed assets remain readable.

| Command | Behavior / protection |
|---|---|
| `build/dev/we_process replay-frames DECODE_STAGE VIDEO` | Checks committed decode references and prints original timestamps/RGB digests; no reconstruction rerun. [Capture contract](docs/protocols/capture.md) |
| `build/dev/we_process replay-poses POSE_JOB VIDEO [--decode-stage DIR]` | Completed-output replay: checks poses/calibration, replays real RGB leases through the estimator, publishes an in-memory map. Explicit stage handles a stale decode pointer without modifying it. [Pose contract](docs/protocols/poses.md) |
| `build/dev/we_process export-poses POSE_JOB VIDEO OUTPUT.weposes [--decode-stage DIR]` | Checked canonical FlatBuffers export; no solver/decoder/optimizer. Parent must exist; existing destination preserved. Images stay in the job; arbitrary scale/unknown gravity; interchange, not resumable map log |
| `build/dev/we_process inspect-poses OUTPUT.weposes SHA256` | Reloads immutable snapshot with the export's reported SHA-256 and prints JSON |
| `build/dev/we_process prepare-poses POSE_JOB VIDEO MAP_DIRECTORY [--decode-stage DIR]` | Imports checked retained poses into a durable map; identical dependency reuse is idempotent, corrected/different maps need new output or explicit resync |
| `build/dev/we_process solve-poses DECODE_STAGE VIDEO NEW_JOB --pose-python EXECUTABLE` | Fresh isolated CPU COLMAP solve from journaled native RGB leases; fresh output, retained failure diagnostics. Publishes canonical poses and `NEW_JOB.map`, preserves original job and does not fit. Optional `--recipe exhaustive_guided`; automatic video-to-fit chaining remains pending |

Persistent maps retain snapshots/retry receipts across restart:

```sh
build/dev/we_process init-map NEW_MAP_DIRECTORY
build/dev/we_process import-map INPUT.weposes SHA256 MAP_DIRECTORY
build/dev/we_process inspect-map MAP_DIRECTORY
```

Use fresh output/existing parent; import copies the checked canonical source,
preserves the capture/job and is idempotent. Corrupt/missing manifests never
silently reset maps. [Map contract](docs/protocols/maps.md) covers writer ownership,
cancellation, retention budgets and recovery. Scale stays arbitrary; no walking.

The GUI/CLI coordinator prepares maps before fitting: explicit `map_directory`
or sibling `FIT_DIRECTORY.map`. It checks fit cameras and attaches map/source
dependencies to the scene; see the [job contract](docs/protocols/jobs.md).

## Appearance and depth

[gsplat worker setup](workers/gsplat/README.md) defines the pinned local development
environment and `we_process fit` / `compile-fit`. Open the result with
`we_viewer --scene OUTPUT.wexplore --source-view --max-splats 500000`.
Source view explicitly resets the independent spectator upright; it does not
follow capture poses. Admit larger scenes with explicit budgets; defaults remain
250k splats/512 MiB.

Standard is 7k steps; quality is 30k. The upstream benchmark adapter needs its
separate environment/reviewed metric weights; Q2 unlimited remains unimplemented.
Development resize/temporal holdout does not certify Q2/Q3 or reference platforms.

`build/dev/we_process inspect-depth INPUT.wedepth SHA256 [--max-dimension N]`
checks format/provenance only; [depth contract](docs/protocols/depth.md).
`we_process prepare-dense` checks/publishes calibrated input metadata without
computing depth/changing scenes; [dense-input arguments/recipe/grid limits](docs/protocols/dense_inputs.md).
The [separate CUDA pilot](workers/dense/README.md) produces partial checked depth;
production dense reconstruction/walking remain unavailable.

Sources/intermediates stay local until the user removes their job. Never commit
raw captures or diagnostic images; `sample.mp4`, caches, scenes and `artifacts/`
are ignored. [Specification](docs/technical_plan.md) owns acceptance gates;
[documentation map](docs/README.md) links contracts/evidence and
[documentation standards](docs/protocols/documentation.md). Check local links with
`python3 tools/check_docs.py`.
