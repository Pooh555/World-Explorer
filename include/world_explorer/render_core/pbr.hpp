#pragma once

#include "world_explorer/scene_store/scene.hpp"

namespace world_explorer {

struct PbrLighting {
    Vec3d m_to_light { .3, .6, 1 };
    double m_intensity { 1 };
    Vec3d m_ambient { .03, .03, .03 };
    double m_exposure { 1 };
};

struct PbrGpuVertex {
    std::array<float, 3> m_position, m_normal;
    std::array<float, 4> m_tangent;
    std::array<float, 2> m_uv;
};

static_assert(sizeof(PbrGpuVertex) == 48uz);

struct PbrDraw {
    std::size_t m_vertex_offset {}, m_index_offset {};
    std::uint32_t m_index_count {}, m_instance_count {}, m_instance_words {}, m_material_words {};
};

struct PbrUpload {
    std::vector<std::byte> m_bytes;
    std::vector<PbrDraw> m_draws;
    std::size_t m_vertices {}, m_triangles {};
};

Result<void> validate_pbr_lighting(const PbrLighting& lighting);
// Scene must already be validated; this packs shared geometry/textures once and
// per-instance affine rows into the ordinary timeline-owned scene buffer.
Result<PbrUpload>
prepare_pbr_upload(const SceneState& scene, Vec3d origin, std::size_t base_offset);

}  // namespace world_explorer
