#include "../fixtures/scale_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace world_explorer;

TEST_CASE("maximum scale fixture freezes two million positive opacity degree three splats",
          "[scale_fixture]") {
    const auto K_SCENE = make_scale_fixture();
    SceneLimits limits;
    limits.m_max_chunks = 8;
    limits.m_max_splats = K_SCALE_FIXTURE_SPLATS;
    limits.m_max_payload_bytes = 512ULL * 1024 * 1024;
    const auto K_BYTES = validate_scene(*K_SCENE, limits);
    REQUIRE(K_BYTES);
    CHECK(*K_BYTES == K_SCALE_FIXTURE_SPLATS * sizeof(Gaussian));
    CHECK(K_SCENE->m_scale == ScaleStatus::ARBITRARY);
    CHECK(K_SCENE->m_provenance.at("source_kind") == "synthetic_diagnostic");
    CHECK(K_SCENE->m_provenance.at("asset_license") == "MIT");
    REQUIRE(K_SCENE->m_chunks.size() == 8);
    std::size_t total {}, front {}, back {}, higher_degree {}, valid_points {};
    const auto K_CAMERA = scale_fixture_camera();
    for (const auto& [K_ID, K_CHUNK] : K_SCENE->m_chunks) {
        static_cast<void>(K_ID);
        CHECK(K_CHUNK->m_splats.size() == 250000);
        CHECK(K_CHUNK->m_geometry_valid == false);
        CHECK(K_CHUNK->m_source_frames.empty());
        for (const auto& splat : K_CHUNK->m_splats) {
            ++total;
            higher_degree += splat.m_sh_degree == 3 && splat.m_opacity == .25F &&
                             (splat.m_sh[45] != 0 || splat.m_sh[46] != 0 || splat.m_sh[47] != 0);
            const Vec3d K_POINT { splat.m_mean[0], splat.m_mean[1], splat.m_mean[2] };
            const auto K_DELTA = K_POINT - K_CAMERA.m_position;
            const auto K_DEPTH = dot(K_DELTA, K_CAMERA.m_forward);
            if (K_DEPTH < 0) {
                ++back;
                continue;
            }
            ++front;
            const auto K_X = 1536 * dot(K_DELTA, K_CAMERA.m_right) / K_DEPTH + 960;
            const auto K_Y = 1536 * dot(K_DELTA, K_CAMERA.m_down) / K_DEPTH + 540;
            // Independent camera-space admission: every positive-depth centre
            // is well inside the viewport, with no clipping/opacity shortcut.
            valid_points += K_DEPTH == 8 && K_X > 2 && K_X < 1918 && K_Y > 2 && K_Y < 1078;
        }
    }
    CHECK(total == 2000000);
    CHECK(front == 1000000);
    CHECK(back == 1000000);
    CHECK(valid_points == front);
    CHECK(higher_degree == total);
    CHECK(scene_content_hash(*K_SCENE) == K_SCALE_FIXTURE_SHA256);
    limits.m_max_splats = K_SCALE_FIXTURE_SPLATS - 1;
    CHECK(!validate_scene(*K_SCENE, limits));
}

TEST_CASE("maximum scale cameras have analytic full-bank coverage in both directions",
          "[scale_fixture]") {
    // The perspective covariance diagonals lie in [0.3, 0.336] for all
    // points/scales in this fixture; both extrema round to a two-pixel radius.
    const auto K_EXTENT = std::min(3.33, std::sqrt(2 * std::log(.25 * 255)));
    CHECK(std::ceil(K_EXTENT * std::sqrt(.3)) == 2);
    CHECK(std::ceil(K_EXTENT * std::sqrt(.336)) == 2);
    std::size_t horizontal {}, vertical {};
    // Every projected ellipse has a two-pixel radius. Count crossings in exact
    // eighth-pixel integer coordinates, independently of GPU projection/scan.
    for (int index { 0 }; index < 1000; ++index) {
        const auto K_X = 7680 + (2 * index - 999) * 6;
        const auto K_Y = 4320 - (2 * index - 999) * 3;
        horizontal += static_cast<std::size_t>((K_X + 16 + 127) / 128 - (K_X - 16) / 128);
        vertical += static_cast<std::size_t>((K_Y + 16 + 127) / 128 - (K_Y - 16) / 128);
    }
    CHECK(horizontal * vertical == K_SCALE_FIXTURE_REFERENCES);
    CHECK(K_SCALE_FIXTURE_REFERENCES <= 8000000);
    for (const bool K_REVERSE : { false, true }) {
        const auto K_CAMERA = scale_fixture_camera(K_REVERSE);
        const auto K_K = raster_intrinsics(K_CAMERA, 1920, 1080);
        REQUIRE(K_K);
        CHECK(*K_K == std::array<float, 4> { 1536, 1536, 960, 540 });
        CHECK(!raster_intrinsics(K_CAMERA, 1280, 720));
        CHECK(dot(Vec3d { 0, 0, K_REVERSE ? 16. : 0. } - K_CAMERA.m_position, K_CAMERA.m_forward) ==
              8);
        CHECK(dot(Vec3d { 0, 0, K_REVERSE ? 0. : 16. } - K_CAMERA.m_position, K_CAMERA.m_forward) ==
              -8);
    }
}
