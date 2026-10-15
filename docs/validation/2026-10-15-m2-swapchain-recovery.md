# M2 staged swapchain resize — 2026-10-15 Asia/Hong_Kong

Scope/status: **M2 IN_PROGRESS**, Linux Wayland / NVIDIA GeForce RTX 4060 Laptop
GPU, native Vulkan validation. Parent source `b2ed78e`; this report and the code
changes form one local implementation slice. Cwd: repository root. Raw ignored
root **R**: `artifacts/validation/m2-swapchain-recovery-20261015/`.
Saved sample_1 scene content SHA-256:
`c5f19d74260fecbcdcd26ff7b00d57e25006c8e2e572ef2ed0f7f1de3defee2d`.
No source video, pose stage, fit, scene asset or frozen fixture was changed.

The renderer now checks prospective extent/dispatch limits and stages only
composition pixel/range buffers that need to grow. Staged bytes count against the
existing residency cap alongside live old buffers. A pre-creation failure leaves
the old swapchain, scratch, tile plan and scene revision owned. The swapchain
builder owns partial views/framebuffers/semaphores through RAII and commits its
complete candidate after creation. Renderer views of its handles are non-owning
spans, avoiding allocations after commit. Vulkan requires `oldSwapchain` on this
Wayland surface: the first attempt with a second independent FIFO swapchain failed
with `Surface already has a fifo` / `VK_ERROR_NATIVE_WINDOW_IN_USE_KHR` and is not
counted as a pass. The corrected handoff uses `oldSwapchain`.

Checks actually run:

| Command / output | Result |
|---|---|
| `cmake --build --preset dev -j 4` | Exit 0; native viewer, CLI/GPU tools rebuilt |
| `ctest --preset dev` | Exit 0; 267/267 CPU, worker and tooling cases |
| `build/dev/we_gpu_smoke R/smoke-native-final.ppm` | Exit 0; 51 frames, overlap pixel 130,3,68, zero Vulkan validation errors; added 512→768 rejected-resize budget case restored 512 render with scene revision 1 and two resident splats |
| `build/dev/we_gpu_tile_plans R/tile-plans` | Exit 0; 24 labeled synthetic cached/fresh phases including resize, no validation errors; output and log under R |
| `build/dev/we_gpu_viewer_regression artifacts/sample_1/scene.wexplore R/viewer-regression-retry.json` | Exit 0; full 295,952-splat scene: 1280×720, 3840×2160, fullscreen 2880×1800, restored 1428×1746; all four phases pass with zero validation errors; highest reported owned bytes 515,214,400 below 536,870,912 cap |
| `clang-format -i` on four changed C++ files, then `clang-tidy -p build/dev` on three changed translation units | Format applied; initial tidy found one test integer-widening warning, corrected and rerun clean |

The first full-scene regression, `R/viewer-regression.json`, passed windowed and
4K but failed fullscreen with `GPU buffer exceeds remaining residency budget`:
the first staged implementation duplicated already-sufficient 4K scratch. The
retry reuses sufficient buffers and passes all four phases. These are actual
render-frame observations, not P2/P3 reference-device certification. The failed
fullscreen JSON/log remain retained. The sandbox-only `we_gpu_smoke` exit 2 with
an empty driver-load error was followed by native device execution; it is not GPU
absence.

Limit: after `vkCreateSwapchainKHR` succeeds with `oldSwapchain`, Vulkan may retire
the old presentation before later view/framebuffer/semaphore creation fails. This
slice proves only pre-creation rejection preservation and normal native resize.
It does not claim post-create rollback, device-loss recovery, minimize behavior,
two-hour stress, cross-platform viewing or Q/P/N/E certification. VMA remains
unimplemented; its renderer-only allocation migration belongs in M2 after the
remaining recovery boundary is handled. Plan §6's dependency note expected an
exact VMA tag fixed at M1; the current build has neither a selected VMA tag nor a
VMA dependency. Pin and audit it before migration, without retroactively counting
the unselected library as part of M1's verified dependency set.

Correction recorded in the subsequent
[post-create recovery](2026-10-15-m2-postcreate-recovery.md): Vulkan retires
`oldSwapchain` when `vkCreateSwapchainKHR` is called, even if creation fails. The
earlier successful-create-only wording above was too narrow.
