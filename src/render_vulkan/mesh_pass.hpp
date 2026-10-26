#pragma once

#include "vulkan_device.hpp"

#include <memory>

namespace world_explorer {

struct MeshTarget {
    VkDevice m_device {};
    VkExtent2D m_extent {};
    detail::gpu_image m_color, m_depth;
    VkImageView m_color_view {}, m_depth_view {};
    VkFramebuffer m_framebuffer {};
    MeshTarget() = default;
    MeshTarget(const MeshTarget&) = delete;
    MeshTarget& operator=(const MeshTarget&) = delete;
    ~MeshTarget();
    std::size_t allocation_bytes() const;
};

struct MeshGpuVertex {
    std::array<float, 3> m_position, m_normal;
    std::array<float, 2> m_uv {};
    std::array<std::uint32_t, 3> m_texture {};
};

static_assert(sizeof(MeshGpuVertex) == 44uz);

struct MeshDraw {
    std::size_t m_vertex_offset {}, m_index_offset {};
    std::uint32_t m_index_count {};
    bool m_baked {};
};

// Mesh-only normal diagnostic pass. Captured splats are a separate representation;
// observed proxy geometry never occludes its own captured appearance implicitly.
class MeshPass {
  public:
    explicit MeshPass(vulkan_device& device);
    ~MeshPass();
    MeshPass(const MeshPass&) = delete;
    MeshPass& operator=(const MeshPass&) = delete;
    void initialize(const std::filesystem::path& shaders,
                    VkDescriptorSetLayout scene_layout,
                    bool pbr = false);
    std::unique_ptr<MeshTarget> create_target(VkExtent2D extent, std::size_t budget);
    void record(VkCommandBuffer command,
                const MeshTarget& target,
                const detail::gpu_buffer& scene,
                const detail::push_parameters& parameters,
                MeshDraw draw,
                VkBuffer captured,
                VkDescriptorSet scene_descriptor);
    void record_pbr(VkCommandBuffer command,
                    const MeshTarget& target,
                    const detail::gpu_buffer& scene,
                    const detail::push_parameters& parameters,
                    const std::vector<PbrDraw>& draws,
                    const PbrLighting& lighting,
                    VkBuffer captured,
                    VkDescriptorSet scene_descriptor);

  private:
    vulkan_device& m_backend;
    VkDevice m_device {};
    VkRenderPass m_pass {};
    VkPipelineLayout m_layout {};
    VkPipeline m_pipeline {};
};

}  // namespace world_explorer
