#include "../fixtures/scale_fixture.hpp"
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace world_explorer;
TEST_CASE("maximum scale fixture freezes two million positive opacity degree three splats",
          "[scale_fixture]") {
    const auto scene = make_scale_fixture();
    scene_limits limits;
    limits.max_chunks = 8;
    limits.max_splats = SCALE_FIXTURE_SPLATS;
    limits.max_payload_bytes = 512ULL * 1024 * 1024;
    const auto bytes = validate_scene(*scene, limits);
    REQUIRE(bytes);
    CHECK(*bytes == SCALE_FIXTURE_SPLATS * sizeof(gaussian));
    CHECK(scene->scale == scale_status::ARBITRARY);
    CHECK(scene->provenance.at("source_kind") == "synthetic_diagnostic");
    CHECK(scene->provenance.at("asset_license") == "MIT");
    REQUIRE(scene->chunks.size() == 8);
    std::size_t total{}, front{}, back{}, higher_degree{}, valid_points{};
    const auto camera = scale_fixture_camera();
    for (const auto& [id, chunk] : scene->chunks) {
        static_cast<void>(id);
        CHECK(chunk->splats.size() == 250000);
        CHECK(chunk->geometry_valid == false);
        CHECK(chunk->source_frames.empty());
        for (const auto& splat : chunk->splats) {
            ++total;
            higher_degree += splat.sh_degree == 3 && splat.opacity == .25F &&
                             (splat.sh[45] != 0 || splat.sh[46] != 0 || splat.sh[47] != 0);
            const vec3d point{splat.mean[0], splat.mean[1], splat.mean[2]};
            const auto delta = point - camera.position;
            const auto depth = dot(delta, camera.forward);
            if (depth < 0) {
                ++back;
                continue;
            }
            ++front;
            const auto x = 1536 * dot(delta, camera.right) / depth + 960;
            const auto y = 1536 * dot(delta, camera.down) / depth + 540;
            // Independent camera-space admission: every positive-depth centre
            // is well inside the viewport, with no clipping/opacity shortcut.
            valid_points += depth == 8 && x > 2 && x < 1918 && y > 2 && y < 1078;
        }
    }
    CHECK(total == 2000000);
    CHECK(front == 1000000);
    CHECK(back == 1000000);
    CHECK(valid_points == front);
    CHECK(higher_degree == total);
    CHECK(scene_content_hash(*scene) == SCALE_FIXTURE_SHA256);
    limits.max_splats = SCALE_FIXTURE_SPLATS - 1;
    CHECK(!validate_scene(*scene, limits));
}
TEST_CASE("maximum scale cameras have analytic full-bank coverage in both directions",
          "[scale_fixture]") {
    // The perspective covariance diagonals lie in [0.3, 0.336] for all
    // points/scales in this fixture; both extrema round to a two-pixel radius.
    const auto extent = std::min(3.33, std::sqrt(2 * std::log(.25 * 255)));
    CHECK(std::ceil(extent * std::sqrt(.3)) == 2);
    CHECK(std::ceil(extent * std::sqrt(.336)) == 2);
    std::size_t horizontal{}, vertical{};
    // Every projected ellipse has a two-pixel radius. Count crossings in exact
    // eighth-pixel integer coordinates, independently of GPU projection/scan.
    for (int index = 0; index < 1000; ++index) {
        const auto x = 7680 + (2 * index - 999) * 6;
        const auto y = 4320 - (2 * index - 999) * 3;
        horizontal += static_cast<std::size_t>((x + 16 + 127) / 128 - (x - 16) / 128);
        vertical += static_cast<std::size_t>((y + 16 + 127) / 128 - (y - 16) / 128);
    }
    CHECK(horizontal * vertical == SCALE_FIXTURE_REFERENCES);
    CHECK(SCALE_FIXTURE_REFERENCES <= 8000000);
    for (const bool reverse : {false, true}) {
        const auto camera = scale_fixture_camera(reverse);
        const auto k = raster_intrinsics(camera, 1920, 1080);
        REQUIRE(k);
        CHECK(*k == std::array<float, 4>{1536, 1536, 960, 540});
        CHECK(!raster_intrinsics(camera, 1280, 720));
        CHECK(dot(vec3d{0, 0, reverse ? 16. : 0.} - camera.position, camera.forward) == 8);
        CHECK(dot(vec3d{0, 0, reverse ? 0. : 16.} - camera.position, camera.forward) == -8);
    }
}
