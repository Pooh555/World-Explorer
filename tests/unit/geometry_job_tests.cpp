#include "../../src/application/depth_job_outputs.hpp"
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
#include <map>
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
        m_path = std::filesystem::canonical(m_path);
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

struct DepthTerminalFixture {
    using Json = nlohmann::json;
    GeometryDirectory m_directory;
    std::filesystem::path m_job { m_directory.m_path / "depth" };
    std::filesystem::path m_request { m_directory.m_path / "dense.json" };
    std::string m_request_sha256;
    std::vector<PreparedDenseFrame> m_frames;
    Json m_terminal;
    std::vector<Json> m_results, m_prepared;
    std::vector<std::filesystem::path> m_stages;

    explicit DepthTerminalFixture(bool sequence = true, std::size_t count = 19uz) {
        const std::string K_WORKER(64uz, 'a');
        Json selection = Json::array();
        for (std::size_t index { 0uz }; index < count; ++index) {
            PreparedDenseFrame frame;
            frame.m_image_name = std::to_string(index) + ".png";
            frame.m_expected_depth.m_dependency.m_frame = { 9007199254740993ULL + index };
            m_frames.push_back(frame);
            if (sequence || index % 2uz == 0uz) {
                selection.push_back(index);
            }
        }
        const auto K_RECIPE = save_request(
            m_directory.m_path / "recipe.json",
            Json { { "scope", sequence ? "BOUNDED_TEMPORAL_DEPTH_SEQUENCE_V1" : "PARTIAL" },
                  { "worker_sha256", K_WORKER },
                  { "selection_indices", selection },
                  { "neighbor_radius", 3U } }
                .dump());
        const std::string K_MANIFEST(64uz, 'b');
        m_request_sha256 = save_request(m_request,
                                        Json { { "manifest_sha256", K_MANIFEST },
                                              { "recipe", "recipe.json" },
                                              { "recipe_sha256", K_RECIPE } }
                                            .dump());
        std::filesystem::create_directories(m_job / "stages");
        const auto K_OWNER = save_request(m_job / "owner.json",
                                          Json { { "schema_version", 1U },
                                                { "kind", "OWNED_RESIZED_DEPTH_JOB_V1" },
                                                { "request", m_request.string() },
                                                { "request_sha256", m_request_sha256 },
                                                { "worker_sha256", K_WORKER } }
                                              .dump());
        m_terminal = { { "schema_version", 1U },
                      { "kind",
                       sequence ? "OWNED_RESIZED_DEPTH_SEQUENCE_TERMINAL_V1"
                                : "OWNED_RESIZED_DEPTH_TERMINAL_V1" },
                      { "owner_sha256", K_OWNER },
                      { "scope", "PARTIAL_DEPTH_NO_Q5_N2_OR_WALK" } };
        if (sequence) {
            m_terminal["frame_count"] = count;
            m_terminal["batches"] = Json::array();
        }
        const auto K_BATCHES = sequence ? (count + 8uz) / 9uz : 1uz;
        for (std::size_t batch { 0uz }; batch < K_BATCHES; ++batch) {
            const auto K_NAME = "depth-" + K_OWNER +
                                (sequence ? "-batch-" + std::to_string(batch) : "") + "-attempt-0";
            const auto K_STAGE = m_job / "stages" / K_NAME;
            std::filesystem::create_directories(K_STAGE / "assets");
            const auto K_FIRST = sequence ? batch * 9uz : 0uz;
            const auto K_END = sequence ? std::min(K_FIRST + 9uz, count) : selection.size();
            const auto K_BEGIN = sequence && K_FIRST >= 3uz ? K_FIRST - 3uz : 0uz;
            const auto K_SOLVED_END = sequence ? std::min(K_END + 3uz, count) : selection.size();
            Json references = Json::array(), solved = Json::array(), records = Json::array();
            for (auto index = K_FIRST; index < K_END; ++index) {
                references.push_back(index);
            }
            for (auto index = K_BEGIN; index < K_SOLVED_END; ++index) {
                const auto K_INDEX = selection[index].get<std::size_t>();
                solved.push_back(K_INDEX);
                const auto K_ASSET = "assets/" + m_frames[K_INDEX].m_image_name + ".wedepth";
                // Synthetic opaque bytes test terminal identity, never native depth admission.
                const auto K_SHA =
                    save_request(K_STAGE / K_ASSET, "synthetic depth " + std::to_string(index));
                records.push_back(
                    { { "asset", K_ASSET },
                     { "frame", m_frames[K_INDEX].m_expected_depth.m_dependency.m_frame.m_value },
                     { "sha256", K_SHA } });
            }
            Json prepared { { "schema_version", 1U },
                           { "status", "PARTIAL_NEIGHBOR_INPUTS_PREPARED" },
                           { "manifest_sha256", K_MANIFEST },
                           { "recipe_sha256", K_RECIPE },
                           { "worker_sha256", K_WORKER } };
            Json result = prepared;
            result["status"] = "PARTIAL_NEIGHBOR_PILOT_COMPLETE";
            result["frames"] = records;
            if (sequence) {
                prepared["batch_index"] = batch;
                prepared["reference_indices"] = references;
                prepared["selection_indices"] = solved;
                result["reference_indices"] = references;
                m_terminal["batches"].push_back(
                    { { "stage", K_NAME }, { "reference_indices", references }, { "result_sha256", "" } });
            } else {
                m_terminal["stage"] = K_NAME;
                m_terminal["result_sha256"] = "";
            }
            m_results.push_back(result);
            m_prepared.push_back(prepared);
            m_stages.push_back(K_STAGE);
        }
        publish();
    }

    void publish() {
        for (std::size_t index { 0uz }; index < m_stages.size(); ++index) {
            m_results[index]["prepared_sha256"] =
                save_request(m_stages[index] / "prepared.json", m_prepared[index].dump());
            const auto K_HASH =
                save_request(m_stages[index] / "result.json", m_results[index].dump());
            if (m_terminal.contains("batches")) {
                if (index < m_terminal["batches"].size()) {
                    m_terminal["batches"][index]["result_sha256"] = K_HASH;
                }
            } else {
                m_terminal["result_sha256"] = K_HASH;
            }
        }
        save_request(m_job / "manifest.json", m_terminal.dump());
    }

    Result<Json> load(const std::stop_token& stop = {}) const {
        return job_detail::depth_job_outputs(m_job, m_request_sha256, m_request, m_frames, stop);
    }

    std::map<std::filesystem::path, std::string> snapshot() const {
        std::map<std::filesystem::path, std::string> result;
        for (const auto& entry :
             std::filesystem::recursive_directory_iterator(m_directory.m_path)) {
            if (entry.is_regular_file()) {
                result.emplace(entry.path(), sha256_file(entry.path(), 1024uz * 1024uz).value());
            }
        }
        return result;
    }
};

}  // namespace

TEST_CASE("depth terminal selects every reference once and excludes solved support halos",
          "[depth-terminal]") {
    const auto K_COUNT = GENERATE(19uz, 63uz, 120uz);
    const DepthTerminalFixture K_FIXTURE(true, K_COUNT);
    const auto K_BEFORE = K_FIXTURE.snapshot();
    const auto K_OUTPUT = K_FIXTURE.load();
    REQUIRE(K_OUTPUT);
    CHECK(K_OUTPUT->at("schema_version") == 2);
    REQUIRE(K_OUTPUT->at("outputs").size() == K_COUNT);
    for (std::size_t index { 0uz }; index < K_COUNT; ++index) {
        CHECK(K_OUTPUT->at("outputs")[index].at("frame") ==
              K_FIXTURE.m_frames[index].m_expected_depth.m_dependency.m_frame.m_value);
    }
    CHECK(K_FIXTURE.snapshot() == K_BEFORE);
    CHECK(K_FIXTURE.load().value() == *K_OUTPUT);
}

TEST_CASE("legacy depth terminal preserves its ordered noncontiguous selection",
          "[depth-terminal]") {
    const DepthTerminalFixture K_FIXTURE(false, 5uz);
    const auto K_OUTPUT = K_FIXTURE.load();
    REQUIRE(K_OUTPUT);
    CHECK(K_OUTPUT->at("schema_version") == 1);
    REQUIRE(K_OUTPUT->at("outputs").size() == 3uz);
    for (std::size_t index { 0uz }; index < 3uz; ++index) {
        CHECK(K_OUTPUT->at("outputs")[index].at("frame") ==
              K_FIXTURE.m_frames[index * 2uz].m_expected_depth.m_dependency.m_frame.m_value);
    }
}

TEST_CASE("depth sequence rejects rehashed incomplete ambiguous or stale batch graphs",
          "[depth-terminal]") {
    DepthTerminalFixture fixture;
    const auto K_MUTATION = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    switch (K_MUTATION) {
    case 0:
        fixture.m_terminal["batches"].erase(2uz);
        break;
    case 1:
        fixture.m_terminal["frame_count"] = 18U;
        break;
    case 2:
        fixture.m_terminal["batches"][1]["reference_indices"][0] = 8U;
        break;
    case 3:
        fixture.m_terminal["batches"][1]["stage"] = fixture.m_terminal["batches"][0]["stage"];
        break;
    case 4:
        fixture.m_terminal["batches"][0]["stage"] = "../caller";
        break;
    case 5:
        fixture.m_results[1]["reference_indices"][0] = 8U;
        break;
    case 6:
        fixture.m_prepared[1]["selection_indices"][0] = 7U;
        break;
    case 7:
        fixture.m_prepared[1]["batch_index"] = 0U;
        break;
    case 8:
        fixture.m_results[1]["manifest_sha256"] = std::string(64uz, 'f');
        break;
    case 9:
        fixture.m_prepared[1]["recipe_sha256"] = std::string(64uz, 'f');
        break;
    case 10:
        fixture.m_results[1]["frames"][0]["frame"] = 1U;
        break;
    case 11:
        fixture.m_results[1]["frames"][0]["asset"] = "assets/../caller.wedepth";
        break;
    case 12:
        fixture.m_results[1]["frames"].erase(0uz);
        break;
    case 13:
        fixture.m_terminal["batches"][1]["reference_indices"][0] = 9.0;
        break;
    case 14:
        fixture.m_prepared[1]["worker_sha256"] = std::string(64uz, 'f');
        break;
    case 15:
        fixture.m_terminal["schema_version"] = true;
        break;
    default:
        FAIL("Unknown depth terminal mutation");
    }
    fixture.publish();
    const auto K_BEFORE = fixture.snapshot();
    const auto K_OUTPUT = fixture.load();
    REQUIRE(K_OUTPUT.has_value() == false);
    CHECK(K_OUTPUT.error().m_code == ErrorCode::INVALID_DATA);
    CHECK(fixture.snapshot() == K_BEFORE);
}

TEST_CASE("depth terminal checks unused support bytes pins paths and cancellation",
          "[depth-terminal]") {
    DepthTerminalFixture fixture;
    SECTION("corrupted support-only halo in a later batch") {
        const auto K_NAME = fixture.m_results[1]["frames"][0]["asset"].get<std::string>();
        save_request(fixture.m_stages[1] / K_NAME, "caller corruption");
    }
    SECTION("changed prepared bytes") {
        save_request(fixture.m_stages[1] / "prepared.json", "{}");
    }
    SECTION("changed result bytes") {
        save_request(fixture.m_stages[1] / "result.json", "{}");
    }
    SECTION("wrong request identity") {
        fixture.m_request_sha256 = std::string(64uz, 'f');
    }
    const auto K_BEFORE = fixture.snapshot();
    CHECK(fixture.load().has_value() == false);
    std::stop_source cancellation;
    cancellation.request_stop();
    const auto K_CANCELLED = fixture.load(cancellation.get_token());
    REQUIRE(K_CANCELLED.has_value() == false);
    CHECK(K_CANCELLED.error().m_code == ErrorCode::CANCELLED);
    CHECK(fixture.snapshot() == K_BEFORE);
}

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
#else
TEST_CASE("geometry solving reports unsupported before accessing caller files") {
    const GeometryDirectory K_DIRECTORY;
    const auto K_RESULT = run_geometry_job(
        K_DIRECTORY.m_path / "missing.json", std::string(64uz, '0'), K_DIRECTORY.m_path / "output");
    REQUIRE_FALSE(K_RESULT);
    CHECK(K_RESULT.error().m_code == ErrorCode::BACKEND_FAILURE);
    CHECK(std::filesystem::is_empty(K_DIRECTORY.m_path));
}
#endif
