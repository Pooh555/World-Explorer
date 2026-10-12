#include "world_explorer/render_core/camera.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <limits>

using namespace world_explorer;
TEST_CASE("calibrated render cameras preserve asymmetric pixel centers and roll") {
    camera_view camera;
    camera.right = {0, -1, 0};
    camera.down = {-1, 0, 0};
    camera.calibration = calibrated_projection{{431, 389, 183.25, 127.75}, 640, 360};
    // Calibrated cameras do not inherit the spectator's FOV.
    camera.vertical_fov_radians = std::numeric_limits<double>::quiet_NaN();
    const auto raster = raster_intrinsics(camera, 640, 360);
    REQUIRE(raster);
    REQUIRE(*raster == std::array<float, 4>{431, 389, 183.75F, 128.25F});
    const auto world = camera.right * 0.4 + camera.down * -0.3 + camera.forward * 2;
    const auto pixel =
        project(camera.calibration->intrinsics,
                {dot(world, camera.right), dot(world, camera.down), dot(world, camera.forward)});
    REQUIRE(pixel);
    REQUIRE((*pixel)[0] + 0.5 == Catch::Approx((*raster)[0] * 0.4 / 2 + (*raster)[2]));
    REQUIRE((*pixel)[1] + 0.5 == Catch::Approx((*raster)[1] * -0.3 / 2 + (*raster)[3]));
    REQUIRE_FALSE(raster_intrinsics(camera, 1280, 720));
}
TEST_CASE("render camera admission rejects invalid bases, clipping and float intrinsics") {
    camera_view camera;
    SECTION("scaled basis") { camera.right.x = 2; }
    SECTION("mirrored basis") { camera.forward.z = 1; }
    SECTION("nonorthogonal basis") { camera.down = camera.right; }
    SECTION("skewed forward") { camera.forward = {0.001, 0, -std::sqrt(1 - 0.001 * 0.001)}; }
    SECTION("nonfinite position") { camera.position.x = std::numeric_limits<double>::infinity(); }
    SECTION("invalid clipping") { camera.far_plane = camera.near_plane; }
    SECTION("invalid spectator FOV") { camera.vertical_fov_radians = 0; }
    SECTION("overflowing focal") {
        camera.calibration = calibrated_projection{{1e300, 300, 10, 10}, 640, 360};
    }
    SECTION("underflowing focal") {
        camera.calibration = calibrated_projection{{1e-300, 300, 10, 10}, 640, 360};
    }
    SECTION("nonfinite principal point") {
        camera.calibration = calibrated_projection{
            {300, 300, std::numeric_limits<double>::quiet_NaN(), 10}, 640, 360};
    }
    REQUIRE_FALSE(raster_intrinsics(camera, 640, 360));
}
TEST_CASE("spectator projection stays centered across framebuffer sizes") {
    camera_view camera;
    for (const auto extent : {std::array<std::uint32_t, 2>{640, 360}, {1280, 720}}) {
        const auto intrinsics = raster_intrinsics(camera, extent[0], extent[1]);
        REQUIRE(intrinsics);
        REQUIRE((*intrinsics)[0] == (*intrinsics)[1]);
        REQUIRE((*intrinsics)[2] == static_cast<float>(extent[0]) * 0.5F);
        REQUIRE((*intrinsics)[3] == static_cast<float>(extent[1]) * 0.5F);
        REQUIRE((*intrinsics)[1] ==
                Catch::Approx(0.5 * extent[1] / std::tan(camera.vertical_fov_radians * 0.5)));
    }
    REQUIRE(!raster_intrinsics(camera, 0, 360).has_value());
}
