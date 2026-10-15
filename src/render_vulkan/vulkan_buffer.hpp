#ifndef WORLD_EXPLORER_RENDER_VULKAN_BUFFER_HPP
#define WORLD_EXPLORER_RENDER_VULKAN_BUFFER_HPP

#include <utility>
#include <vk_mem_alloc.h>

namespace world_explorer::detail {
struct gpu_buffer {
    VmaAllocator allocator{};
    VkBuffer buffer{};
    VmaAllocation allocation{};
    VkDeviceSize bytes{};
    VkDeviceSize allocation_bytes{};
    void* mapped{};

    gpu_buffer() = default;
    gpu_buffer(const gpu_buffer&) = delete;
    gpu_buffer& operator=(const gpu_buffer&) = delete;
    gpu_buffer(gpu_buffer&& other) noexcept { swap(other); }
    gpu_buffer& operator=(gpu_buffer&& other) noexcept {
        gpu_buffer temporary(std::move(other));
        swap(temporary);
        return *this;
    }
    ~gpu_buffer() {
        if (buffer)
            vmaDestroyBuffer(allocator, buffer, allocation);
    }
    void swap(gpu_buffer& other) noexcept {
        std::swap(allocator, other.allocator);
        std::swap(buffer, other.buffer);
        std::swap(allocation, other.allocation);
        std::swap(bytes, other.bytes);
        std::swap(allocation_bytes, other.allocation_bytes);
        std::swap(mapped, other.mapped);
    }
};
} // namespace world_explorer::detail

#endif // WORLD_EXPLORER_RENDER_VULKAN_BUFFER_HPP
