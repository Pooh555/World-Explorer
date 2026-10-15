#ifndef WORLD_EXPLORER_RENDER_VULKAN_COMMON_HPP
#define WORLD_EXPLORER_RENDER_VULKAN_COMMON_HPP

#include "world_explorer/render_core/camera.hpp"
#include "world_explorer/render_core/update_sink.hpp"
#include "world_explorer/render_vulkan/renderer.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>
#include <vulkan/vulkan.h>

namespace world_explorer {
namespace detail {

template <class type> type structure(VkStructureType tag) {
    type value{};
    value.sType = tag;
    return value;
}

class vulkan_error : public std::runtime_error {
  public:
    vulkan_error(VkResult status, const char* operation)
        : std::runtime_error(std::string(operation) + " failed (VkResult " +
                             std::to_string(status) + ")"),
          status_(status) {}
    VkResult status() const noexcept { return status_; }

  private:
    VkResult status_;
};

inline void check(VkResult status, const char* operation) {
    if (status != VK_SUCCESS)
        throw vulkan_error(status, operation);
}

struct gpu_buffer {
    VkDevice device{};
    VkBuffer buffer{};
    VkDeviceMemory memory{};
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
        if (mapped)
            vkUnmapMemory(device, memory);
        if (buffer)
            vkDestroyBuffer(device, buffer, nullptr);
        if (memory)
            vkFreeMemory(device, memory, nullptr);
    }
    void swap(gpu_buffer& other) noexcept {
        std::swap(device, other.device);
        std::swap(buffer, other.buffer);
        std::swap(memory, other.memory);
        std::swap(bytes, other.bytes);
        std::swap(allocation_bytes, other.allocation_bytes);
        std::swap(mapped, other.mapped);
    }
};

struct gpu_splat {
    std::array<float, 4> mean_opacity, scale_degree, rotation, world_to_sh;
    std::array<float, 48> sh;
};
static_assert(sizeof(gpu_splat) == 256);

struct push_parameters {
    std::array<float, 4> right, down, forward, camera_position, intrinsics, viewport_clip;
    std::array<std::uint32_t, 4> count_shift;
    std::array<std::uint32_t, 4> tiles;
};
static_assert(sizeof(push_parameters) == 128);

inline quatd multiply(quatd a, quatd b) {
    return {a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}

inline std::array<float, 4> floats(vec3d v) {
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z), 0};
}

inline std::array<float, 4> floats(quatd q) {
    return {static_cast<float>(q.w), static_cast<float>(q.x), static_cast<float>(q.y),
            static_cast<float>(q.z)};
}

inline void memory_barrier(VkCommandBuffer command, VkPipelineStageFlags2 source_stage,
                           VkAccessFlags2 source_access, VkPipelineStageFlags2 target_stage,
                           VkAccessFlags2 target_access) {
    auto barrier = structure<VkMemoryBarrier2>(VK_STRUCTURE_TYPE_MEMORY_BARRIER_2);
    barrier.srcStageMask = source_stage;
    barrier.srcAccessMask = source_access;
    barrier.dstStageMask = target_stage;
    barrier.dstAccessMask = target_access;
    auto dependency = structure<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(command, &dependency);
}

inline void compute_barrier(VkCommandBuffer command) {
    memory_barrier(command, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
                   VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                   VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
}

} // namespace detail
} // namespace world_explorer

#endif // WORLD_EXPLORER_RENDER_VULKAN_COMMON_HPP
