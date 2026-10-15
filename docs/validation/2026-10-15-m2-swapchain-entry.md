# M2 swapchain recovery entry — 2026-10-15 Asia/Hong_Kong

Scope/status: **M2 IN_PROGRESS** on the Linux RTX 4060 Laptop development host.
This is a current native update/resize baseline and code review, not device-loss,
long-stress, reference-performance or cross-platform qualification. Cwd:
repository root; reviewed source commit `788db85`. Raw ignored root **R**:
`artifacts/validation/m2-swapchain-entry-20261015/`.

`cmake --preset dev` and `cmake --build --preset dev -j 4` succeeded in the
companion [M1 review](2026-10-15-m1-exit-review.md). The first
`build/dev/we_gpu_smoke R/smoke.ppm` exited **2** inside the sandbox with an
empty GPU-verification error; that is driver-access failure, not evidence of an
absent GPU. The same existing binary then ran with native device access:
`build/dev/we_gpu_smoke R/smoke-native.ppm` exited **0**. It reported **51 frames**,
the expected overlap pixel **130,3,68**, replace/delete/resize passing, zero
Vulkan validation errors, and rejected framebuffer, composition-scratch and
tile-reference over-budget admissions. R/smoke-native.log and the PPM retain the
raw result. Binary SHA-256:
`f0707c719ffec3d36abe11714344333f6a393722d6fd333dbc02b37b3e95f319`.

Code inspection found a recovery risk in
`src/render_vulkan/vulkan_swapchain.cpp::create`: on resize it waits idle and
destroys the previous swapchain before the new surface queries, swapchain,
views, framebuffers and semaphores have all succeeded. In
`src/render_vulkan/renderer.cpp::refresh_swapchain`, composition scratch is also
released before recreation succeeds. A later allocation or WSI failure can leave
the last visible scene without a renderable presentation path. Existing native
smoke checks normal resize; it does not inject such a failure. No source change or
device-loss recovery claim is made here.

Next work is to stage replacement swapchain and composition resources before
committing a new presentation state, with a bounded native failure-injection
check and resize/minimize/restore regression. Preserve scene/update revision and
caller assets across rejected recreation; follow Vulkan old-swapchain retirement
rules rather than assuming an old swapchain stays presentable after a successful
replacement create. The [progress handoff](../progress.md) owns this task.
