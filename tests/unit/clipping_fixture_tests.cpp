#include "../gpu/clipping_fixture.hpp"
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>

using namespace world_explorer;

TEST_CASE("synthetic clipping fixture preserves exact float neighbors and isolated image regions") {
    const auto scene = clipping_fixture();
    REQUIRE(validate_scene(*scene));
    REQUIRE(scene->provenance.at("source_kind") == "synthetic_diagnostic");
    REQUIRE(scene->chunks.size() == 1);
    const auto& splats = scene->chunks.begin()->second->splats;
    REQUIRE(splats.size() == 6);
    constexpr std::array<std::uint32_t, 6> BITS{0x3f7fffff, 0x3f800000, 0x3f800001,
                                                0x40ffffff, 0x41000000, 0x41000001};
    const auto camera = clipping_camera();
    const auto intrinsics = raster_intrinsics(camera, 1280, 720);
    REQUIRE(intrinsics);
    REQUIRE(camera.position.x == 0);
    REQUIRE(camera.position.y == 0);
    REQUIRE(camera.position.z == 0);
    for (std::size_t index = 0; index < splats.size(); ++index) {
        const auto& splat = splats[index];
        REQUIRE(std::bit_cast<std::uint32_t>(-splat.mean[2]) == BITS[index]);
        const auto depth = -static_cast<double>(splat.mean[2]);
        const auto x =
            static_cast<double>((*intrinsics)[0]) * splat.mean[0] / depth + (*intrinsics)[2] - 0.5;
        const auto y =
            -static_cast<double>((*intrinsics)[1]) * splat.mean[1] / depth + (*intrinsics)[3] - 0.5;
        REQUIRE(x == 189.5 + 450 * static_cast<double>(index % 3));
        REQUIRE(y == (index < 3 ? 254.5 : 464.5));
    }
    REQUIRE(!raster_intrinsics(camera, 1280, 719));
}
