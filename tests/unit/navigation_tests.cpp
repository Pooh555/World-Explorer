#include "world_explorer/navigation/fly_camera.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <numbers>

using namespace world_explorer;

TEST_CASE("fly movement is normalized, instantaneous and independent of update frequency") {
    FlyCamera single, repeated;
    const auto K_ORIGIN = single.m_position;
    FlyInput input;
    input.m_forward = input.m_right = 1;
    input.m_captured = true;
    single.update(input, 0.1);
    for (int step { 0 }; step < 10; ++step) {
        repeated.update(input, 0.01);
    }
    REQUIRE(single.m_position.m_x == Catch::Approx(repeated.m_position.m_x));
    REQUIRE(single.m_position.m_z == Catch::Approx(repeated.m_position.m_z));
    const auto K_MOVED = single.m_position - K_ORIGIN;
    REQUIRE(std::sqrt(dot(K_MOVED, K_MOVED)) == Catch::Approx(0.2));
    input.m_forward = input.m_right = 0;
    const auto K_STOPPED = single.m_position;
    single.update(input, 0.01);
    REQUIRE(single.m_position == K_STOPPED);
}

TEST_CASE("UI ownership and invalid input cannot move the camera") {
    FlyCamera camera;
    const auto K_ORIGINAL = camera.m_position;
    FlyInput input;
    input.m_forward = 1;
    camera.update(input, 0.1);
    input.m_captured = true;
    input.m_focused = false;
    camera.update(input, 0.1);
    input.m_focused = true;
    input.m_text_input = true;
    camera.update(input, 0.1);
    input.m_text_input = false;
    input.m_mouse_x = std::numeric_limits<double>::quiet_NaN();
    camera.update(input, 0.1);
    REQUIRE(camera.m_position == K_ORIGINAL);
}

TEST_CASE("camera axes and epoch-scoped bookmark are unambiguous") {
    FlyCamera camera;
    REQUIRE(camera.forward().m_z == -1);
    REQUIRE(camera.right().m_x == 1);
    REQUIRE(camera.down().m_y == -1);
    REQUIRE(dot(cross(camera.right(), camera.down()), camera.forward()) == 1);
    CameraBookmark bookmark { { 3 }, { 7 }, camera };
    REQUIRE(restore_bookmark(bookmark, { 3 }, { 7 }));
    REQUIRE_FALSE(restore_bookmark(bookmark, { 3 }, { 8 }));
    REQUIRE_FALSE(restore_bookmark(bookmark, { 4 }, { 7 }));
}

TEST_CASE("capture up reference survives fly movement fit and bookmarks") {
    FlyCamera camera;
    camera.m_up_axis = { 0, 0, 1 };
    CHECK(camera.forward() == Vec3d { 0, 1, 0 });
    CHECK(camera.right() == Vec3d { 1, 0, 0 });
    CHECK(camera.down() == Vec3d { 0, 0, -1 });
    const auto K_ORIGIN = camera.m_position;
    FlyInput input;
    input.m_captured = true;
    input.m_up = 1;
    camera.update(input, 0.1);
    CHECK(camera.m_position.m_z == Catch::Approx(K_ORIGIN.m_z + 0.2));
    CHECK(camera.m_position.m_y == K_ORIGIN.m_y);
    camera.m_yaw = std::numbers::pi / 2;
    CHECK(camera.forward().m_x == Catch::Approx(1));
    CHECK(dot(cross(camera.right(), camera.down()), camera.forward()) == Catch::Approx(1));
    camera.fit({ { -1, -1, -1 }, { 1, 1, 1 } });
    CHECK(camera.m_up_axis == Vec3d { 0, 0, 1 });
    CHECK(camera.forward() == Vec3d { 0, 1, 0 });
    CHECK(camera.m_position.m_y < 0);
    CameraBookmark bookmark { { 3 }, { 7 }, camera };
    REQUIRE(restore_bookmark(bookmark, { 3 }, { 7 }));
    CHECK(restore_bookmark(bookmark, { 3 }, { 7 })->m_up_axis == camera.m_up_axis);
    bookmark.m_camera.m_up_axis = { 0, 0, 0 };
    CHECK_FALSE(restore_bookmark(bookmark, { 3 }, { 7 }));
}
