#ifndef WORLD_EXPLORER_RENDER_VULKAN_COMMON_HPP
#define WORLD_EXPLORER_RENDER_VULKAN_COMMON_HPP

#include "world_explorer/render_core/camera.hpp"
#include "world_explorer/render_core/update_sink.hpp"
#include "world_explorer/render_vulkan/renderer.hpp"

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

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>

namespace world_explorer {
namespace detail {

template <class type> type structure(VkStructureType tag) {
    type value {};
    value.sType = tag;
    return value;
}

class vulkan_error : public std::runtime_error {
  public:
    vulkan_error(VkResult status, const char* operation)
        : std::runtime_error(std::string(operation) + " failed (VkResult " +
                             std::to_string(status) + ")"),
          status_(status) { }

    VkResult status() const noexcept { return status_; }

  private:
    VkResult status_;
};

inline void check(VkResult status, const char* operation) {
    if (status != VK_SUCCESS) throw vulkan_error(status, operation);
}

struct gpu_splat {
    std::array<float, 4> mean_opacity, scale_degree, rotation, world_to_sh;
    std::array<float, 48> sh;
};

static_assert(sizeof(gpu_splat) == 256);

struct gpu_splat_packed_sh {
    std::array<float, 4> mean_opacity, scale_degree, rotation, world_to_sh;
    std::array<std::uint32_t, 24> sh;
};

static_assert(sizeof(gpu_splat_packed_sh) == 160);
static_assert(offsetof(gpu_splat, sh) == 64 && offsetof(gpu_splat_packed_sh, sh) == 64);

struct push_parameters {
    std::array<float, 4> right, down, forward, camera_position, intrinsics, viewport_clip;
    std::array<std::uint32_t, 4> count_shift;
    std::array<std::uint32_t, 4> tiles;
};

static_assert(sizeof(push_parameters) == 128);

inline Quatd multiply(Quatd a, Quatd b) {
    return {a.m_w * b.m_w - a.m_x * b.m_x - a.m_y * b.m_y - a.m_z * b.m_z,
            a.m_w * b.m_x + a.m_x * b.m_w + a.m_y * b.m_z - a.m_z * b.m_y,
            a.m_w * b.m_y - a.m_x * b.m_z + a.m_y * b.m_w + a.m_z * b.m_x,
            a.m_w * b.m_z + a.m_x * b.m_y - a.m_y * b.m_x + a.m_z * b.m_w};
}

inline std::array<float, 4> floats(Vec3d v) {
    return { static_cast<float>(v.m_x), static_cast<float>(v.m_y), static_cast<float>(v.m_z), 0 };
}

inline std::array<float, 4> floats(Quatd q) {
    return {static_cast<float>(q.m_w),
            static_cast<float>(q.m_x),
            static_cast<float>(q.m_y),
            static_cast<float>(q.m_z)};
}

inline void memory_barrier(VkCommandBuffer command,
                           VkPipelineStageFlags2 source_stage,
                           VkAccessFlags2 source_access,
                           VkPipelineStageFlags2 target_stage,
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
    memory_barrier(command,
                   VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                   VK_ACCESS_2_SHADER_WRITE_BIT,
                   VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                   VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
}

}  // namespace detail
}  // namespace world_explorer

#endif  // WORLD_EXPLORER_RENDER_VULKAN_COMMON_HPP
