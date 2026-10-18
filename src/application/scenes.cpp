#include "world_explorer/application/scenes.hpp"

#include <algorithm>
#include <limits>

namespace world_explorer {

SceneSnapshot make_diagnostic_scene() {
    auto scene = std::make_shared<SceneState>();
    scene->m_id = { 1 };
    scene->m_revision = { 1 };
    scene->m_provenance = { { "source_kind", "synthetic_diagnostic" },
                           { "fixture", "asymmetric_gaussians_v1" } };
    auto chunk = std::make_shared<SceneChunk>();
    chunk->m_id = { 1 };
    chunk->m_bounds = { { -1.5, -1, -1 }, { 1.5, 1, 1 } };
    // An asymmetric arrangement exposes handedness and depth-order mistakes.
    for (int index { 0 }; index < 45; ++index) {
        Gaussian splat;
        const float K_COL = static_cast<float>(index % 9) - 4.0F;
        const float K_ROW = static_cast<float>(index) / 9.0F - 2.0F;
        splat.m_mean = { K_COL * 0.28F, K_ROW * 0.28F, static_cast<float>(index % 3) * -0.2F };
        splat.m_scale = { 0.13F, 0.08F, 0.06F };
        splat.m_opacity = 0.9F;
        splat.m_sh[0] = (static_cast<float>(index % 9) / 8.0F - 0.5F) / 0.2820947918F;
        splat.m_sh[1] = (static_cast<float>(index) / 9.0F / 4.0F - 0.5F) / 0.2820947918F;
        splat.m_sh[2] = 0.25F / 0.2820947918F;
        chunk->m_splats.push_back(splat);
    }
    scene->m_chunks.emplace(chunk->m_id, chunk);
    return scene;
}

Bounds3d world_bounds(const SceneState& scene) {
    const auto K_INFINITY = std::numeric_limits<double>::infinity();
    Bounds3d result {{K_INFINITY, K_INFINITY, K_INFINITY}, {-K_INFINITY, -K_INFINITY, -K_INFINITY}};
    for (const auto& [K_ID, K_CHUNK] : scene.m_chunks) {
        (void)K_ID;
        for (unsigned corner { 0 }; corner < 8; ++corner) {
            const auto& bounds = K_CHUNK->m_bounds;
            const auto K_POINT = transform_point(
                K_CHUNK->m_world_from_chunk,
                {(corner & 1u) != 0u ? bounds.m_maximum.m_x : bounds.m_minimum.m_x,
                 (corner & 2u) != 0u ? bounds.m_maximum.m_y : bounds.m_minimum.m_y,
                 (corner & 4u) != 0u ? bounds.m_maximum.m_z : bounds.m_minimum.m_z});
            result.m_minimum.m_x = std::min(result.m_minimum.m_x, K_POINT.m_x);
            result.m_minimum.m_y = std::min(result.m_minimum.m_y, K_POINT.m_y);
            result.m_minimum.m_z = std::min(result.m_minimum.m_z, K_POINT.m_z);
            result.m_maximum.m_x = std::max(result.m_maximum.m_x, K_POINT.m_x);
            result.m_maximum.m_y = std::max(result.m_maximum.m_y, K_POINT.m_y);
            result.m_maximum.m_z = std::max(result.m_maximum.m_z, K_POINT.m_z);
        }
    }
    return result;
}

}  // namespace world_explorer
