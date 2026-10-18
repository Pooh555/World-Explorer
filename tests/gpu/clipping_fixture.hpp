#ifndef WORLD_EXPLORER_TESTS_GPU_CLIPPING_FIXTURE_HPP
#define WORLD_EXPLORER_TESTS_GPU_CLIPPING_FIXTURE_HPP

#include "world_explorer/render_core/camera.hpp"
#include "world_explorer/scene_store/scene.hpp"

#include <array>
#include <cmath>
#include <limits>

namespace world_explorer {

inline CameraView clipping_camera() {
    CameraView camera;
    camera.m_near_plane = 1.F;
    camera.m_far_plane = 8.F;
    camera.m_calibration = CalibratedProjection {{900, 840, 639.5, 359.5}, 1280, 720};
    return camera;
}

inline SceneSnapshot clipping_fixture() {
    auto scene = std::make_shared<SceneState>();
    scene->m_id = { 75 };
    scene->m_revision = { 1 };
    scene->m_provenance = { { "source_kind", "synthetic_diagnostic" },
                           { "fixture", "inclusive_clipping_float_neighbors_v1" } };
    auto chunk = std::make_shared<SceneChunk>();
    chunk->m_id = { 1 };
    chunk->m_bounds = { { -5, -2, -9 }, { 5, 2, 0 } };
    const auto camera = clipping_camera();
    const auto infinity = std::numeric_limits<float>::infinity();
    const std::array depths {std::nextafter(camera.m_near_plane, 0.F),
                             camera.m_near_plane,
                             std::nextafter(camera.m_near_plane, infinity),
                             std::nextafter(camera.m_far_plane, 0.F),
                             camera.m_far_plane,
                             std::nextafter(camera.m_far_plane, infinity)};
    for (std::size_t index { 0uz }; index < depths.size(); ++index) {
        const auto depth = depths[index];
        Gaussian splat;
        // Ratios are exact powers of two; each footprint occupies a separate image region.
        splat.m_mean = { depth * (static_cast<float>(index % 3) - 1.F) * 0.5F,
                        depth * (index < 3 ? 0.125F : -0.125F),
                        -depth };
        splat.m_scale = { depth / 64.F, depth / 64.F, depth / 64.F };
        splat.m_opacity = 0.8F;
        splat.m_sh_degree = 3;
        const std::array color =
            index < 3 ? std::array { 0.85F, 0.35F, 0.15F } : std::array { 0.15F, 0.45F, 0.85F };
        for (std::size_t channel { 0uz }; channel < color.size(); ++channel)
            splat.m_sh[channel] = (color[channel] - 0.5F) / 0.2820947918F;
        chunk->m_splats.push_back(splat);
    }
    scene->m_chunks.emplace(chunk->m_id, chunk);
    return scene;
}

}  // namespace world_explorer

#endif  // WORLD_EXPLORER_TESTS_GPU_CLIPPING_FIXTURE_HPP
