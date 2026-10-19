#include "world_explorer/application/geometry_job.hpp"
#include "world_explorer/core/asset_files.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/map_store/pose_asset.hpp"
#include "world_explorer/reconstruction_adapters/process.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <span>
#include <stop_token>
#include <string>

using namespace world_explorer;

namespace {

struct GeometryDirectory {
    std::filesystem::path m_path {
        std::filesystem::temp_directory_path() /
        ("we-geometry-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())) };

    GeometryDirectory() {
        std::filesystem::create_directories(m_path);
    }

    ~GeometryDirectory() {
        std::filesystem::remove_all(m_path);
    }
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

TEST_CASE("geometry process supervision rejects unbounded termination deadlines before launch") {
    ProcessControl control;
    control.m_termination_grace =
        GENERATE(std::chrono::milliseconds { -1 }, std::chrono::milliseconds { 5001 });
    const auto K_RESULT = execute_process({ "must-not-launch" }, {}, {}, {}, control);
    REQUIRE_FALSE(K_RESULT);
    CHECK(K_RESULT.error().m_code == ErrorCode::INVALID_ARGUMENT);
}

#if defined(__linux__)
TEST_CASE("fresh geometry rejects incomplete or ambiguous requests before ownership") {
    const GeometryDirectory K_DIRECTORY;
    const auto K_TEXT =
        GENERATE(std::string("{\"schema_version\":1,\"kind\":\"FRESH_GEOMETRY_PIPELINE_V1\"}"),
                 std::string("{\"schema_version\":1.0,\"kind\":\"FRESH_GEOMETRY_PIPELINE_V1\"}"),
                 std::string("{\"schema_version\":1,\"kind\":\"FRESH_GEOMETRY_PIPELINE_V1\","
                             "\"kind\":\"FRESH_GEOMETRY_PIPELINE_V1\"}"));
    const auto K_REQUEST = K_DIRECTORY.m_path / "request.json";
    const auto K_HASH = save_request(K_REQUEST, K_TEXT);
    const auto K_RESULT = run_geometry_job(K_REQUEST, K_HASH, K_DIRECTORY.m_path / "fresh");
    REQUIRE_FALSE(K_RESULT);
    CHECK(K_RESULT.error().m_code == ErrorCode::INVALID_DATA);
    CHECK_FALSE(std::filesystem::exists(K_DIRECTORY.m_path / "fresh"));
    CHECK(sha256_file(K_REQUEST, 1024uz * 1024uz).value() == K_HASH);
}

TEST_CASE("geometry pipeline rejects ambiguous pinned requests without creating output") {
    const GeometryDirectory K_DIRECTORY;
    const auto K_TEXT = GENERATE(std::string("[]"),
                                 std::string("{}"),
                                 std::string("{\"schema_version\":1,\"schema_version\":1}"),
                                 std::string("{}\0{}", 5uz),
                                 std::string(1024uz * 1024uz + 1uz, ' '));
    const auto K_REQUEST = K_DIRECTORY.m_path / "request.json";
    const auto K_HASH = save_request(K_REQUEST, K_TEXT);
    CHECK_FALSE(run_geometry_job(K_REQUEST, K_HASH, K_DIRECTORY.m_path / "pipeline"));
    CHECK_FALSE(std::filesystem::exists(K_DIRECTORY.m_path / "pipeline"));
    CHECK(sha256_file(K_REQUEST, 2uz * 1024uz * 1024uz).value() == K_HASH);
}

TEST_CASE("geometry pipeline owns cancellation and preserves partial solver attempts") {
    const GeometryDirectory K_DIRECTORY;
    const auto K_ROOT = K_DIRECTORY.m_path;
    MapStore store;
    MapTransaction transaction;
    transaction.m_new_revision = { 1 };
    transaction.m_operation = { 1 };
    transaction.m_calibrations = { { { 3 }, { 2, 2, .5, .5 }, 2, 2 } };
    transaction.m_observations = {
        { { 7 }, { 1 }, { 3 }, { 0, 1, 30 }, std::chrono::nanoseconds { 0 }, std::string(64uz, 'b') } };
    transaction.m_alignments = { { { 1 }, { 1 }, {} } };
    transaction.m_poses = {
        { { 7 }, { 1 }, {}, std::chrono::nanoseconds { 0 }, { 1 }, TrackingState::TRACKING, {} } };
    REQUIRE(store.commit(transaction));
    const auto K_POSE = write_pose_asset(
        K_ROOT / "poses.weposes",
        { store.acquire_snapshot(),
         { std::string(64uz, '1'), std::string(64uz, '2'), std::string(64uz, '3'), {} } });
    REQUIRE(K_POSE);
    std::filesystem::create_directory(K_ROOT / "cache");
    save_request(K_ROOT / "recipe.json", "{}");
    const auto K_FUSION =
        save_request(K_ROOT / "fusion.json", "{\"cache\":\"cache\",\"recipe\":\"recipe.json\"}");
    const auto K_SCRIPT = save_request(K_ROOT / "coordinator.py",
                                       "import pathlib,sys,time\n"
                                       "p=pathlib.Path(sys.argv[5]);p.mkdir(exist_ok=True)\n"
                                       "(p/'partial').write_bytes(b'partial solver data')\n"
                                       "print('READY',flush=True)\n"
                                       "time.sleep(30)\n");
    const auto K_CONVERTER =
        save_request(K_ROOT / "converter.py", "raise RuntimeError('must not launch')\n");
    nlohmann::json configuration { { "schema_version", 1 },
                                  { "kind", "RETAINED_GEOMETRY_PIPELINE_V1" },
                                  { "fusion_request", "fusion.json" },
                                  { "fusion_request_sha256", K_FUSION },
                                  { "poses", "poses.weposes" },
                                  { "pose_sha256", *K_POSE },
                                  { "python", WE_TEST_PYTHON },
                                  { "coordinator", "coordinator.py" },
                                  { "coordinator_sha256", K_SCRIPT },
                                  { "converter", "converter.py" },
                                  { "converter_sha256", K_CONVERTER } };
    const auto K_REQUEST = K_ROOT / "request.json";
    const auto K_HASH = save_request(K_REQUEST, configuration.dump());
    const auto K_OUTPUT = K_ROOT / "pipeline";
    SECTION("wrong script pin rejects before ownership") {
        configuration["coordinator_sha256"] = std::string(64uz, 'a');
        const auto K_CHANGED = save_request(K_REQUEST, configuration.dump());
        CHECK_FALSE(run_geometry_job(K_REQUEST, K_CHANGED, K_OUTPUT));
        CHECK_FALSE(std::filesystem::exists(K_OUTPUT));
    }
    SECTION("foreign occupied output rejects without a lock") {
        std::filesystem::create_directory(K_OUTPUT);
        save_request(K_OUTPUT / "foreign", "caller bytes");
        CHECK_FALSE(run_geometry_job(K_REQUEST, K_HASH, K_OUTPUT));
        CHECK_FALSE(std::filesystem::exists(K_OUTPUT / "writer.lock"));
        CHECK(std::filesystem::file_size(K_OUTPUT / "foreign") == 12uz);
    }
    SECTION("output cannot contain indirect cache inputs") {
        CHECK_FALSE(run_geometry_job(K_REQUEST, K_HASH, K_ROOT / "cache"));
        CHECK(std::filesystem::is_empty(K_ROOT / "cache"));
    }
    SECTION("active child cancellation retains its populated attempt") {
        std::stop_source cancellation;
        bool reached {};
        const auto K_RESULT = run_geometry_job(
            K_REQUEST, K_HASH, K_OUTPUT, cancellation.get_token(), [&](const std::string& text) {
                if (text.contains("READY")) {
                    reached = true;
                    cancellation.request_stop();
                }
            });
        REQUIRE_FALSE(K_RESULT);
        CHECK(K_RESULT.error().m_code == ErrorCode::CANCELLED);
        CHECK(reached);
        CHECK(std::filesystem::file_size(K_OUTPUT / "stages/solver/partial") == 19uz);
        CHECK_FALSE(std::filesystem::exists(K_OUTPUT / "adoption.json"));
        CHECK_FALSE(std::filesystem::exists(K_OUTPUT / "stages/native"));
        CHECK(sha256_file(K_ROOT / "poses.weposes", 1024uz * 1024uz).value() == *K_POSE);
    }
}
#endif
