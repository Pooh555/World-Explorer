#ifndef WORLD_EXPLORER_TESTS_FIXTURES_VIEWER_FIXTURE_HPP
#define WORLD_EXPLORER_TESTS_FIXTURES_VIEWER_FIXTURE_HPP

#include "world_explorer/render_core/camera.hpp"
#include "world_explorer/scene_store/scene.hpp"
#include <string_view>

namespace world_explorer {
inline constexpr std::string_view VIEWER_FIXTURE_SHA256 =
    "10ddfa905d3136ed8343d13393cfdab1c79cc23bfd846d23bc237793492b65ba";
inline constexpr std::size_t VIEWER_FIXTURE_SPLATS = 250000;
inline constexpr std::uint32_t VIEWER_FIXTURE_WIDTH = 1280, VIEWER_FIXTURE_HEIGHT = 720;

// Project-authored diagnostic. Binary fractions and fixed integer indexing make
// the asset independent of libm, random engines and captured/private data.
inline scene_snapshot make_portable_viewer_fixture() {
    auto scene = std::make_shared<scene_state>();
    scene->id = {81};
    scene->revision = {1};
    scene->provenance = {{"source_kind", "synthetic_diagnostic"},
                         {"fixture", "portable_viewer_250k_v1"},
                         {"asset_license", "MIT"},
                         {"generator", "tests/fixtures/viewer_fixture.hpp"}};
    for (std::size_t layer = 0; layer < 4; ++layer) {
        auto chunk = std::make_shared<scene_chunk>();
        chunk->id = {layer + 1};
        chunk->bounds = {{-4, -4, -.125}, {4, 4, .125}};
        chunk->world_from_chunk.translation = {static_cast<double>(layer) * .125,
                                               -static_cast<double>(layer) * .25,
                                               -static_cast<double>(layer) * .5};
        chunk->splats.reserve(62500);
        for (std::size_t y = 0; y < 250; ++y)
            for (std::size_t x = 0; x < 250; ++x) {
                gaussian splat;
                splat.mean = {(static_cast<float>(x) - 124.5F) * .03125F,
                              (static_cast<float>(y) - 124.5F) * .03125F,
                              -static_cast<float>((x + y) % 4) * .0078125F};
                splat.scale = {.0234375F, .015625F, .0078125F};
                splat.opacity = .5F;
                splat.sh_degree = 3;
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    splat.sh[channel] =
                        static_cast<float>(static_cast<int>((x + y * 3 + layer + channel) % 9) -
                                           4) *
                        .25F;
                    for (std::size_t coefficient = 1; coefficient < 16; ++coefficient)
                        splat.sh[coefficient * 3 + channel] =
                            static_cast<float>(
                                static_cast<int>((x + y * 3 + layer + channel + coefficient) % 17) -
                                8) *
                            .015625F;
                }
                chunk->splats.push_back(splat);
            }
        scene->chunks.emplace(chunk->id, chunk);
    }
    return scene;
}

inline camera_view portable_viewer_camera() {
    camera_view camera;
    camera.position = {0, 0, 12};
    camera.near_plane = 1;
    camera.far_plane = 64;
    camera.calibration = calibrated_projection{
        {900, 900, 639.5, 359.5}, VIEWER_FIXTURE_WIDTH, VIEWER_FIXTURE_HEIGHT};
    return camera;
}
} // namespace world_explorer
#endif // WORLD_EXPLORER_TESTS_FIXTURES_VIEWER_FIXTURE_HPP
