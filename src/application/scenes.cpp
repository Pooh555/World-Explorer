#include "world_explorer/application/scenes.hpp"
#include <algorithm>
#include <limits>

namespace world_explorer {
scene_snapshot make_diagnostic_scene() {
    auto scene = std::make_shared<scene_state>();
    scene->id = {1};
    scene->revision = {1};
    scene->provenance = {{"source_kind", "synthetic_diagnostic"},
                         {"fixture", "asymmetric_gaussians_v1"}};
    auto chunk = std::make_shared<scene_chunk>();
    chunk->id = {1};
    chunk->bounds = {{-1.5, -1, -1}, {1.5, 1, 1}};
    // An asymmetric arrangement exposes handedness and depth-order mistakes.
    for (int index = 0; index < 45; ++index) {
        gaussian splat;
        const float col = static_cast<float>(index % 9) - 4.0F;
        const float row = static_cast<float>(index) / 9.0F - 2.0F;
        splat.mean = {col * 0.28F, row * 0.28F, static_cast<float>(index % 3) * -0.2F};
        splat.scale = {0.13F, 0.08F, 0.06F};
        splat.opacity = 0.9F;
        splat.sh[0] = (static_cast<float>(index % 9) / 8.0F - 0.5F) / 0.2820947918F;
        splat.sh[1] = (static_cast<float>(index) / 9.0F / 4.0F - 0.5F) / 0.2820947918F;
        splat.sh[2] = 0.25F / 0.2820947918F;
        chunk->splats.push_back(splat);
    }
    scene->chunks.emplace(chunk->id, chunk);
    return scene;
}
bounds3d world_bounds(const scene_state& scene) {
    const auto infinity = std::numeric_limits<double>::infinity();
    bounds3d result{{infinity, infinity, infinity}, {-infinity, -infinity, -infinity}};
    for (const auto& [id, chunk] : scene.chunks) {
        (void)id;
        for (unsigned corner = 0; corner < 8; ++corner) {
            const auto& bounds = chunk->bounds;
            const auto point =
                transform_point(chunk->world_from_chunk,
                                {(corner & 1u) != 0u ? bounds.maximum.x : bounds.minimum.x,
                                 (corner & 2u) != 0u ? bounds.maximum.y : bounds.minimum.y,
                                 (corner & 4u) != 0u ? bounds.maximum.z : bounds.minimum.z});
            result.minimum.x = std::min(result.minimum.x, point.x);
            result.minimum.y = std::min(result.minimum.y, point.y);
            result.minimum.z = std::min(result.minimum.z, point.z);
            result.maximum.x = std::max(result.maximum.x, point.x);
            result.maximum.y = std::max(result.maximum.y, point.y);
            result.maximum.z = std::max(result.maximum.z, point.z);
        }
    }
    return result;
}
} // namespace world_explorer
