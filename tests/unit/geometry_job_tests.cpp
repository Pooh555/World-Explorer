#include "world_explorer/application/geometry_job.hpp"
#include "world_explorer/core/asset_files.hpp"
#include "world_explorer/core/hash.hpp"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <span>
#include <stop_token>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace world_explorer;

namespace {

struct GeometryDirectory {
    std::filesystem::path m_path {
        std::filesystem::temp_directory_path() /
        ("we-geometry-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))};

    GeometryDirectory() { std::filesystem::create_directories(m_path); }

    ~GeometryDirectory() { std::filesystem::remove_all(m_path); }
};

std::string save_request(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.close();
    REQUIRE(file);
    return sha256(std::as_bytes(std::span(text)));
}

}  // namespace

TEST_CASE("geometry adoption rejects ambiguous pinned requests without creating output") {
    const GeometryDirectory K_DIRECTORY;
    const auto K_TEXT = GENERATE(std::string("[]"),
                                 std::string("{}"),
                                 std::string("{\"schema_version\":2}"),
                                 std::string("{\"schema_version\":1.0}"),
                                 std::string("{\"schema_version\":1,\"schema_version\":1}"),
                                 std::string("{}\0{}", 5uz),
                                 std::string("{\"a\":{\"b\":1,\"b\":2}}"),
                                 std::string(34uz, '[') + "0" + std::string(34uz, ']'));
    const auto K_REQUEST = K_DIRECTORY.m_path / "request.json";
    const auto K_HASH = save_request(K_REQUEST, K_TEXT);
    const auto K_RESULT = adopt_geometry_job(K_REQUEST, K_HASH, K_DIRECTORY.m_path / "native");
    REQUIRE_FALSE(K_RESULT);
    REQUIRE(K_RESULT.error().m_code == ErrorCode::INVALID_DATA);
    REQUIRE_FALSE(std::filesystem::exists(K_DIRECTORY.m_path / "native"));
    const auto K_UNCHANGED = sha256_file(K_REQUEST, 1024uz * 1024uz);
    REQUIRE(K_UNCHANGED);
    CHECK(*K_UNCHANGED == K_HASH);
}

TEST_CASE("geometry adoption checks request pins and cancellation before ownership") {
    const GeometryDirectory K_DIRECTORY;
    const auto K_REQUEST = K_DIRECTORY.m_path / "request.json";
    const auto K_HASH = save_request(K_REQUEST, "{\"schema_version\":1}");
    SECTION("invalid or different checksum") {
        const auto K_PIN =
            GENERATE(std::string(64uz, '0'), std::string(64uz, 'A'), std::string(63uz, 'a'));
        const auto K_RESULT = adopt_geometry_job(K_REQUEST, K_PIN, K_DIRECTORY.m_path / "native");
        REQUIRE_FALSE(K_RESULT);
        CHECK(K_RESULT.error().m_code == ErrorCode::INVALID_DATA);
    }
    SECTION("pre-requested cancellation") {
        std::stop_source cancellation;
        cancellation.request_stop();
        const auto K_RESULT = adopt_geometry_job(
            K_REQUEST, K_HASH, K_DIRECTORY.m_path / "native", cancellation.get_token());
        REQUIRE_FALSE(K_RESULT);
        CHECK(K_RESULT.error().m_code == ErrorCode::CANCELLED);
    }
    CHECK_FALSE(std::filesystem::exists(K_DIRECTORY.m_path / "native"));
}

TEST_CASE("geometry adoption bounds request bytes before creating an owned job") {
    const GeometryDirectory K_DIRECTORY;
    const auto K_REQUEST = K_DIRECTORY.m_path / "request.json";
    const auto K_HASH = save_request(K_REQUEST, std::string(1024uz * 1024uz + 1uz, ' '));
    const auto K_RESULT = adopt_geometry_job(K_REQUEST, K_HASH, K_DIRECTORY.m_path / "native");
    REQUIRE_FALSE(K_RESULT);
    CHECK(K_RESULT.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
    CHECK_FALSE(std::filesystem::exists(K_DIRECTORY.m_path / "native"));
}
