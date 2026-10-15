# M2 retired-swapchain recovery — 2026-10-15 Asia/Hong_Kong

Scope/status: **M2 IN_PROGRESS**, native Linux Wayland / NVIDIA GeForce RTX
4060 Laptop GPU with Vulkan validation. Parent `c561524`; this report accompanies
the local implementation slice. Cwd: repository root. Raw ignored root **R**:
`artifacts/validation/m2-postcreate-recovery-20261015/`. Saved sample_1 scene
content SHA-256 remains
`c5f19d74260fecbcdcd26ff7b00d57e25006c8e2e572ef2ed0f7f1de3defee2d`.
No source video, pose/fit stage, scene asset or frozen fixture changed.

The [Vulkan swapchain specification](https://docs.vulkan.org/refpages/latest/refpages/source/VkSwapchainCreateInfoKHR.html)
states that passing `oldSwapchain` retires it when `vkCreateSwapchainKHR` is called,
**even if creation fails**. This corrects the successful-create-only wording in
the [preceding report](2026-10-15-m2-swapchain-recovery.md). After any attempted
handoff failure, the renderer now destroys the retired presentation, clears its
borrowed image/framebuffer/semaphore views and exposes zero presentation images.
The next draw recreates the swapchain with `oldSwapchain = VK_NULL_HANDLE`; the
immutable scene revision and GPU scratch stay owned. Failures before the handoff
still preserve the old presentation. A one-shot fault immediately after successful
creation is compiled only in Debug testing builds. It has no application
setting or release-binary entry point.

Checks actually run:

| Command / output | Result |
|---|---|
| `cmake --preset dev`; `cmake --build --preset dev -j 4` | Exit 0, GCC 16.2.1 / C++26; native viewer and GPU tools built |
| `ctest --preset dev` | Exit 0; 267/267 CPU, worker and tooling tests |
| `build/dev/we_gpu_smoke R/smoke.ppm` | Exit 0; synthetic two-splat overlap 130,3,68, 51 normal frames, zero Vulkan errors. One injected post-create failure clears presentation; next native draw recreates 640×360 and retains scene revision 1 / two splats. Pre-handoff budget rejection, replacement and deletion still pass |
| `build/dev/we_gpu_smoke R/smoke-timings.ppm --timings` | Exit 0; same recovery path with completed GPU timing checks |
| `build/dev/we_gpu_viewer_regression artifacts/sample_1/scene.wexplore R/viewer-regression.json` | Exit 0; full 295,952-splat scene passes 1280×720, 3840×2160, fullscreen 2880×1800 and restored 1428×1746, all with zero Vulkan validation errors |
| `cmake -S . -B build/m2-no-fault -G Ninja -DCMAKE_BUILD_TYPE=Release -DWE_CXX_STANDARD=23 -DWE_SYSTEM_DEVELOPMENT_DEPS=ON -DWE_BUILD_VIEWER=ON -DBUILD_TESTING=OFF`; `cmake --build build/m2-no-fault -j 4`; `build/m2-no-fault/we_gpu_smoke R/release-cxx23-smoke.ppm` | Exit 0; isolated system-dependency Release C++23 native viewer and 51-frame smoke, overlap 130,3,68 and zero validation errors. `nm -C build/m2-no-fault/librender_vulkan.a` contains no fault-hook symbol. This is not the separate `release-vcpkg` qualification profile |
| `clang-format --dry-run --Werror` on four changed C++ files; `clang-tidy -p build/dev` on three changed translation units; `python3 tools/check_docs.py`; `git diff --check` | Exit 0, no tidy warnings; local link and whitespace checks pass |

The Wayland compositor denied `SDL_MinimizeWindow` despite SDL accepting the
request. The smoke records `minimize/restore denied by compositor`, then checks
that a subsequent draw and the scene remain valid. The code skips frames if SDL
does report a minimized window, but native minimized-state behavior was **not
verified** on this compositor. An optional X11/Xwayland smoke attempt also exited
2 before the fault case with an empty SDL error during hidden-window resize/sync;
its raw log is `R/smoke-x11.log` and it is not a platform pass. Neither result is
evidence of absent GPU hardware.

Remaining M2 work: actual device-loss teardown/recreation with retained CPU scene,
a compositor that grants minimize/restore, extended update/resize stress and
reference Q/P/N/E qualification. The injected one-shot post-create failure does
not simulate a lost Vulkan device or prove native Windows/macOS behavior. VMA
integration remains the later renderer-only M2 allocation task.
