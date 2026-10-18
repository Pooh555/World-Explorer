#ifndef WORLD_EXPLORER_TESTS_FIXTURES_VIEWER_FIXTURE_HPP
#define WORLD_EXPLORER_TESTS_FIXTURES_VIEWER_FIXTURE_HPP

#include "world_explorer/render_core/camera.hpp"
#include "world_explorer/scene_store/scene.hpp"

#include <string_view>

namespace world_explorer {

inline constexpr std::string_view K_VIEWER_FIXTURE_SHA256 {
    "10ddfa905d3136ed8343d13393cfdab1c79cc23bfd846d23bc237793492b65ba"};
inline constexpr std::size_t K_VIEWER_FIXTURE_SPLATS { 250000uz };
inline constexpr std::uint32_t K_VIEWER_FIXTURE_WIDTH { 1280 };
inline constexpr std::uint32_t K_VIEWER_FIXTURE_HEIGHT { 720 };

// Project-authored diagnostic. Binary fractions and fixed integer indexing make
// the asset independent of libm, random engines and captured/private data.
inline SceneSnapshot make_portable_viewer_fixture() {
    auto scene = std::make_shared<SceneState>();
    scene->m_id = { 81 };
    scene->m_revision = { 1 };
    scene->m_provenance = { { "source_kind", "synthetic_diagnostic" },
                           { "fixture", "portable_viewer_250k_v1" },
                           { "asset_license", "MIT" },
                           { "generator", "tests/fixtures/viewer_fixture.hpp" } };
    for (std::size_t layer { 0uz }; layer < 4uz; ++layer) {
        auto chunk = std::make_shared<SceneChunk>();
        chunk->m_id = { layer + 1 };
        chunk->m_bounds = { { -4, -4, -.125 }, { 4, 4, .125 } };
        chunk->m_world_from_chunk.m_translation = { static_cast<double>(layer) * .125,
                                                   -static_cast<double>(layer) * .25,
                                                   -static_cast<double>(layer) * .5 };
        chunk->m_splats.reserve(62500);
        for (std::size_t y { 0uz }; y < 250uz; ++y)
            for (std::size_t x { 0uz }; x < 250uz; ++x) {
                Gaussian splat;
                splat.m_mean = { (static_cast<float>(x) - 124.5F) * .03125F,
                                (static_cast<float>(y) - 124.5F) * .03125F,
                                -static_cast<float>((x + y) % 4) * .0078125F };
                splat.m_scale = { .0234375F, .015625F, .0078125F };
                splat.m_opacity = .5F;
                splat.m_sh_degree = 3;
                for (std::size_t channel { 0uz }; channel < 3uz; ++channel) {
                    splat.m_sh[channel] =
                        static_cast<float>(static_cast<int>((x + y * 3 + layer + channel) % 9) -
                                           4) *
                        .25F;
                    for (std::size_t coefficient { 1uz }; coefficient < 16uz; ++coefficient)
                        splat.m_sh[coefficient * 3 + channel] =
                            static_cast<float>(
                                static_cast<int>((x + y * 3 + layer + channel + coefficient) % 17) -
                                8) *
                            .015625F;
                }
                chunk->m_splats.push_back(splat);
            }
        scene->m_chunks.emplace(chunk->m_id, chunk);
    }
    return scene;
}

inline CameraView portable_viewer_camera() {
    CameraView camera;
    camera.m_position = { 0, 0, 12 };
    camera.m_near_plane = 1;
    camera.m_far_plane = 64;
    camera.m_calibration = CalibratedProjection {
        {900, 900, 639.5, 359.5}, K_VIEWER_FIXTURE_WIDTH, K_VIEWER_FIXTURE_HEIGHT};
    return camera;
}

}  // namespace world_explorer
#endif  // WORLD_EXPLORER_TESTS_FIXTURES_VIEWER_FIXTURE_HPP
