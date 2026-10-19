#pragma once

#if defined(WE_VULKAN_TEST_FAULTS)
namespace world_explorer::detail {

// One-shot diagnostic at the frame ownership boundary. The GPU is not actually lost.
void fail_next_frame_with_device_loss();
enum class frame_acquire_delay { NONE, TIMEOUT, NOT_READY };
// Skip the driver call, so the acquire semaphore remains unsignaled.
void defer_next_frame_acquisition(frame_acquire_delay delay);

}  // namespace world_explorer::detail
#endif
