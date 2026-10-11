#include "world_explorer/navigation/fly_camera.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <limits>

using namespace world_explorer;
TEST_CASE("fly movement is normalized, instantaneous and independent of update frequency") {
    fly_camera single, repeated;
    const auto origin = single.position;
    fly_input input;
    input.forward = input.right = 1;
    input.captured = true;
    single.update(input, 0.1);
    for (int step = 0; step < 10; ++step)
        repeated.update(input, 0.01);
    REQUIRE(single.position.x == Catch::Approx(repeated.position.x));
    REQUIRE(single.position.z == Catch::Approx(repeated.position.z));
    const auto moved = single.position - origin;
    REQUIRE(std::sqrt(dot(moved, moved)) == Catch::Approx(0.2));
    input.forward = input.right = 0;
    const auto stopped = single.position;
    single.update(input, 0.01);
    REQUIRE(single.position == stopped);
}
TEST_CASE("UI ownership and invalid input cannot move the camera") {
    fly_camera camera;
    const auto original = camera.position;
    fly_input input;
    input.forward = 1;
    camera.update(input, 0.1);
    input.captured = true;
    input.focused = false;
    camera.update(input, 0.1);
    input.focused = true;
    input.text_input = true;
    camera.update(input, 0.1);
    input.text_input = false;
    input.mouse_x = std::numeric_limits<double>::quiet_NaN();
    camera.update(input, 0.1);
    REQUIRE(camera.position == original);
}
TEST_CASE("camera axes and epoch-scoped bookmark are unambiguous") {
    fly_camera camera;
    REQUIRE(camera.forward().z == -1);
    REQUIRE(camera.right().x == 1);
    REQUIRE(camera.down().y == -1);
    REQUIRE(dot(cross(camera.right(), camera.down()), camera.forward()) == 1);
    camera_bookmark bookmark{{3}, {7}, camera};
    REQUIRE(restore_bookmark(bookmark, {3}, {7}));
    REQUIRE_FALSE(restore_bookmark(bookmark, {3}, {8}));
    REQUIRE_FALSE(restore_bookmark(bookmark, {4}, {7}));
}
