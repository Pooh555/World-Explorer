#ifndef WORLD_EXPLORER_RENDER_VULKAN_RENDERER_FAULTS_HPP
#define WORLD_EXPLORER_RENDER_VULKAN_RENDERER_FAULTS_HPP

#if defined(WE_VULKAN_TEST_FAULTS)
namespace world_explorer::detail {
// One-shot diagnostic at the frame ownership boundary. The GPU is not actually lost.
void fail_next_frame_with_device_loss();
} // namespace world_explorer::detail
#endif

#endif // WORLD_EXPLORER_RENDER_VULKAN_RENDERER_FAULTS_HPP
