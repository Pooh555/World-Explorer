#include "../gpu/clipping_fixture.hpp"

#include <bit>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

using namespace world_explorer;

TEST_CASE("synthetic clipping fixture preserves exact float neighbors and isolated image regions") {
    const auto K_SCENE = clipping_fixture();
    REQUIRE(validate_scene(*K_SCENE));
    REQUIRE(K_SCENE->m_provenance.at("source_kind") == "synthetic_diagnostic");
    REQUIRE(K_SCENE->m_chunks.size() == 1);
    const auto& splats = K_SCENE->m_chunks.begin()->second->m_splats;
    REQUIRE(splats.size() == 6);
    constexpr std::array<std::uint32_t, 6> K_BITS {
        0x3f7fffff, 0x3f800000, 0x3f800001, 0x40ffffff, 0x41000000, 0x41000001};
    const auto K_CAMERA = clipping_camera();
    const auto K_INTRINSICS = raster_intrinsics(K_CAMERA, 1280, 720);
    REQUIRE(K_INTRINSICS);
    REQUIRE(K_CAMERA.m_position.m_x == 0);
    REQUIRE(K_CAMERA.m_position.m_y == 0);
    REQUIRE(K_CAMERA.m_position.m_z == 0);
    for (std::size_t index { 0uz }; index < splats.size(); ++index) {
        const auto& splat = splats[index];
        REQUIRE(std::bit_cast<std::uint32_t>(-splat.m_mean[2]) == K_BITS[index]);
        const auto K_DEPTH = -static_cast<double>(splat.m_mean[2]);
        const auto K_X = static_cast<double>((*K_INTRINSICS)[0]) * splat.m_mean[0] / K_DEPTH +
                         (*K_INTRINSICS)[2] - 0.5;
        const auto K_Y = -static_cast<double>((*K_INTRINSICS)[1]) * splat.m_mean[1] / K_DEPTH +
                         (*K_INTRINSICS)[3] - 0.5;
        REQUIRE(K_X == 189.5 + 450 * static_cast<double>(index % 3));
        REQUIRE(K_Y == (index < 3 ? 254.5 : 464.5));
    }
    REQUIRE(!raster_intrinsics(K_CAMERA, 1280, 719));
}
