#include "world_explorer/render_core/camera.hpp"

#include <limits>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace world_explorer;

TEST_CASE("calibrated render cameras preserve asymmetric pixel centers and roll") {
    CameraView camera;
    camera.m_right = { 0, -1, 0 };
    camera.m_down = { -1, 0, 0 };
    camera.m_calibration = CalibratedProjection {{431, 389, 183.25, 127.75}, 640, 360};
    // Calibrated cameras do not inherit the spectator's FOV.
    camera.m_vertical_fov_radians = std::numeric_limits<double>::quiet_NaN();
    const auto K_RASTER = raster_intrinsics(camera, 640, 360);
    REQUIRE(K_RASTER);
    REQUIRE(*K_RASTER == std::array<float, 4> {431, 389, 183.75F, 128.25F});
    const auto K_WORLD = camera.m_right * 0.4 + camera.m_down * -0.3 + camera.m_forward * 2;
    const auto K_PIXEL = project(camera.m_calibration->m_intrinsics,
                                 {dot(K_WORLD, camera.m_right),
                                  dot(K_WORLD, camera.m_down),
                                  dot(K_WORLD, camera.m_forward)});
    REQUIRE(K_PIXEL);
    REQUIRE((*K_PIXEL)[0] + 0.5 == Catch::Approx((*K_RASTER)[0] * 0.4 / 2 + (*K_RASTER)[2]));
    REQUIRE((*K_PIXEL)[1] + 0.5 == Catch::Approx((*K_RASTER)[1] * -0.3 / 2 + (*K_RASTER)[3]));
    REQUIRE_FALSE(raster_intrinsics(camera, 1280, 720));
}

TEST_CASE("render camera admission rejects invalid bases, clipping and float intrinsics") {
    CameraView camera;
    SECTION("scaled basis") {
        camera.m_right.m_x = 2;
    }
    SECTION("mirrored basis") {
        camera.m_forward.m_z = 1;
    }
    SECTION("nonorthogonal basis") {
        camera.m_down = camera.m_right;
    }
    SECTION("skewed forward") {
        camera.m_forward = { 0.001, 0, -std::sqrt(1 - 0.001 * 0.001) };
    }
    SECTION("nonfinite position") {
        camera.m_position.m_x = std::numeric_limits<double>::infinity();
    }
    SECTION("invalid clipping") {
        camera.m_far_plane = camera.m_near_plane;
    }
    SECTION("invalid spectator FOV") {
        camera.m_vertical_fov_radians = 0;
    }
    SECTION("overflowing focal") {
        camera.m_calibration = CalibratedProjection {{1e300, 300, 10, 10}, 640, 360};
    }
    SECTION("underflowing focal") {
        camera.m_calibration = CalibratedProjection {{1e-300, 300, 10, 10}, 640, 360};
    }
    SECTION("nonfinite principal point") {
        camera.m_calibration = CalibratedProjection {
            {300, 300, std::numeric_limits<double>::quiet_NaN(), 10}, 640, 360};
    }
    REQUIRE_FALSE(raster_intrinsics(camera, 640, 360));
}

TEST_CASE("spectator projection stays centered across framebuffer sizes") {
    CameraView camera;
    for (const auto K_EXTENT : {std::array<std::uint32_t, 2> {640, 360}, {1280, 720}}) {
        const auto K_INTRINSICS = raster_intrinsics(camera, K_EXTENT[0], K_EXTENT[1]);
        REQUIRE(K_INTRINSICS);
        REQUIRE((*K_INTRINSICS)[0] == (*K_INTRINSICS)[1]);
        REQUIRE((*K_INTRINSICS)[2] == static_cast<float>(K_EXTENT[0]) * 0.5F);
        REQUIRE((*K_INTRINSICS)[3] == static_cast<float>(K_EXTENT[1]) * 0.5F);
        REQUIRE((*K_INTRINSICS)[1] ==
                Catch::Approx(0.5 * K_EXTENT[1] / std::tan(camera.m_vertical_fov_radians * 0.5)));
    }
    REQUIRE(!raster_intrinsics(camera, 0, 360).has_value());
}
