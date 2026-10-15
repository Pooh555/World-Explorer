# M2 native update and resize lifetime stress — 2026-10-15 Asia/Hong_Kong

Scope/status: **M2 IN_PROGRESS**. Parent `a503edf`; this report accompanies the
local GPU diagnostic implementation. Cwd for commands: repository root. Native
Linux Wayland, NVIDIA GeForce RTX 4060 Laptop GPU, driver 615.78.08, with Vulkan
validation. The
read-only input is `artifacts/sample_1/scene.wexplore`, with 295,952 splats,
manifest SHA-256
`327fa72f74b62eb9dc6537ac6726f9d25cafba791d260f79cc37de9fb8f7863d`,
and scene content SHA-256
`c5f19d74260fecbcdcd26ff7b00d57e25006c8e2e572ef2ed0f7f1de3defee2d`.
No video, pose, fit, saved scene or frozen fixture was changed.

`we_gpu_lifetime_stress` loads the retained scene and source camera, requires
Vulkan validation, alternates the full scene and an empty synthetic revision
every 15 seconds through ordinary update admission, and requests four extents
every 20 seconds. It verifies the resident splat count against the visible
revision, checks tracked owned bytes against the 512 MiB renderer budget, and
compares periodic 1280×720 full-scene PPM SHA-256 values with the baseline.
Flushed JSONL events record revisions, resizes, readbacks, Vulkan validation
errors, completed timeline, tracked owned bytes and Linux process RSS/HWM.

| Actual command / observation | Result |
|---|---|
| `cmake --build --preset dev -j 4`; `ctest --preset dev --output-on-failure` | Exit 0; GCC C++26 build, 267/267 CPU and worker checks pass |
| `build/dev/we_gpu_lifetime_stress artifacts/sample_1/scene.wexplore artifacts/validation/m2-lifetime-20261015-refactor-check 45` | Exit 0, native validation; two visible updates, two resizes, two matching captures, 1,820 frames and zero Vulkan errors. Final owned peak 301,703,552 bytes; process peak RSS 440,979,456 bytes |
| `build/dev/we_gpu_lifetime_stress artifacts/sample_1/scene.wexplore artifacts/validation/m2-lifetime-20261015-full-v2 7200 > artifacts/validation/m2-lifetime-20261015-full-v2.log 2>&1` | Exit 0, native validation; **7,203.746 s**, 480 visible updates through revision 481, 359 resizes, 13 matching captures (baseline, 11 periodic, final), 270,414 submitted frames, 119 minute samples and zero Vulkan validation errors. Tracked peak owned bytes 369,408,864 (under 512 MiB); process peak RSS 499,478,528 bytes. Last minute sample reports 293,645,136 owned bytes and 321,462,272 RSS bytes |
| `sha256sum artifacts/validation/m2-lifetime-20261015-full-v2/capture-*.ppm`; `sha256sum artifacts/sample_1/scene.wexplore/manifest.json` | Exit 0; all 13 PPM hashes identical and saved manifest hash unchanged |
| `clang-format --dry-run --Werror tests/gpu/lifetime_stress.cpp`; `clang-tidy -p build/dev tests/gpu/lifetime_stress.cpp` | Exit 0; no first-party tidy warnings |
| `cmake --build build/m2-no-fault -j 4 --target we_gpu_lifetime_stress` | Exit 0; Release GCC C++23 system-dependency viewer target built. The existing `compat-cxx23` directory has `WE_BUILD_VIEWER=OFF`, so its target lookup exits 1; this is not a GPU compatibility failure |
| `SDL_VIDEODRIVER=x11 build/dev/we_gpu_smoke artifacts/validation/m2-lifetime-20261015-x11/smoke.ppm` | Exit 2 before a minimize assertion: `SDL_SyncWindow` after the minimize request returned false; a debugger showed window flags `0x10000220`, without `SDL_WINDOW_MINIMIZED` (`0x40`). This X11 path supplies no accepted-minimize evidence |

The 13 PPMs share SHA-256
`107ceeb0c2cb975041af83db2283e0d6f3673c06d8503f1109c9cd3955036613`.
The full run's raw `events.jsonl`, `summary.json`, PPMs and redirected log are in
`artifacts/validation/m2-lifetime-20261015-full-v2/` and its adjacent `.log`.
The event log has 479 periodic revision records; the final full-scene revision
481 is awaited and counted in the summary before its matching final readback.
The manifest SHA-256 above was rechecked after the run and remained unchanged.
The 119 samples report zero Vulkan validation errors; owned bytes reached a
steady 293,645,136 at the same sampled empty-scene phase after growth and at
the last sample. Process HWM is a process measure, not GPU VRAM.

The 60-second native pilot initially passed with four updates, two resizes, two
matching captures, 2,520 frames and zero Vulkan errors. Its source lacked later
process-RSS sampling and tidy corrections. A first long run using that earlier
source was intentionally stopped after about 105 seconds while correcting
first-party static-analysis warnings. The interrupted log is retained at
`artifacts/validation/m2-lifetime-20261015-full/`; exit 137 is **not** a passed
stress check. The 45-second source-matched pilot above passed after the
corrections. Raw outputs and logs are ignored under `artifacts/validation/`.

The native stress is a local diagnostic for this scene, camera, GPU and display
server. It does not measure total device VRAM, reference hardware, Q6 packing,
P2/P4/P6 performance or N1 latency. Empty revisions are synthetic. A separate
accepted minimize/restore, physical device loss and VMA allocation integration
remain open; the full M2 gate is not certified.
