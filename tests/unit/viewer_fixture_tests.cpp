#include "../fixtures/viewer_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace world_explorer;

TEST_CASE("portable synthetic viewer fixture freezes the complete 250k payload and conventions") {
    const auto K_SCENE = make_portable_viewer_fixture();
    SceneLimits limits;
    limits.m_max_chunks = 4;
    limits.m_max_splats = K_VIEWER_FIXTURE_SPLATS;
    REQUIRE(validate_scene(*K_SCENE, limits));
    REQUIRE(K_SCENE->m_scale == ScaleStatus::ARBITRARY);
    REQUIRE(K_SCENE->m_provenance.at("source_kind") == "synthetic_diagnostic");
    REQUIRE(K_SCENE->m_provenance.at("asset_license") == "MIT");
    REQUIRE(K_SCENE->m_chunks.size() == 4);
    std::size_t count = 0;
    for (const auto& [K_ID, K_CHUNK] : K_SCENE->m_chunks) {
        REQUIRE(K_CHUNK->m_splats.size() == 62500);
        REQUIRE(!K_CHUNK->m_geometry_valid);
        REQUIRE(K_CHUNK->m_source_frames.empty());
        REQUIRE(K_CHUNK->m_pose_revision.m_value == 0);
        const auto& first = K_CHUNK->m_splats.front();
        const auto& last = K_CHUNK->m_splats.back();
        REQUIRE(first.m_mean == std::array<float, 3> {-3.890625F, -3.890625F, 0});
        REQUIRE(last.m_mean == std::array<float, 3> {3.890625F, 3.890625F, -.015625F});
        REQUIRE(first.m_sh_degree == 3);
        REQUIRE(last.m_sh_degree == 3);
        REQUIRE(K_CHUNK->m_world_from_chunk.m_translation.m_z ==
                -.5 * static_cast<double>(K_ID.m_value - 1));
        count += K_CHUNK->m_splats.size();
    }
    REQUIRE(count == 250000);
    REQUIRE(scene_content_hash(*K_SCENE) == K_VIEWER_FIXTURE_SHA256);
    limits.m_max_splats = 249999;
    REQUIRE(!validate_scene(*K_SCENE, limits));
    limits.m_max_splats = 250000;
    limits.m_max_chunks = 3;
    REQUIRE(!validate_scene(*K_SCENE, limits));
}

TEST_CASE(
    "portable synthetic viewer camera fixes 720p pixel centers and arbitrary world orientation") {
    const auto K_CAMERA = portable_viewer_camera();
    REQUIRE(K_CAMERA.m_position == Vec3d { 0, 0, 12 });
    REQUIRE(K_CAMERA.m_right == Vec3d { 1, 0, 0 });
    REQUIRE(K_CAMERA.m_down == Vec3d { 0, -1, 0 });
    REQUIRE(K_CAMERA.m_forward == Vec3d { 0, 0, -1 });
    REQUIRE(K_CAMERA.m_near_plane == 1);
    REQUIRE(K_CAMERA.m_far_plane == 64);
    const auto K_K = raster_intrinsics(K_CAMERA, 1280, 720);
    REQUIRE(K_K);
    REQUIRE(*K_K == std::array<float, 4> {900, 900, 640, 360});
    REQUIRE(!raster_intrinsics(K_CAMERA, 1920, 1080));
}
