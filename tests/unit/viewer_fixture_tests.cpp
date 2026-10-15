#include "../fixtures/viewer_fixture.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace world_explorer;

TEST_CASE("portable synthetic viewer fixture freezes the complete 250k payload and conventions") {
    const auto scene = make_portable_viewer_fixture();
    scene_limits limits;
    limits.max_chunks = 4;
    limits.max_splats = VIEWER_FIXTURE_SPLATS;
    REQUIRE(validate_scene(*scene, limits));
    REQUIRE(scene->scale == scale_status::ARBITRARY);
    REQUIRE(scene->provenance.at("source_kind") == "synthetic_diagnostic");
    REQUIRE(scene->provenance.at("asset_license") == "MIT");
    REQUIRE(scene->chunks.size() == 4);
    std::size_t count = 0;
    for (const auto& [id, chunk] : scene->chunks) {
        REQUIRE(chunk->splats.size() == 62500);
        REQUIRE(!chunk->geometry_valid);
        REQUIRE(chunk->source_frames.empty());
        REQUIRE(chunk->pose_revision.value == 0);
        const auto& first = chunk->splats.front();
        const auto& last = chunk->splats.back();
        REQUIRE(first.mean == std::array<float, 3>{-3.890625F, -3.890625F, 0});
        REQUIRE(last.mean == std::array<float, 3>{3.890625F, 3.890625F, -.015625F});
        REQUIRE(first.sh_degree == 3);
        REQUIRE(last.sh_degree == 3);
        REQUIRE(chunk->world_from_chunk.translation.z == -.5 * static_cast<double>(id.value - 1));
        count += chunk->splats.size();
    }
    REQUIRE(count == 250000);
    REQUIRE(scene_content_hash(*scene) == VIEWER_FIXTURE_SHA256);
    limits.max_splats = 249999;
    REQUIRE(!validate_scene(*scene, limits));
    limits.max_splats = 250000;
    limits.max_chunks = 3;
    REQUIRE(!validate_scene(*scene, limits));
}

TEST_CASE(
    "portable synthetic viewer camera fixes 720p pixel centers and arbitrary world orientation") {
    const auto camera = portable_viewer_camera();
    REQUIRE(camera.position == vec3d{0, 0, 12});
    REQUIRE(camera.right == vec3d{1, 0, 0});
    REQUIRE(camera.down == vec3d{0, -1, 0});
    REQUIRE(camera.forward == vec3d{0, 0, -1});
    REQUIRE(camera.near_plane == 1);
    REQUIRE(camera.far_plane == 64);
    const auto k = raster_intrinsics(camera, 1280, 720);
    REQUIRE(k);
    REQUIRE(*k == std::array<float, 4>{900, 900, 640, 360});
    REQUIRE(!raster_intrinsics(camera, 1920, 1080));
}
