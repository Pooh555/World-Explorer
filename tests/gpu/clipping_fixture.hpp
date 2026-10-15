#ifndef WORLD_EXPLORER_TESTS_GPU_CLIPPING_FIXTURE_HPP
#define WORLD_EXPLORER_TESTS_GPU_CLIPPING_FIXTURE_HPP

#include "world_explorer/render_core/camera.hpp"
#include "world_explorer/scene_store/scene.hpp"
#include <array>
#include <cmath>
#include <limits>

namespace world_explorer {
inline camera_view clipping_camera() {
    camera_view camera;
    camera.near_plane = 1.F;
    camera.far_plane = 8.F;
    camera.calibration = calibrated_projection{{900, 840, 639.5, 359.5}, 1280, 720};
    return camera;
}

inline scene_snapshot clipping_fixture() {
    auto scene = std::make_shared<scene_state>();
    scene->id = {75};
    scene->revision = {1};
    scene->provenance = {{"source_kind", "synthetic_diagnostic"},
                         {"fixture", "inclusive_clipping_float_neighbors_v1"}};
    auto chunk = std::make_shared<scene_chunk>();
    chunk->id = {1};
    chunk->bounds = {{-5, -2, -9}, {5, 2, 0}};
    const auto camera = clipping_camera();
    const auto infinity = std::numeric_limits<float>::infinity();
    const std::array depths{std::nextafter(camera.near_plane, 0.F),
                            camera.near_plane,
                            std::nextafter(camera.near_plane, infinity),
                            std::nextafter(camera.far_plane, 0.F),
                            camera.far_plane,
                            std::nextafter(camera.far_plane, infinity)};
    for (std::size_t index = 0; index < depths.size(); ++index) {
        const auto depth = depths[index];
        gaussian splat;
        // Ratios are exact powers of two; each footprint occupies a separate image region.
        splat.mean = {depth * (static_cast<float>(index % 3) - 1.F) * 0.5F,
                      depth * (index < 3 ? 0.125F : -0.125F), -depth};
        splat.scale = {depth / 64.F, depth / 64.F, depth / 64.F};
        splat.opacity = 0.8F;
        splat.sh_degree = 3;
        const std::array color =
            index < 3 ? std::array{0.85F, 0.35F, 0.15F} : std::array{0.15F, 0.45F, 0.85F};
        for (std::size_t channel = 0; channel < color.size(); ++channel)
            splat.sh[channel] = (color[channel] - 0.5F) / 0.2820947918F;
        chunk->splats.push_back(splat);
    }
    scene->chunks.emplace(chunk->id, chunk);
    return scene;
}
} // namespace world_explorer

#endif // WORLD_EXPLORER_TESTS_GPU_CLIPPING_FIXTURE_HPP
