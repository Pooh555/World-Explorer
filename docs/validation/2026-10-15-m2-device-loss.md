# M2 renderer recreation after device loss — 2026-10-15 Asia/Hong_Kong

Scope/status: **M2 IN_PROGRESS**. Parent `dc24fad6503113c7280c868b0dfe864865582795`;
this report accompanies the local implementation. Cwd: repository root. Native
Linux Wayland, NVIDIA GeForce RTX 4060 Laptop GPU, driver 615.78.08, Vulkan
validation. Raw ignored outputs: `artifacts/validation/m2-device-loss-20261015/`.
Saved `sample_1` scene manifest SHA-256:
`327fa72f74b62eb9dc6537ac6726f9d25cafba791d260f79cc37de9fb8f7863d`.
No video, pose, fit, scene or frozen fixture was changed.

`VK_ERROR_DEVICE_LOST` now crosses the renderer boundary as `DEVICE_LOST` rather
than generic backend failure. The viewer shuts down the old ImGui Vulkan backend,
destroys its renderer, creates a fresh renderer/backend and replays the last
admitted immutable CPU snapshot through full-snapshot update admission. The new
ticket gates visibility. A second loss fails explicitly. Device-loss creation and
idle errors also retain their Vulkan result type. The diagnostic hook exists only
in Debug testing builds; it reports a synthetic loss at the render owner's
retirement-timeline query. It first drains real submissions because the test GPU
is healthy. It does **not** prove recovery after physical device loss.

| Actual command / observation | Result |
|---|---|
| `cmake --build --preset dev -j 4`; `ctest --preset dev` | Exit 0; GCC 16.2.1 C++26 viewer/CLI/tests built, 267/267 CPU and worker checks pass |
| `build/dev/we_gpu_smoke R/smoke.ppm`; same with `R/smoke-timings.ppm --timings` | Both exit 0; synthetic loss typed, renderer recreated, revision 1/two splats visible, recovered overlap center RGB8 exactly `130,3,68`, valid PPM, zero Vulkan validation errors; existing 51-frame replace/delete/resize and post-create recovery pass |
| `build/dev/we_viewer --scene artifacts/sample_1/scene.wexplore --max-splats 400000 --frames 80 --inject-device-loss-frame 30 --screenshot R/viewer2-after-loss.ppm --input-trace R/viewer2-trace.jsonl --settings R/viewer2-settings.json` | Exit 0 with native display access; full 295,952-splat scene first visible at trace frame 134, injected recovery at 135, visible again at 136; post-recovery 1428×1746 PPM written; zero Vulkan validation messages. The frame limit is a minimum while asynchronous load/ticket work remains |
| `build/dev/we_viewer --scene artifacts/sample_1/scene.wexplore --max-splats 400000 --frames 80 --screenshot R/viewer-baseline.ppm --settings R/viewer-baseline-settings.json` | Exit 0; 1428×1746 native baseline PPM is byte-identical to the post-recovery PPM, including scene and overlay pixels; zero Vulkan validation messages |
| `cmake --build build/m2-no-fault -j 4`; `build/m2-no-fault/we_gpu_smoke R/release-cxx23-smoke.ppm`; `nm -C build/m2-no-fault/librender_vulkan.a` | Exit 0; Release GCC C++23 system-dependency build and native 51-frame smoke pass with zero Vulkan errors; no synthetic-loss hook symbol. This is not the separate `release-vcpkg` qualification |
| `clang-format --dry-run --Werror` 23.1.1 on changed C++; `clang-tidy -p build/dev` on renderer/viewer/smoke; `python3 tools/check_docs.py`; `git diff --check` | Exit 0; no reported first-party tidy diagnostics, 578 local documentation targets checked, no whitespace errors |

`R` above is `artifacts/validation/m2-device-loss-20261015`. The recovered
synthetic readback SHA-256 is
`0ba0b8ce5b5aea379a2ff0c4350813d51620728ecb586b3e40a80e3577296c64`;
the post-recovery and baseline real-viewer PPM SHA-256 is
`da08c055d613f529efac62daa34c573f4cd633ebfc33db1bd8d458d14776f88`.
These ignored images stay local.

An initial native viewer attempt omitted the 400,000-splat budget and could not
admit the saved scene. Its synthetic fault also tore down a healthy test device
with outstanding submissions, producing Vulkan lifetime validation errors. The
first attempt exited 2; `R/viewer.log` and `R/viewer-trace.jsonl` retain it.
The hook now drains those submissions, and the second run uses the required
budget and passes. A sandboxed Release smoke also exited 2 before GPU loading;
the native-access rerun passed. The Wayland compositor still denied minimize;
that behavior remains unverified. Actual device-loss hardware behavior, a
two-hour update/resize stress run, VMA allocation integration and full Q/P/N/E
qualification remain open.
