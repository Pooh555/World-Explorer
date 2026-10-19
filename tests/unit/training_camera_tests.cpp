#include "training_camera_fixture.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/reconstruction_adapters/training_camera.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <chrono>
#include <limits>

using namespace world_explorer;

namespace {

struct CameraFixture {
    std::filesystem::path m_file =
        std::filesystem::temp_directory_path() /
        ("we_training_camera_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    ~CameraFixture() {
        std::error_code error;
        std::filesystem::remove(m_file, error);
    }
};

}  // namespace

TEST_CASE("training camera import preserves asymmetric intrinsics and integer pixel centers") {
    CameraFixture fixture;
    const auto K_SIMPLE = GENERATE(false, true);
    const auto K_BYTES = test::training_camera_bytes(
        K_SIMPLE ? 0 : 1,
        241,
        131,
        K_SIMPLE ? std::array { 180., 103.25, 65.75, 0. } : std::array { 180., 190., 103.25, 65.75 });
    test::write_training_camera(fixture.m_file, K_BYTES);
    const auto K_LOADED = read_colmap_training_camera(fixture.m_file, sha256(K_BYTES));
    REQUIRE(K_LOADED);
    CHECK(K_LOADED->m_width == 241);
    CHECK(K_LOADED->m_height == 131);
    CHECK(K_LOADED->m_camera.m_fx == 180);
    CHECK(K_LOADED->m_camera.m_fy == (K_SIMPLE ? 180 : 190));
    CHECK(K_LOADED->m_camera.m_cx == 102.75);
    CHECK(K_LOADED->m_camera.m_cy == 65.25);
}

TEST_CASE("training camera import rejects rehashed malformed models limits and invalid numbers") {
    CameraFixture fixture;
    auto bytes = test::training_camera_bytes();
    SECTION("short header") {
        bytes.resize(31);
    }
    SECTION("short parameters") {
        bytes.pop_back();
    }
    SECTION("trailing bytes") {
        bytes.push_back(std::byte { 1 });
    }
    SECTION("empty camera inventory") {
        bytes[0] = std::byte { 0 };
    }
    SECTION("multiple cameras") {
        bytes[0] = std::byte { 2 };
    }
    SECTION("invalid id") {
        bytes[8] = std::byte { 0 };
    }
    SECTION("signed id") {
        bytes[11] = std::byte { 128 };
    }
    SECTION("distorted model") {
        bytes[12] = std::byte { 4 };
    }
    SECTION("unbounded dimensions") {
        bytes = test::training_camera_bytes(1, UINT64_MAX);
    }
    SECTION("tiny dimensions") {
        bytes = test::training_camera_bytes(1, 10);
    }
    SECTION("unbounded pixels") {
        bytes = test::training_camera_bytes(1, 8192, 8192);
    }
    SECTION("nonfinite focal") {
        bytes = test::training_camera_bytes(
            1, 128, 128, { std::numeric_limits<double>::infinity(), 200, 64, 64 });
    }
    SECTION("nonfinite principal point") {
        bytes = test::training_camera_bytes(
            1, 128, 128, { 200, 200, std::numeric_limits<double>::quiet_NaN(), 64 });
    }
    SECTION("negative focal") {
        bytes = test::training_camera_bytes(1, 128, 128, { -1, 200, 64, 64 });
    }
    test::write_training_camera(fixture.m_file, bytes);
    CHECK_FALSE(read_colmap_training_camera(fixture.m_file, sha256(bytes)));
}

TEST_CASE("training camera import enforces selected hash and explicit cancellation") {
    CameraFixture fixture;
    const auto K_BYTES = test::training_camera_bytes();
    test::write_training_camera(fixture.m_file, K_BYTES);
    const auto K_MISMATCH = read_colmap_training_camera(fixture.m_file, std::string(64, 'a'));
    REQUIRE_FALSE(K_MISMATCH);
    CHECK(K_MISMATCH.error().m_code == ErrorCode::INVALID_DATA);
    std::stop_source stop;
    stop.request_stop();
    const auto K_CANCELLED =
        read_colmap_training_camera(fixture.m_file, sha256(K_BYTES), stop.get_token());
    REQUIRE_FALSE(K_CANCELLED);
    CHECK(K_CANCELLED.error().m_code == ErrorCode::CANCELLED);
}
