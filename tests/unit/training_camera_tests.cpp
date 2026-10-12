#include "training_camera_fixture.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/reconstruction_adapters/training_camera.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <chrono>
#include <limits>

using namespace world_explorer;
namespace {
struct camera_fixture {
    std::filesystem::path file =
        std::filesystem::temp_directory_path() /
        ("we_training_camera_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~camera_fixture() {
        std::error_code error;
        std::filesystem::remove(file, error);
    }
};
} // namespace
TEST_CASE("training camera import preserves asymmetric intrinsics and integer pixel centers") {
    camera_fixture fixture;
    const auto simple = GENERATE(false, true);
    const auto bytes = test::training_camera_bytes(simple ? 0 : 1, 241, 131,
                                                   simple ? std::array{180., 103.25, 65.75, 0.}
                                                          : std::array{180., 190., 103.25, 65.75});
    test::write_training_camera(fixture.file, bytes);
    const auto loaded = read_colmap_training_camera(fixture.file, sha256(bytes));
    REQUIRE(loaded);
    CHECK(loaded->width == 241);
    CHECK(loaded->height == 131);
    CHECK(loaded->camera.fx == 180);
    CHECK(loaded->camera.fy == (simple ? 180 : 190));
    CHECK(loaded->camera.cx == 102.75);
    CHECK(loaded->camera.cy == 65.25);
}
TEST_CASE("training camera import rejects rehashed malformed models limits and invalid numbers") {
    camera_fixture fixture;
    auto bytes = test::training_camera_bytes();
    SECTION("short header") { bytes.resize(31); }
    SECTION("short parameters") { bytes.pop_back(); }
    SECTION("trailing bytes") { bytes.push_back(std::byte{1}); }
    SECTION("empty camera inventory") { bytes[0] = std::byte{0}; }
    SECTION("multiple cameras") { bytes[0] = std::byte{2}; }
    SECTION("invalid id") { bytes[8] = std::byte{0}; }
    SECTION("signed id") { bytes[11] = std::byte{128}; }
    SECTION("distorted model") { bytes[12] = std::byte{4}; }
    SECTION("unbounded dimensions") { bytes = test::training_camera_bytes(1, UINT64_MAX); }
    SECTION("tiny dimensions") { bytes = test::training_camera_bytes(1, 10); }
    SECTION("unbounded pixels") { bytes = test::training_camera_bytes(1, 8192, 8192); }
    SECTION("nonfinite focal") {
        bytes = test::training_camera_bytes(1, 128, 128,
                                            {std::numeric_limits<double>::infinity(), 200, 64, 64});
    }
    SECTION("nonfinite principal point") {
        bytes = test::training_camera_bytes(
            1, 128, 128, {200, 200, std::numeric_limits<double>::quiet_NaN(), 64});
    }
    SECTION("negative focal") {
        bytes = test::training_camera_bytes(1, 128, 128, {-1, 200, 64, 64});
    }
    test::write_training_camera(fixture.file, bytes);
    CHECK_FALSE(read_colmap_training_camera(fixture.file, sha256(bytes)));
}
TEST_CASE("training camera import enforces selected hash and explicit cancellation") {
    camera_fixture fixture;
    const auto bytes = test::training_camera_bytes();
    test::write_training_camera(fixture.file, bytes);
    const auto mismatch = read_colmap_training_camera(fixture.file, std::string(64, 'a'));
    REQUIRE_FALSE(mismatch);
    CHECK(mismatch.error().code == error_code::INVALID_DATA);
    std::stop_source stop;
    stop.request_stop();
    const auto cancelled =
        read_colmap_training_camera(fixture.file, sha256(bytes), stop.get_token());
    REQUIRE_FALSE(cancelled);
    CHECK(cancelled.error().code == error_code::CANCELLED);
}
