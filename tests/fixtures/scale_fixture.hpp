#ifndef WORLD_EXPLORER_TESTS_FIXTURES_SCALE_FIXTURE_HPP
#define WORLD_EXPLORER_TESTS_FIXTURES_SCALE_FIXTURE_HPP

#include "world_explorer/render_core/camera.hpp"
#include "world_explorer/scene_store/scene.hpp"

#include <string_view>

namespace world_explorer {

inline constexpr std::string_view K_SCALE_FIXTURE_SHA256 {
    "589336d9a958d9870936b6707857e0407fd8bf0643e3d822b5c1416080ae64f8"};
inline constexpr std::size_t K_SCALE_FIXTURE_SPLATS { 2000000uz };
inline constexpr std::size_t K_SCALE_FIXTURE_CONTRIBUTORS { 1000000uz };
inline constexpr std::size_t K_SCALE_FIXTURE_REFERENCES { 1561248uz };
inline constexpr std::uint32_t K_SCALE_FIXTURE_WIDTH { 1920 };
inline constexpr std::uint32_t K_SCALE_FIXTURE_HEIGHT { 1080 };

// Separate from the frozen 250k portable fixture. Two equal banks straddle
// the camera; a half turn exposes the previously culled bank without eviction.
// Integer indexing and binary fractions avoid random engines and libm variation.
inline SceneSnapshot make_scale_fixture() {
    auto scene = std::make_shared<SceneState>();
    scene->m_id = { 82 };
    scene->m_revision = { 1 };
    scene->m_provenance = { { "source_kind", "synthetic_diagnostic" },
                           { "fixture", "resident_2m_two_banks_v1" },
                           { "asset_license", "MIT" },
                           { "generator", "tests/fixtures/scale_fixture.hpp" } };
    for (std::size_t bank { 0uz }; bank < 2uz; ++bank)
        for (std::size_t part { 0uz }; part < 4uz; ++part) {
            auto chunk = std::make_shared<SceneChunk>();
            chunk->m_id = { bank * 4 + part + 1 };
            chunk->m_bounds = { { -4, -2, static_cast<double>(bank * 16) },
                               { 4, 2, static_cast<double>(bank * 16) } };
            chunk->m_splats.reserve(250000);
            for (std::size_t y { part * 250uz }; y < (part + 1uz) * 250uz; ++y)
                for (std::size_t x { 0uz }; x < 1000uz; ++x) {
                    Gaussian splat;
                    splat.m_mean = { (static_cast<float>(x) - 499.5F) * .0078125F,
                                    (static_cast<float>(y) - 499.5F) * .00390625F,
                                    static_cast<float>(bank * 16) };
                    splat.m_scale = { .0009765625F, .00048828125F, .000244140625F };
                    splat.m_opacity = .25F;
                    splat.m_sh_degree = 3;
                    for (std::size_t channel { 0uz }; channel < 3uz; ++channel) {
                        const int base = bank == channel ? 4 : -2;
                        splat.m_sh[channel] =
                            static_cast<float>(base + static_cast<int>((x / 125 + y / 125) % 2)) *
                            .25F;
                        for (std::size_t coefficient { 1uz }; coefficient < 16uz; ++coefficient)
                            splat.m_sh[coefficient * 3 + channel] =
                                static_cast<float>(
                                    static_cast<int>((x + y * 3 + channel + coefficient) % 17) -
                                    8) *
                                .001953125F;
                    }
                    chunk->m_splats.push_back(splat);
                }
            scene->m_chunks.emplace(chunk->m_id, std::move(chunk));
        }
    return scene;
}

inline CameraView scale_fixture_camera(bool reverse = false) {
    CameraView camera;
    camera.m_position = { 0, 0, 8 };
    if (reverse) {
        camera.m_right = { -1, 0, 0 };
        camera.m_forward = { 0, 0, 1 };
    }
    camera.m_near_plane = 1;
    camera.m_far_plane = 32;
    camera.m_calibration = CalibratedProjection {
        {1536, 1536, 959.5, 539.5}, K_SCALE_FIXTURE_WIDTH, K_SCALE_FIXTURE_HEIGHT};
    return camera;
}

}  // namespace world_explorer
#endif
