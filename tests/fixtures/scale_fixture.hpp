#ifndef WORLD_EXPLORER_TESTS_FIXTURES_SCALE_FIXTURE_HPP
#define WORLD_EXPLORER_TESTS_FIXTURES_SCALE_FIXTURE_HPP

#include "world_explorer/render_core/camera.hpp"
#include "world_explorer/scene_store/scene.hpp"
#include <string_view>

namespace world_explorer {
inline constexpr std::string_view SCALE_FIXTURE_SHA256 =
    "589336d9a958d9870936b6707857e0407fd8bf0643e3d822b5c1416080ae64f8";
inline constexpr std::size_t SCALE_FIXTURE_SPLATS = 2000000;
inline constexpr std::size_t SCALE_FIXTURE_CONTRIBUTORS = 1000000;
inline constexpr std::size_t SCALE_FIXTURE_REFERENCES = 1561248;
inline constexpr std::uint32_t SCALE_FIXTURE_WIDTH = 1920, SCALE_FIXTURE_HEIGHT = 1080;

// Separate from the frozen 250k portable fixture. Two equal banks straddle
// the camera; a half turn exposes the previously culled bank without eviction.
// Integer indexing and binary fractions avoid random engines and libm variation.
inline scene_snapshot make_scale_fixture() {
    auto scene = std::make_shared<scene_state>();
    scene->id = {82};
    scene->revision = {1};
    scene->provenance = {{"source_kind", "synthetic_diagnostic"},
                         {"fixture", "resident_2m_two_banks_v1"},
                         {"asset_license", "MIT"},
                         {"generator", "tests/fixtures/scale_fixture.hpp"}};
    for (std::size_t bank = 0; bank < 2; ++bank)
        for (std::size_t part = 0; part < 4; ++part) {
            auto chunk = std::make_shared<scene_chunk>();
            chunk->id = {bank * 4 + part + 1};
            chunk->bounds = {{-4, -2, static_cast<double>(bank * 16)},
                             {4, 2, static_cast<double>(bank * 16)}};
            chunk->splats.reserve(250000);
            for (std::size_t y = part * 250; y < (part + 1) * 250; ++y)
                for (std::size_t x = 0; x < 1000; ++x) {
                    gaussian splat;
                    splat.mean = {(static_cast<float>(x) - 499.5F) * .0078125F,
                                  (static_cast<float>(y) - 499.5F) * .00390625F,
                                  static_cast<float>(bank * 16)};
                    splat.scale = {.0009765625F, .00048828125F, .000244140625F};
                    splat.opacity = .25F;
                    splat.sh_degree = 3;
                    for (std::size_t channel = 0; channel < 3; ++channel) {
                        const int base = bank == channel ? 4 : -2;
                        splat.sh[channel] =
                            static_cast<float>(base + static_cast<int>((x / 125 + y / 125) % 2)) *
                            .25F;
                        for (std::size_t coefficient = 1; coefficient < 16; ++coefficient)
                            splat.sh[coefficient * 3 + channel] =
                                static_cast<float>(
                                    static_cast<int>((x + y * 3 + channel + coefficient) % 17) -
                                    8) *
                                .001953125F;
                    }
                    chunk->splats.push_back(splat);
                }
            scene->chunks.emplace(chunk->id, std::move(chunk));
        }
    return scene;
}
inline camera_view scale_fixture_camera(bool reverse = false) {
    camera_view camera;
    camera.position = {0, 0, 8};
    if (reverse) {
        camera.right = {-1, 0, 0};
        camera.forward = {0, 0, 1};
    }
    camera.near_plane = 1;
    camera.far_plane = 32;
    camera.calibration = calibrated_projection{
        {1536, 1536, 959.5, 539.5}, SCALE_FIXTURE_WIDTH, SCALE_FIXTURE_HEIGHT};
    return camera;
}
} // namespace world_explorer
#endif
