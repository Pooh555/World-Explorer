#include "inspection_fixture.hpp"
#include "reconstruction_fixture.hpp"
#include "stage_chain_fixture.hpp"
#include "training_camera_fixture.hpp"
#include "world_explorer/application/pose_preparation.hpp"
#include "world_explorer/application/reconstruction_job.hpp"
#include "world_explorer/application/scenes.hpp"
#include "world_explorer/core/hash.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <limits>
#include <stdexcept>

using namespace world_explorer;

namespace {

void save_json(const std::filesystem::path& path, const nlohmann::json& value) {
    std::ofstream file(path);
    file << value.dump();
}

using JobFixture = test::reconstruction_fixture;

}  // namespace

TEST_CASE(
    "coordinator reuses an explicitly selected fit and preserves publication on changed inputs") {
    JobFixture fixture;
    std::string log;
    const auto K_INITIAL = reconstruct(fixture.job, {}, [&](const std::string& text) {
        log += text;
    });
    INFO((K_INITIAL ? "published" : K_INITIAL.error().m_message));
    REQUIRE(K_INITIAL);
    REQUIRE(log.find("optimization skipped") != std::string::npos);
    const auto K_SCENE = load_bundle(fixture.job.m_scene_output).value();
    CHECK(K_SCENE->m_provenance.at("fit_identity") == fixture.identity);
    CHECK(K_SCENE->m_provenance.at("map_revision") == "1");
    CHECK(K_SCENE->m_provenance.at("map_epoch") == "0");
    CHECK(K_SCENE->m_provenance.at("pose_asset_sha256").size() == 64);
    CHECK(K_SCENE->m_chunks.begin()->second->m_source_frames.size() == 3);
    const auto K_MANIFEST = fixture.job.m_scene_output / "manifest.json";
    const auto K_COMMITTED = sha256_file(K_MANIFEST, 65536).value();
    SECTION("different completed identity") {
        fixture.job.m_completed_fit_identity = std::string(64, 'c');
    }
    SECTION("different recipe settings") {
        fixture.job.m_max_image_dimension = 640;
    }
    SECTION("fit from a different camera solve") {
        const auto K_PATH = fixture.job.m_fit_directory / "input_manifest.json";
        auto input = nlohmann::json::parse(std::ifstream(K_PATH));
        input["dataset"]["frames"][0]["view"][0][3] = 100.;
        save_json(K_PATH, input);
    }
    SECTION("different fit focal length") {
        const auto K_PATH = fixture.job.m_fit_directory / "input_manifest.json";
        auto input = nlohmann::json::parse(std::ifstream(K_PATH));
        input["dataset"]["frames"][0]["intrinsic"][0][0] = 201.;
        save_json(K_PATH, input);
    }
    SECTION("wrong half-pixel convention") {
        const auto K_PATH = fixture.job.m_fit_directory / "input_manifest.json";
        auto input = nlohmann::json::parse(std::ifstream(K_PATH));
        input["dataset"]["frames"][0]["intrinsic"][0][2] = 63.5;
        save_json(K_PATH, input);
    }
    SECTION("wrong original image domain") {
        const auto K_PATH = fixture.job.m_fit_directory / "input_manifest.json";
        auto input = nlohmann::json::parse(std::ifstream(K_PATH));
        input["dataset"]["frames"][0]["original_width"] = 127;
        save_json(K_PATH, input);
    }
    SECTION("wrong resized domain") {
        const auto K_PATH = fixture.job.m_fit_directory / "input_manifest.json";
        auto input = nlohmann::json::parse(std::ifstream(K_PATH));
        input["dataset"]["frames"][0]["width"] = 127;
        save_json(K_PATH, input);
    }
    SECTION("rehashed invalid training camera") {
        test::write_training_camera(fixture.stage / "training/sparse/cameras.bin",
                                    test::training_camera_bytes(4));
        fixture.poses.inventory(fixture.stage);
    }
    SECTION("incomplete camera inventory") {
        const auto K_PATH = fixture.job.m_fit_directory / "input_manifest.json";
        auto input = nlohmann::json::parse(std::ifstream(K_PATH));
        input["dataset"]["frames"].erase(0);
        save_json(K_PATH, input);
    }
    SECTION("changed source") {
        std::ofstream(fixture.job.m_video) << "different source";
    }
    SECTION("changed image") {
        std::ofstream(fixture.stage / "training/images/0.png") << "corrupt";
    }
    SECTION("extra uncommitted file") {
        std::ofstream(fixture.stage / "extra") << "uncommitted";
    }
    SECTION("escaping pose pointer") {
        save_json(fixture.job.m_pose_job / "poses.json",
                  { { "key", std::string(64, 'a') }, { "output", fixture.root.string() } });
    }
    SECTION("cancel before publication") {
        std::stop_source cancellation;
        cancellation.request_stop();
        const auto K_RESULT = reconstruct(fixture.job, cancellation.get_token(), {});
        REQUIRE_FALSE(K_RESULT);
        CHECK(K_RESULT.error().m_code == ErrorCode::CANCELLED);
        CHECK(sha256_file(K_MANIFEST, 65536).value() == K_COMMITTED);
        return;
    }
    SECTION("cancel at compilation boundary") {
        std::stop_source cancellation;
        const auto K_RESULT =
            reconstruct(fixture.job, cancellation.get_token(), [&](const std::string& text) {
                if (text.find("Compiling checked fit") != std::string::npos) {
                    cancellation.request_stop();
                }
            });
        REQUIRE_FALSE(K_RESULT);
        CHECK(K_RESULT.error().m_code == ErrorCode::CANCELLED);
        CHECK(sha256_file(K_MANIFEST, 65536).value() == K_COMMITTED);
        return;
    }
    REQUIRE_FALSE(reconstruct(fixture.job, {}, {}));
    CHECK(sha256_file(K_MANIFEST, 65536).value() == K_COMMITTED);
}

TEST_CASE(
    "reconstruction request resolves relative paths and rejects implicit or malformed recipes") {
    JobFixture fixture;
    const auto K_REQUEST = fixture.root / "request.json";
    nlohmann::json config { { "schema_version", 1 },
                           { "recipe", "local_gsplat_v1" },
                           { "action", "reuse_completed" },
                           { "video", "capture.mp4" },
                           { "pose_job", "poses" },
                           { "fit_directory", "fit" },
                           { "scene_output", "scene.wexplore" },
                           { "map_directory", "shared-map" },
                           { "decode_stage", "poses/stages/decode-" + fixture.poses.decode_key },
                           { "steps", 7000 },
                           { "max_image_dimension", 1280 },
                           { "max_splats", 2000000 },
                           { "completed_fit_identity", fixture.identity } };
    save_json(K_REQUEST, config);
    const auto K_PARSED = read_reconstruction_job(K_REQUEST);
    REQUIRE(K_PARSED);
    CHECK(std::filesystem::equivalent(K_PARSED->m_video, fixture.job.m_video));
    CHECK(K_PARSED->m_map_directory == std::filesystem::canonical(fixture.root) / "shared-map");
    CHECK(std::filesystem::equivalent(K_PARSED->m_decode_stage, fixture.poses.decoded));
    SECTION("unknown field") {
        config["remote_fallback"] = true;
    }
    SECTION("different recipe") {
        config["recipe"] = "standard_nvidia";
    }
    SECTION("missing recipe") {
        config.erase("recipe");
    }
    SECTION("signed resource") {
        config["max_splats"] = -1;
    }
    SECTION("fractional resource") {
        config["steps"] = 7000.5;
    }
    SECTION("overflow") {
        config["max_image_dimension"] = 4294967297ULL;
    }
    SECTION("output overwrites source") {
        config["scene_output"] = "capture.mp4";
    }
    SECTION("output overwrites inputs") {
        config["scene_output"] = ".";
    }
    SECTION("output mutates pose stage") {
        config["scene_output"] = "poses/scene.wexplore";
    }
    SECTION("output mutates fit") {
        config["scene_output"] = "fit/scene.wexplore";
    }
    SECTION("map overlaps source") {
        config["map_directory"] = "capture.mp4";
    }
    SECTION("map mutates pose job") {
        config["map_directory"] = "poses/map";
    }
    SECTION("map mutates fit") {
        config["map_directory"] = "fit/map";
    }
    SECTION("map overlaps scene") {
        config["map_directory"] = "scene.wexplore";
    }
    SECTION("map includes all outputs") {
        config["map_directory"] = ".";
    }
    SECTION("start would clobber completed fit") {
        config["action"] = "start";
    }
    SECTION("reuse needs explicit identity") {
        config.erase("completed_fit_identity");
    }
    save_json(K_REQUEST, config);
    CHECK_FALSE(read_reconstruction_job(K_REQUEST));
}

TEST_CASE("coordinator worker failure and missing completion cannot replace an existing scene") {
    JobFixture fixture;
    REQUIRE(reconstruct(fixture.job, {}, {}));
    const auto K_MANIFEST = fixture.job.m_scene_output / "manifest.json";
    const auto K_COMMITTED = sha256_file(K_MANIFEST, 65536).value();
    fixture.job.m_action = FitAction::START;
    fixture.job.m_completed_fit_identity.clear();
    fixture.job.m_fit_directory = fixture.root / "fresh-fit";
    fixture.job.m_gsplat_source = fixture.root / "backend source with spaces";
    fixture.job.m_fitting_worker = { fixture.root / "worker.py", WE_TEST_PYTHON, false };
    int exit_code = 7;
    SECTION("backend nonzero exit") { }
    SECTION("zero exit without a fit result") {
        exit_code = 0;
    }
    std::ofstream(fixture.job.m_fitting_worker.m_script)
        << "import sys\nprint('diagnostic backend output')\nsys.exit(" << exit_code << ")\n";
    const auto K_RESULT = reconstruct(fixture.job, {}, {});
    REQUIRE_FALSE(K_RESULT);
    if (exit_code) {
        CHECK(K_RESULT.error().m_code == ErrorCode::BACKEND_FAILURE);
    }
    CHECK(sha256_file(K_MANIFEST, 65536).value() == K_COMMITTED);
}

TEST_CASE("coordinator forwards bounded start and resume arguments and rechecks inputs after "
          "worker completion") {
    JobFixture fixture;
    REQUIRE(reconstruct(fixture.job, {}, {}));
    const auto K_MANIFEST = fixture.job.m_scene_output / "manifest.json";
    const auto K_COMMITTED = sha256_file(K_MANIFEST, 65536).value();
    const auto K_PREVIOUS_FIT = fixture.job.m_fit_directory;
    fixture.job.m_fit_directory = fixture.root / "new fit";
    fixture.job.m_gsplat_source = fixture.root / "source $literal ; with spaces";
    fixture.job.m_fitting_worker = { fixture.root / "worker.py", WE_TEST_PYTHON, false };
    fixture.job.m_action = FitAction::START;
    fixture.job.m_completed_fit_identity.clear();
    bool change_source = false;
    SECTION("start publishes a checked diagnostic result") { }
    SECTION("resume forwards explicit checkpoint") {
        fixture.job.m_action = FitAction::RESUME;
        fixture.job.m_checkpoint = fixture.root / "checkpoint with spaces.pt";
        std::ofstream(fixture.job.m_checkpoint) << "diagnostic checkpoint";
    }
    SECTION("source changed by worker prevents publication") {
        change_source = true;
    }
    // A synthetic subprocess contract fixture, never a reconstruction gate.
    const auto K_LITERAL = [](const std::filesystem::path& path) {
        return nlohmann::json(path.string()).dump();
    };
    std::ofstream script(fixture.job.m_fitting_worker.m_script);
    script
        << "import argparse, shutil\nfrom pathlib import Path\np=argparse.ArgumentParser()\n"
           "for k in "
           "('source','data','video','output','steps','max-image-dimension','max-splats'):\n"
           " p.add_argument('--'+k,required=True)\np.add_argument('--resume')\na=p.parse_args()\n"
        << "assert a.source == " << K_LITERAL(fixture.job.m_gsplat_source) << "\n"
        << "assert Path(a.data).samefile(" << K_LITERAL(fixture.stage / "training") << ")\n"
        << "assert a.video == " << K_LITERAL(fixture.job.m_video) << "\n"
        << "assert a.steps == '7000' and a.max_image_dimension == '1280' and a.max_splats == "
           "'2000000'\n"
        << "assert a.resume == "
        << (fixture.job.m_checkpoint.empty() ? std::string("None")
                                             : K_LITERAL(fixture.job.m_checkpoint))
        << "\n"
        << "shutil.copytree(" << K_LITERAL(K_PREVIOUS_FIT) << ",a.output)\n";
    if (change_source) {
        script << "Path(a.video).write_text('changed capture')\n";
    }
    script << "print('synthetic subprocess contract fixture')\n";
    script.close();
    std::string worker_log;
    const auto K_RESULT = reconstruct(fixture.job, {}, [&](const std::string& text) {
        worker_log += text;
    });
    INFO(worker_log);
    INFO((K_RESULT ? "published" : K_RESULT.error().m_message));
    REQUIRE(worker_log.find("synthetic subprocess contract fixture") != std::string::npos);
    if (change_source) {
        REQUIRE_FALSE(K_RESULT);
        CHECK(K_RESULT.error().m_code == ErrorCode::INVALID_DATA);
        CHECK(sha256_file(K_MANIFEST, 65536).value() == K_COMMITTED);
    } else {
        REQUIRE(K_RESULT);
        const auto K_SCENE = load_bundle(*K_RESULT);
        REQUIRE(K_SCENE);
        CHECK((*K_SCENE)->m_provenance.at("fit_identity") == fixture.identity);
    }
}

TEST_CASE("pose preparation reopens identical dependencies and holds the writer through use",
          "[poses][persistence][application]") {
    JobFixture fixture;
    const PosePreparationRequest K_REQUEST { { fixture.job.m_pose_job, fixture.job.m_video, {} },
                                            fixture.root / "prepared-map" };
    auto prepared = prepare_completed_pose_map(K_REQUEST, {});
    REQUIRE(prepared);
    const auto K_HASH = prepared->m_asset_sha256;
    const auto K_SNAPSHOT = prepared->m_store->acquire_snapshot();
    CHECK(K_SNAPSHOT->m_initial_pose_asset_sha256 == K_HASH);
    CHECK(K_SNAPSHOT->m_poses.size() == 3);
    CHECK(prepared->m_provenance.m_source_sha256 == fixture.source_hash);
    CHECK(read_pose_asset(K_REQUEST.m_map_directory / "assets" / ("poses-" + K_HASH + ".weposes"),
                          K_HASH));
    CHECK_FALSE(prepare_completed_pose_map(K_REQUEST, {}));
    const auto K_MANIFEST = K_REQUEST.m_map_directory / "manifest.json";
    const auto K_BEFORE = sha256_file(K_MANIFEST, 65536).value();
    prepared->m_store.reset();
    auto reopened = prepare_completed_pose_map(K_REQUEST, {});
    REQUIRE(reopened);
    CHECK(reopened->m_asset_sha256 == K_HASH);
    CHECK(sha256_file(K_MANIFEST, 65536).value() == K_BEFORE);
    CHECK(K_SNAPSHOT->m_poses.size() == 3);
    reopened->m_store.reset();
    SECTION("different canonical dependency is never reseeded") {
        fixture.poses.canonical["poses"][0]["world_from_camera"]["translation"][0] = 42.;
        fixture.poses.publish();
    }
    SECTION("corrections require explicit resync") {
        auto store = MapStore::open_persistent({ .m_directory = K_REQUEST.m_map_directory }).value();
        MapTransaction transaction;
        transaction.m_operation = { 2 };
        transaction.m_base_revision = { 1 };
        transaction.m_new_revision = { 2 };
        auto pose = K_SNAPSHOT->m_poses.begin()->second;
        pose.m_revision = { 2 };
        pose.m_submap_from_camera.m_translation.m_x += 1.;
        transaction.m_poses.push_back(pose);
        REQUIRE(store->commit(transaction));
    }
    SECTION("new epoch requires explicit resync") {
        auto store = MapStore::open_persistent({ .m_directory = K_REQUEST.m_map_directory }).value();
        REQUIRE(store->reset({ 1 }));
    }
    SECTION("corrupt manifest is never reset") {
        std::ofstream(K_MANIFEST) << "broken";
    }
    const auto K_PRESERVED = sha256_file(K_MANIFEST, 65536).value();
    CHECK_FALSE(prepare_completed_pose_map(K_REQUEST, {}));
    CHECK(sha256_file(K_MANIFEST, 65536).value() == K_PRESERVED);
}

TEST_CASE("coordinator cancellation retains durable preparation and restart reuses it",
          "[poses][persistence][application]") {
    JobFixture fixture;
    fixture.job.m_map_directory = fixture.root / "shared-map";
    const auto K_POINTER = fixture.job.m_pose_job / "decode.json";
    save_json(K_POINTER, { { "key", std::string(64, 'd') }, { "output", "missing stale stage" } });
    const auto K_POINTER_HASH = sha256_file(K_POINTER, 65536).value();
    fixture.job.m_decode_stage = fixture.poses.decoded;
    std::stop_source cancellation;
    const auto K_INTERRUPTED =
        reconstruct(fixture.job, cancellation.get_token(), [&](const std::string& text) {
            if (text.find("Durable pose map ready") != std::string::npos) {
                cancellation.request_stop();
            }
        });
    REQUIRE_FALSE(K_INTERRUPTED);
    CHECK(K_INTERRUPTED.error().m_code == ErrorCode::CANCELLED);
    CHECK_FALSE(std::filesystem::exists(fixture.job.m_scene_output / "manifest.json"));
    const auto K_MANIFEST = fixture.job.m_map_directory / "manifest.json";
    const auto K_COMMITTED = sha256_file(K_MANIFEST, 65536).value();
    REQUIRE(reconstruct(fixture.job, {}, {}));
    CHECK(sha256_file(K_MANIFEST, 65536).value() == K_COMMITTED);
    CHECK(sha256_file(K_POINTER, 65536).value() == K_POINTER_HASH);
    const auto K_SCENE = load_bundle(fixture.job.m_scene_output).value();
    auto map = MapStore::open_persistent({ .m_directory = fixture.job.m_map_directory }).value();
    CHECK(map->acquire_snapshot()->m_initial_pose_asset_sha256 ==
          K_SCENE->m_provenance.at("pose_asset_sha256"));
    map.reset();
    CHECK_FALSE(reconstruct(fixture.job, {}, [&](const std::string& text) {
        if (text.find("Durable pose map ready") != std::string::npos) {
            auto metadata =
                nlohmann::json::parse(std::ifstream(fixture.poses.decoded / "result.json"));
            metadata["source_frame_count"] = 31;
            save_json(fixture.poses.decoded / "result.json", metadata);
            fixture.poses.inventory(fixture.poses.decoded);
        }
    }));
    CHECK(load_bundle(fixture.job.m_scene_output).value()->m_provenance == K_SCENE->m_provenance);
    CHECK(sha256_file(K_MANIFEST, 65536).value() == K_COMMITTED);
}

TEST_CASE("schema 2 requires explicit checked inspection and retained pose adoption",
          "[preflight]") {
    JobFixture fixture;
    fixture.adopt_inspection();
    const auto K_REQUEST = fixture.root / "request.json";
    auto config = nlohmann::json {
        { "schema_version", 2 },
        { "recipe", "local_gsplat_v1" },
        { "action", "reuse_completed" },
        { "pose_action", "adopt_completed" },
        { "video", "capture.mp4" },
        { "pose_job", "poses" },
        { "fit_directory", "fit" },
        { "scene_output", "scene.wexplore" },
        { "steps", 7000 },
        { "max_image_dimension", 1280 },
        { "max_splats", 2000000 },
        { "completed_fit_identity", fixture.identity },
        { "inspection",
         { { "action", "adopt_completed" },
          { "stage", test::path_utf8(fixture.inspection().m_stage.lexically_relative(fixture.root)) },
          { "completion_sha256", fixture.inspection().m_completion_sha256 },
          { "minimum_free_bytes", 1 } } } };
    save_json(K_REQUEST, config);
    auto parsed = read_reconstruction_job(K_REQUEST);
    REQUIRE(parsed);
    REQUIRE(parsed->m_inspection);
    if (parsed->m_inspection) {
        CHECK(std::filesystem::equivalent(parsed->m_inspection->m_stage,
                                          fixture.inspection().m_stage));
    }
    REQUIRE(reconstruct(*parsed, {}, {}));
    SECTION("implicit pose choice") {
        config.erase("pose_action");
    }
    SECTION("unsupported fresh solve") {
        config["pose_action"] = "solve";
    }
    SECTION("implicit inspection choice") {
        config["inspection"].erase("action");
    }
    SECTION("implicit checksum") {
        config["inspection"].erase("completion_sha256");
    }
    SECTION("unknown nested option") {
        config["inspection"]["remote"] = true;
    }
    SECTION("invalid digest") {
        config["inspection"]["completion_sha256"] = "not a digest";
    }
    SECTION("zero storage requirement") {
        config["inspection"]["minimum_free_bytes"] = 0;
    }
    SECTION("negative storage requirement") {
        config["inspection"]["minimum_free_bytes"] = -1;
    }
    SECTION("fractional storage requirement") {
        config["inspection"]["minimum_free_bytes"] = 1.5;
    }
    SECTION("oversized storage requirement") {
        config["inspection"]["minimum_free_bytes"] = UINT64_MAX;
    }
    SECTION("float schema version") {
        config["schema_version"] = 2.;
    }
    SECTION("legacy schema cannot claim preflight") {
        config["schema_version"] = 1;
    }
    save_json(K_REQUEST, config);
    CHECK_FALSE(read_reconstruction_job(K_REQUEST));
}

TEST_CASE("retained video preflight checks metadata identity bounds and read-only admission",
          "[preflight]") {
    JobFixture fixture;
    fixture.adopt_inspection();
    VideoPreflightRequest request {
        fixture.job.m_video, fixture.inspection(), { fixture.root / "new-volume/future-map" } };
    const auto K_INITIAL = preflight_video(request, {});
    REQUIRE(K_INITIAL);
    CHECK(K_INITIAL->m_source_sha256 == fixture.source_hash);
    CHECK(K_INITIAL->m_source_bytes == std::filesystem::file_size(fixture.job.m_video));
    CHECK(K_INITIAL->m_rotation_degrees == -90);
    CHECK(K_INITIAL->m_time_base_numerator == 1);
    CHECK(K_INITIAL->m_time_base_denominator == 30);
    CHECK_FALSE(std::filesystem::exists(request.m_outputs[0].parent_path()));
    const auto K_METADATA_PATH = request.m_inspection.m_stage / "result.json";
    auto metadata = nlohmann::json::parse(std::ifstream(K_METADATA_PATH));
    const auto K_ORIGINAL_METADATA = metadata.dump();
    SECTION("video differs") {
        std::ofstream(fixture.job.m_video) << "another capture";
    }
    SECTION("checksum differs") {
        request.m_inspection.m_completion_sha256 = std::string(64, 'b');
    }
    SECTION("missing inventory file") {
        std::filesystem::remove(K_METADATA_PATH);
    }
    SECTION("uncommitted file") {
        std::ofstream(request.m_inspection.m_stage / "extra") << "uncommitted";
    }
    SECTION("inspection byte budget is checked before hashing oversized entries") {
        const auto K_PATH = request.m_inspection.m_stage / "completion.json";
        auto completion = nlohmann::json::parse(std::ifstream(K_PATH));
        completion["files"][0]["bytes"] = 17ULL * 1024 * 1024;
        save_json(K_PATH, completion);
        request.m_inspection.m_completion_sha256 = sha256_file(K_PATH, 65536).value();
    }
    SECTION("inspection file count is bounded independently of pose stages") {
        for (int index { 0 }; index < 16; ++index) {
            std::ofstream(request.m_inspection.m_stage / ("extra-" + std::to_string(index)))
                << "diagnostic";
        }
        fixture.poses.inventory(request.m_inspection.m_stage);
        request.m_inspection.m_completion_sha256 =
            sha256_file(request.m_inspection.m_stage / "completion.json", 65536).value();
    }
    SECTION("symlink output escape") {
        request.m_outputs[0] = request.m_inspection.m_stage / "output";
    }
    SECTION("source output overlap") {
        request.m_outputs[0] = fixture.root;
    }
    SECTION("output parent must be a directory") {
        std::ofstream(fixture.root / "occupied-file") << "preserve";
        request.m_outputs[0] = fixture.root / "occupied-file/future-map";
    }
    SECTION("zero storage budget") {
        request.m_inspection.m_minimum_free_bytes = 0;
    }
#if defined(__linux__)
    SECTION("insufficient or unknown storage blocks retained work") {
        const auto K_AVAILABLE = std::filesystem::space("/proc").available;
        REQUIRE((K_AVAILABLE == 0 || K_AVAILABLE == UINTMAX_MAX));
        request.m_outputs[0] = "/proc/we-preflight-not-created";
    }
#endif
    SECTION("unsigned rotation cannot wrap") {
        metadata["rotation_degrees"] = UINT64_MAX - 89;
    }
    SECTION("fractional rotation") {
        metadata["rotation_degrees"] = 90.5;
    }
    SECTION("nonorthogonal rotation") {
        metadata["rotation_degrees"] = 45;
    }
    SECTION("unbounded pixel count") {
        metadata["stream"]["width"] = UINT64_MAX;
    }
    SECTION("empty dimensions") {
        metadata["stream"]["height"] = 0;
    }
    SECTION("excessive duration") {
        metadata["duration_seconds"] = 1201.;
    }
    SECTION("nonpositive duration") {
        metadata["duration_seconds"] = 0.;
    }
    SECTION("zero time denominator") {
        metadata["stream"]["time_base"] = "1/0";
    }
    SECTION("extra time delimiter") {
        metadata["stream"]["time_base"] = "1/30/2";
    }
    SECTION("time integer overflow") {
        metadata["stream"]["time_base"] = "1/9223372036854775808";
    }
    SECTION("HDR requires explicit conversion") {
        metadata["stream"]["color_transfer"] = "smpte2084";
    }
    SECTION("wrong source hash") {
        metadata["source"]["sha256"] = std::string(64, 'a');
    }
    SECTION("wrong source size") {
        metadata["source"]["bytes"] = 1;
    }
    SECTION("invalid stream type") {
        metadata["stream"]["codec_type"] = "audio";
    }
    SECTION("fractional metadata version") {
        metadata["schema_version"] = 1.;
    }
    SECTION("signed dimensions") {
        metadata["stream"]["width"] = -2;
    }
    SECTION("cancelled admission") {
        std::stop_source cancellation;
        cancellation.request_stop();
        auto result = preflight_video(request, cancellation.get_token());
        REQUIRE_FALSE(result);
        CHECK(result.error().m_code == ErrorCode::CANCELLED);
        return;
    }
    // Changed metadata is explicitly selected anew so semantic checks, rather
    // than only the inventory checksum, reject it. Byte fixtures are synthetic.
    if (std::filesystem::exists(K_METADATA_PATH) && metadata.dump() != K_ORIGINAL_METADATA) {
        save_json(K_METADATA_PATH, metadata);
        fixture.poses.inventory(request.m_inspection.m_stage);
        request.m_inspection.m_completion_sha256 =
            sha256_file(request.m_inspection.m_stage / "completion.json", 65536).value();
    }
    INFO(metadata.dump());
    CHECK_FALSE(preflight_video(request, {}));
    CHECK_FALSE(std::filesystem::exists(fixture.root / "new-volume"));
}

TEST_CASE("coordinator preflight failure preserves output and inspection dependencies on restart",
          "[preflight]") {
    JobFixture fixture;
    fixture.adopt_inspection();
    fixture.job.m_map_directory = fixture.root / "prepared-map";
    const auto K_INSPECTION_HASH = fixture.inspection().m_completion_sha256;
    REQUIRE(reconstruct(fixture.job, {}, {}));
    CHECK(load_bundle(fixture.job.m_scene_output)
              .value()
              ->m_provenance.at("inspection_completion_sha256") == K_INSPECTION_HASH);
    const auto K_MANIFEST = fixture.job.m_scene_output / "manifest.json";
    const auto K_COMMITTED = sha256_file(K_MANIFEST, 65536).value();
    const auto K_MAP_HASH =
        sha256_file(fixture.job.m_map_directory / "manifest.json", 65536).value();
    REQUIRE(reconstruct(fixture.job, {}, {}));
    CHECK(sha256_file(K_MANIFEST, 65536).value() == K_COMMITTED);
    CHECK(sha256_file(fixture.job.m_map_directory / "manifest.json", 65536).value() == K_MAP_HASH);
    SECTION("bad initial inspection creates no new map or fit") {
        fixture.inspection().m_completion_sha256 = std::string(64, 'b');
        fixture.job.m_map_directory = fixture.root / "must-not-create-map";
        fixture.job.m_fit_directory = fixture.root / "must-not-create-fit";
        fixture.job.m_action = FitAction::START;
        fixture.job.m_completed_fit_identity.clear();
        fixture.job.m_gsplat_source = fixture.root / "absent-source";
        fixture.job.m_fitting_worker = { fixture.root / "must-not-launch.py", WE_TEST_PYTHON, false };
        REQUIRE_FALSE(reconstruct(fixture.job, {}, {}));
        CHECK_FALSE(std::filesystem::exists(fixture.job.m_map_directory));
        CHECK_FALSE(std::filesystem::exists(fixture.job.m_fit_directory));
    }
    SECTION("inspection changes during fitting boundary") {
        const auto K_RESULT = reconstruct(fixture.job, {}, [&](const std::string& text) {
            if (text.find("Durable pose map ready") != std::string::npos) {
                std::ofstream(fixture.inspection().m_stage / "result.json") << "changed metadata";
            }
        });
        REQUIRE_FALSE(K_RESULT);
    }
    CHECK(sha256_file(K_MANIFEST, 65536).value() == K_COMMITTED);
}

TEST_CASE("automatic inspection request resolves local worker paths and rejects protected overlaps",
          "[inspection]") {
    JobFixture fixture;
    test::inspection_fixture inspector { fixture.root / "inspection-inputs", fixture.job.m_video };
    const auto K_REQUEST = fixture.root / "request.json";
    auto config = nlohmann::json { { "schema_version", 2 },
                                  { "recipe", "local_gsplat_v1" },
                                  { "action", "reuse_completed" },
                                  { "pose_action", "adopt_completed" },
                                  { "video", "capture.mp4" },
                                  { "pose_job", "poses" },
                                  { "fit_directory", "fit" },
                                  { "scene_output", "scene.wexplore" },
                                  { "steps", 7000 },
                                  { "max_image_dimension", 1280 },
                                  { "max_splats", 2000000 },
                                  { "completed_fit_identity", fixture.identity },
                                  { "inspection",
                                   { { "action", "inspect" },
                                    { "job_directory", "inspection-inputs/metadata $ ; job" },
                                    { "worker_script", "inspection-inputs/worker with spaces.py" },
                                    { "python", WE_TEST_PYTHON },
                                    { "minimum_free_bytes", 1 } } } };
    save_json(K_REQUEST, config);
    const auto K_PARSED = read_reconstruction_job(K_REQUEST);
    REQUIRE(K_PARSED);
    REQUIRE(K_PARSED->m_automatic_inspection);
    CHECK_FALSE(K_PARSED->m_inspection);
    CHECK(K_PARSED->m_request_file == std::filesystem::canonical(K_REQUEST));
    if (K_PARSED->m_automatic_inspection) {
        CHECK(std::filesystem::equivalent(K_PARSED->m_automatic_inspection->m_worker.m_script,
                                          inspector.request.m_configuration.m_worker.m_script));
        CHECK(K_PARSED->m_automatic_inspection->m_job_directory ==
              std::filesystem::weakly_canonical(inspector.request.m_configuration.m_job_directory));
    }
    SECTION("missing local worker") {
        config["inspection"].erase("worker_script");
    }
    SECTION("missing executable choice") {
        config["inspection"].erase("python");
    }
    SECTION("unknown inspection option") {
        config["inspection"]["remote"] = true;
    }
    SECTION("implicit reuse is rejected") {
        config["inspection"]["completion_sha256"] = std::string(64, 'a');
    }
    SECTION("pose job protected") {
        config["inspection"]["job_directory"] = "poses/metadata";
    }
    SECTION("map protected") {
        config["inspection"]["job_directory"] = "fit.map/metadata";
    }
    SECTION("fit protected") {
        config["inspection"]["job_directory"] = "fit/metadata";
    }
    SECTION("scene protected") {
        config["inspection"]["job_directory"] = "scene.wexplore/metadata";
    }
    SECTION("worker protected") {
        config["inspection"]["job_directory"] = "inspection-inputs";
    }
    SECTION("request protected from scene output") {
        config["scene_output"] = "request.json";
    }
    SECTION("request protected from fit output") {
        config["fit_directory"] = "request.json";
    }
    SECTION("request protected from metadata output") {
        config["inspection"]["job_directory"] = ".";
    }
    SECTION("invalid resource threshold") {
        config["inspection"]["minimum_free_bytes"] = 0;
    }
    save_json(K_REQUEST, config);
    CHECK_FALSE(read_reconstruction_job(K_REQUEST));
    CHECK_FALSE(std::filesystem::exists(inspector.request.m_configuration.m_job_directory));
}

TEST_CASE(
    "shared coordinator inspects before retained map preparation and preserves scene on failure",
    "[inspection]") {
    JobFixture fixture;
    REQUIRE(reconstruct(fixture.job, {}, {}));
    const auto K_MANIFEST = fixture.job.m_scene_output / "manifest.json";
    const auto K_ORIGINAL = sha256_file(K_MANIFEST, 65536).value();
    const auto K_MODE = GENERATE("valid", "failure", "escape", "no_video", "cancel_once");
    test::inspection_fixture inspector {
        fixture.root / "inspection-inputs", fixture.job.m_video, K_MODE };
    fixture.job.m_automatic_inspection = inspector.request.m_configuration;
    fixture.job.m_map_directory = fixture.root / "automatic-map";
    std::stop_source cancellation;
    std::string log;
    const auto K_RESULT =
        reconstruct(fixture.job, cancellation.get_token(), [&](const std::string& text) {
            log += text;
            if (std::string_view(K_MODE) == "cancel_once" &&
                text.find("Worker accepted START") != std::string::npos) {
                cancellation.request_stop();
            }
        });
    INFO(log);
    if (std::string_view(K_MODE) == "valid") {
        REQUIRE(K_RESULT);
        const auto K_COMMITTED = sha256_file(K_MANIFEST, 65536).value();
        const auto K_SCENE = load_bundle(fixture.job.m_scene_output).value();
        const auto K_STAGE = inspector.request.m_configuration.m_job_directory / "stages" /
                             ("inspect-" + std::string(64, 'c'));
        CHECK(K_SCENE->m_provenance.at("inspection_completion_sha256") ==
              sha256_file(K_STAGE / "completion.json", 65536).value());
        REQUIRE(reconstruct(fixture.job, {}, {}));
        CHECK(sha256_file(K_MANIFEST, 65536).value() == K_COMMITTED);
    } else {
        REQUIRE_FALSE(K_RESULT);
        CHECK(sha256_file(K_MANIFEST, 65536).value() == K_ORIGINAL);
        CHECK_FALSE(std::filesystem::exists(fixture.job.m_map_directory));
        if (std::string_view(K_MODE) == "cancel_once") {
            CHECK(K_RESULT.error().m_code == ErrorCode::CANCELLED);
            REQUIRE(reconstruct(fixture.job, {}, {}));
        }
    }
}

TEST_CASE("fresh coordinator chains decode RGB admission solve and publication with restart",
          "[stage-chain]") {
    JobFixture fixture;
    const auto K_AUTOMATIC = GENERATE(false, true);
    fixture.fresh("valid", K_AUTOMATIC);
    std::string log;
    const auto K_COMPLETED = reconstruct(fixture.job, {}, [&](const std::string& text) {
        log += text;
    });
    INFO(log);
    INFO((K_COMPLETED ? "complete" : K_COMPLETED.error().m_message));
    REQUIRE(K_COMPLETED);
    const auto K_MANIFEST = fixture.job.m_scene_output / "manifest.json";
    const auto K_PUBLISHED = sha256_file(K_MANIFEST, 65536).value();
    const auto K_COUNTER = sha256_file(fixture.job.m_pose_job / "capabilities.json", 65536).value();
    log.clear();
    REQUIRE(reconstruct(fixture.job, {}, [&](const std::string& text) {
        log += text;
    }));
    CHECK(log.find("solver skipped") != std::string::npos);
    CHECK(sha256_file(fixture.job.m_pose_job / "capabilities.json", 65536).value() == K_COUNTER);
    CHECK(sha256_file(K_MANIFEST, 65536).value() == K_PUBLISHED);
    SECTION("corrupt RGB receipt") {
        std::ofstream(fixture.job.m_pose_job / "input/1.ppm") << "changed";
    }
    SECTION("different decode dependency") {
        fixture.poses.inventory(fixture.poses.decoded);
        if (fixture.job.m_selected_decode) {
            fixture.job.m_selected_decode->m_completion_sha256 = std::string(64, 'd');
        } else if (fixture.job.m_automatic_decode) {
            fixture.job.m_automatic_decode->m_sample_fps = 2.;
        }
    }
    SECTION("recipe change") {
        REQUIRE(fixture.job.m_fresh_poses);
        if (fixture.job.m_fresh_poses) {
            fixture.job.m_fresh_poses->m_recipe = PoseRecipe::EXHAUSTIVE_GUIDED;
        }
    }
    SECTION("pose worker change") {
        std::ofstream(fixture.root / "pose worker.py", std::ios::app) << "\n# changed\n";
    }
    CHECK_FALSE(reconstruct(fixture.job, {}, {}));
    CHECK(sha256_file(K_MANIFEST, 65536).value() == K_PUBLISHED);
    CHECK(sha256_file(fixture.job.m_pose_job / "capabilities.json", 65536).value() == K_COUNTER);
}

TEST_CASE("fresh stage cancellation resumes checked journals and preserves existing scene",
          "[stage-chain]") {
    JobFixture fixture;
    REQUIRE(reconstruct(fixture.job, {}, {}));
    const auto K_MANIFEST = fixture.job.m_scene_output / "manifest.json";
    const auto K_ORIGINAL = sha256_file(K_MANIFEST, 65536).value();
    fixture.fresh("cancel_once");
    fixture.job.m_map_directory = fixture.root / "fresh-map";
    std::stop_source cancellation;
    std::string log;
    const auto K_FAILED =
        reconstruct(fixture.job, cancellation.get_token(), [&](const std::string& text) {
            log += text;
            if (text.find("Worker accepted START") != std::string::npos) {
                cancellation.request_stop();
            }
        });
    INFO(log);
    REQUIRE_FALSE(K_FAILED);
    CHECK(K_FAILED.error().m_code == ErrorCode::CANCELLED);
    CHECK(sha256_file(K_MANIFEST, 65536).value() == K_ORIGINAL);
    CHECK_FALSE(std::filesystem::exists(fixture.job.m_map_directory));
    const auto K_RECEIPT = sha256_file(fixture.job.m_pose_job / "input/1.ppm", 4096).value();
    const auto K_JOURNAL =
        sha256_file(fixture.job.m_pose_job / "input/journal.json", 65536).value();
    const auto K_COMPLETED = reconstruct(fixture.job, {}, {});
    INFO((K_COMPLETED ? "restarted" : K_COMPLETED.error().m_message));
    REQUIRE(K_COMPLETED);
    CHECK(sha256_file(fixture.job.m_pose_job / "input/1.ppm", 4096).value() == K_RECEIPT);
    CHECK(sha256_file(fixture.job.m_pose_job / "input/journal.json", 65536).value() == K_JOURNAL);
}

TEST_CASE("fresh stage failure and unowned poses cannot publish or invoke fitting",
          "[stage-chain]") {
    JobFixture fixture;
    REQUIRE(reconstruct(fixture.job, {}, {}));
    const auto K_MANIFEST = fixture.job.m_scene_output / "manifest.json";
    const auto K_ORIGINAL = sha256_file(K_MANIFEST, 65536).value();
    fixture.fresh("failure", true);
    fixture.job.m_map_directory = fixture.root / "fresh-map";
    SECTION("pose backend fails") { }
    SECTION("decode corrupt") {
        test::stage_worker(fixture.root / "decode worker.py", fixture.poses, "corrupt");
    }
    SECTION("unowned existing poses") {
        std::filesystem::create_directory(fixture.job.m_pose_job);
        std::ofstream(fixture.job.m_pose_job / "foreign") << "preserve";
    }
    SECTION("overlapping request") {
        fixture.job.m_request_file = fixture.job.m_pose_job / "request.json";
    }
    REQUIRE_FALSE(reconstruct(fixture.job, {}, {}));
    CHECK_FALSE(std::filesystem::exists(fixture.job.m_map_directory));
    CHECK(sha256_file(K_MANIFEST, 65536).value() == K_ORIGINAL);
}

TEST_CASE("fresh reconstruction requests require explicit stages resources and disjoint jobs",
          "[stage-chain]") {
    JobFixture fixture;
    fixture.fresh("valid", true);
    const auto K_REQUEST = fixture.root / "request.json";
    auto config = nlohmann::json { { "schema_version", 2 },
                                  { "recipe", "local_gsplat_v1" },
                                  { "action", "reuse_completed" },
                                  { "pose_action", "solve" },
                                  { "video", "capture.mp4" },
                                  { "pose_job", "fresh poses" },
                                  { "fit_directory", "fit" },
                                  { "scene_output", "scene.wexplore" },
                                  { "steps", 7000 },
                                  { "max_image_dimension", 1280 },
                                  { "max_splats", 2000000 },
                                  { "completed_fit_identity", fixture.identity },
                                  { "inspection",
                                   { { "action", "adopt_completed" },
                                    { "stage", test::path_utf8(fixture.inspection().m_stage) },
                                    { "completion_sha256", fixture.inspection().m_completion_sha256 },
                                    { "minimum_free_bytes", 1 } } },
                                  { "pose_worker",
                                   { { "worker_script", "pose worker.py" },
                                    { "worker_python", WE_TEST_PYTHON },
                                    { "pose_python", WE_TEST_PYTHON },
                                    { "recipe", "sequential" },
                                    { "cpu_threads", 4 },
                                    { "journal_bytes", 2147483648ULL } } },
                                  { "decode",
                                   { { "action", "decode" },
                                    { "job_directory", "decoded video" },
                                    { "worker_script", "decode worker.py" },
                                    { "python", WE_TEST_PYTHON },
                                    { "sample_fps", 3. },
                                    { "max_frames", 120 },
                                    { "cpu_threads", 4 },
                                    { "max_output_bytes", 34359738368ULL } } } };
    save_json(K_REQUEST, config);
    const auto K_PARSED = read_reconstruction_job(K_REQUEST);
    INFO((K_PARSED ? "parsed" : K_PARSED.error().m_message));
    REQUIRE(K_PARSED);
    REQUIRE(K_PARSED->m_fresh_poses);
    REQUIRE(K_PARSED->m_automatic_decode);
    if (K_PARSED->m_automatic_decode) {
        CHECK(K_PARSED->m_automatic_decode->m_frame_selection ==
              FrameSelection::TIMESTAMP_SAMPLING_V1);
    }
    REQUIRE(reconstruct(*K_PARSED, {}, {}));
    SECTION("explicit candidate selection") {
        config["decode"]["frame_selection"] = "quality_keyframes_candidate_v1";
        save_json(K_REQUEST, config);
        const auto K_CANDIDATE = read_reconstruction_job(K_REQUEST);
        REQUIRE(K_CANDIDATE);
        REQUIRE(K_CANDIDATE->m_automatic_decode);
        if (K_CANDIDATE->m_automatic_decode) {
            CHECK(K_CANDIDATE->m_automatic_decode->m_frame_selection ==
                  FrameSelection::QUALITY_KEYFRAMES_CANDIDATE_V1);
        }
        config["decode"]["frame_selection"] = "unknown";
    }
    SECTION("candidate requires at least two candidates per second") {
        config["decode"]["frame_selection"] = "quality_keyframes_candidate_v1";
        config["decode"]["sample_fps"] = 1.0;
    }
    SECTION("unknown decode option") {
        config["decode"]["remote"] = true;
    }
    SECTION("unknown pose option") {
        config["pose_worker"]["remote"] = true;
    }
    SECTION("missing executable") {
        config["pose_worker"].erase("pose_python");
    }
    SECTION("different pose action") {
        config["pose_action"] = "adopt_completed";
    }
    SECTION("invalid rate") {
        config["decode"]["sample_fps"] = 0.;
    }
    SECTION("non-numeric rate") {
        config["decode"]["sample_fps"] = "3";
    }
    SECTION("fractional frame limit") {
        config["decode"]["max_frames"] = 120.5;
    }
    SECTION("oversized frames") {
        config["decode"]["max_frames"] = 121;
    }
    SECTION("negative bytes") {
        config["decode"]["max_output_bytes"] = -1;
    }
    SECTION("insufficient journal") {
        config["pose_worker"]["journal_bytes"] = 1;
    }
    SECTION("unknown recipe") {
        config["pose_worker"]["recipe"] = "automatic_fallback";
    }
    SECTION("decode includes request") {
        config["decode"]["job_directory"] = ".";
    }
    SECTION("decode mutates retained input") {
        config["decode"]["job_directory"] = "poses";
    }
    SECTION("pose output includes decoder") {
        config["pose_job"] = "decoded video";
    }
    SECTION("map mutates pose intent") {
        config["map_directory"] = "fresh poses.plan/map";
    }
    SECTION("scene overlaps intent") {
        config["scene_output"] = "fresh poses.plan";
    }
    SECTION("legacy decode selection has different meaning") {
        config["decode_stage"] = "poses/stages/decode-" + fixture.poses.decode_key;
    }
    save_json(K_REQUEST, config);
    CHECK_FALSE(read_reconstruction_job(K_REQUEST));
}

TEST_CASE("pose preparation rejects unsafe destinations and cancelled admission",
          "[poses][persistence][application]") {
    JobFixture fixture;
    PosePreparationRequest request { { fixture.job.m_pose_job, fixture.job.m_video, {} },
                                    fixture.root / "prepared-map" };
    SECTION("input overlap") {
        request.m_map_directory = fixture.job.m_pose_job / "map";
    }
    SECTION("occupied directory") {
        std::filesystem::create_directory(request.m_map_directory);
        std::ofstream(request.m_map_directory / "unowned.txt") << "preserve";
    }
    SECTION("empty destination") {
        request.m_map_directory.clear();
    }
    SECTION("cancel before initialization") {
        std::stop_source stop;
        stop.request_stop();
        const auto K_RESULT = prepare_completed_pose_map(request, stop.get_token());
        REQUIRE_FALSE(K_RESULT);
        CHECK(K_RESULT.error().m_code == ErrorCode::CANCELLED);
        CHECK_FALSE(std::filesystem::exists(request.m_map_directory));
        return;
    }
    CHECK_FALSE(prepare_completed_pose_map(request, {}));
    CHECK_FALSE(std::filesystem::exists(request.m_map_directory / "manifest.json"));
}

TEST_CASE("fit admission preserves nearest-even resizing and independent intrinsic scales") {
    JobFixture fixture;
    fixture.job.m_max_image_dimension = 128;
    test::write_training_camera(fixture.stage / "training/sparse/cameras.bin",
                                test::training_camera_bytes(1, 256, 241, { 400, 420, 128, 120.5 }));
    fixture.poses.inventory(fixture.stage);
    const auto K_PATH = fixture.job.m_fit_directory / "input_manifest.json";
    auto input = nlohmann::json::parse(std::ifstream(K_PATH));
    input["max_image_dimension"] = 128;
    for (auto& frame : input["dataset"]["frames"]) {
        frame["original_width"] = 256;
        frame["original_height"] = 241;
        frame["width"] = 128;
        frame["height"] = 120;
        frame["intrinsic"] = {
            { 200., 0, 64. }, { 0, static_cast<float>(420. * 120 / 241), 60. }, { 0, 0, 1 } };
    }
    save_json(K_PATH, input);
    const auto K_INITIAL = reconstruct(fixture.job, {}, {});
    INFO((K_INITIAL ? "accepted" : K_INITIAL.error().m_message));
    REQUIRE(K_INITIAL);
    const auto K_COMMITTED =
        sha256_file(fixture.job.m_scene_output / "manifest.json", 65536).value();
    // 241 * .5 = 120.5 rounds to even 120 in the recorded Python recipe.
    for (auto& frame : input["dataset"]["frames"]) {
        frame["height"] = 121;
    }
    save_json(K_PATH, input);
    CHECK_FALSE(reconstruct(fixture.job, {}, {}));
    CHECK(sha256_file(fixture.job.m_scene_output / "manifest.json", 65536).value() == K_COMMITTED);
}

TEST_CASE("invalid training calibration fails before durable map or scene publication") {
    JobFixture fixture;
    test::write_training_camera(fixture.stage / "training/sparse/cameras.bin",
                                test::training_camera_bytes(4));
    fixture.poses.inventory(fixture.stage);
    const auto K_REJECTED = reconstruct(fixture.job, {}, {});
    REQUIRE_FALSE(K_REJECTED);
    CHECK(K_REJECTED.error().m_code == ErrorCode::UNSUPPORTED_OPERATION);
    auto map = fixture.job.m_fit_directory;
    map += ".map";
    CHECK_FALSE(std::filesystem::exists(map));
    CHECK_FALSE(std::filesystem::exists(fixture.job.m_scene_output));
}

TEST_CASE("fresh chaining resumes its own canonical fit checkpoint without repeating poses",
          "[stage-chain]") {
    JobFixture fixture;
    fixture.fresh("valid", GENERATE(false, true));
    const auto K_TEMPLATE_FIT = fixture.job.m_fit_directory;
    fixture.job.m_fit_directory = fixture.root / "resumed fit";
    fixture.job.m_action = FitAction::RESUME;
    fixture.job.m_completed_fit_identity.clear();
    fixture.job.m_gsplat_source = fixture.root / "backend source";
    fixture.job.m_fitting_worker = { fixture.root / "fit worker.py", WE_TEST_PYTHON, false };
    const auto K_OWNED = GENERATE(false, true);
    fixture.job.m_checkpoint = K_OWNED ? fixture.job.m_fit_directory / "checkpoint.pt"
                                       : fixture.root / "external checkpoint.pt";
    std::filesystem::create_directories(fixture.job.m_checkpoint.parent_path());
    std::ofstream(fixture.job.m_checkpoint) << "synthetic full-checkpoint marker";
    const auto K_CHECKPOINT_HASH = sha256_file(fixture.job.m_checkpoint, 1024).value();
    std::ofstream script(fixture.job.m_fitting_worker.m_script);
    script << "import argparse,shutil\nfrom pathlib import Path\np=argparse.ArgumentParser()\n"
              "for key in "
              "('source','data','video','output','steps','max-image-dimension','max-splats'):\n"
              " p.add_argument('--'+key,required=True)\np.add_argument('--resume',required=True)\n"
              "args=p.parse_args()\n"
           << "assert Path(args.resume).samefile("
           << nlohmann::json(test::path_utf8(fixture.job.m_checkpoint)).dump() << ")\n"
           << "shutil.copytree(" << nlohmann::json(test::path_utf8(K_TEMPLATE_FIT)).dump()
           << ",args.output,dirs_exist_ok=True)\n"
              "print('synthetic checkpoint forwarding; no optimizer')\n";
    script.close();
    std::string log;
    const auto K_RESUMED = reconstruct(fixture.job, {}, [&](const std::string& text) {
        log += text;
    });
    INFO(log);
    INFO((K_RESUMED ? "resumed" : K_RESUMED.error().m_message));
    REQUIRE(K_RESUMED);
    CHECK(sha256_file(fixture.job.m_checkpoint, 1024).value() == K_CHECKPOINT_HASH);
    const auto K_POSE_INVOCATIONS =
        sha256_file(fixture.job.m_pose_job / "capabilities.json", 65536).value();
    const auto K_SCENE_HASH =
        sha256_file(fixture.job.m_scene_output / "manifest.json", 65536).value();
    fixture.job.m_action = FitAction::REUSE_COMPLETED;
    fixture.job.m_checkpoint.clear();
    fixture.job.m_completed_fit_identity = fixture.identity;
    log.clear();
    REQUIRE(reconstruct(fixture.job, {}, [&](const std::string& text) {
        log += text;
    }));
    CHECK(log.find("solver skipped") != std::string::npos);
    CHECK(log.find("optimization skipped") != std::string::npos);
    CHECK(sha256_file(fixture.job.m_pose_job / "capabilities.json", 65536).value() ==
          K_POSE_INVOCATIONS);
    CHECK(sha256_file(fixture.job.m_scene_output / "manifest.json", 65536).value() == K_SCENE_HASH);
}

TEST_CASE("fresh checkpoint exception preserves other protected inputs and destinations",
          "[stage-chain]") {
    JobFixture fixture;
    REQUIRE(reconstruct(fixture.job, {}, {}));
    const auto K_PUBLISHED =
        sha256_file(fixture.job.m_scene_output / "manifest.json", 65536).value();
    fixture.fresh("valid", true);
    fixture.job.m_fit_directory = fixture.root / "fresh fit";
    fixture.job.m_action = FitAction::RESUME;
    fixture.job.m_completed_fit_identity.clear();
    fixture.job.m_gsplat_source = fixture.root / "backend source";
    fixture.job.m_fitting_worker = { fixture.root / "fit worker.py", WE_TEST_PYTHON, false };
    fixture.job.m_checkpoint = fixture.job.m_fit_directory / "checkpoint.pt";
    SECTION("fit metadata stays protected") {
        fixture.job.m_checkpoint = fixture.job.m_fit_directory / "input_manifest.json";
    }
    SECTION("pose output stays protected") {
        fixture.job.m_checkpoint = fixture.job.m_pose_job / "checkpoint.pt";
    }
    SECTION("pose intent stays protected") {
        auto plan = fixture.job.m_pose_job;
        plan += ".plan";
        fixture.job.m_checkpoint = plan / "checkpoint.pt";
    }
    SECTION("map stays protected") {
        fixture.job.m_map_directory = fixture.root / "map";
        fixture.job.m_checkpoint = fixture.job.m_map_directory / "checkpoint.pt";
    }
    SECTION("scene stays protected") {
        fixture.job.m_checkpoint = fixture.job.m_scene_output / "manifest.json";
    }
    SECTION("decoder stays protected") {
        REQUIRE(fixture.job.m_automatic_decode);
        if (fixture.job.m_automatic_decode) {
            fixture.job.m_checkpoint =
                fixture.job.m_automatic_decode->m_job_directory / "checkpoint.pt";
        }
    }
    SECTION("request alias stays protected") {
        fixture.job.m_request_file = fixture.job.m_checkpoint;
    }
    SECTION("worker alias stays protected") {
        fixture.job.m_fitting_worker.m_script = fixture.job.m_checkpoint;
    }
    SECTION("backend alias stays protected") {
        fixture.job.m_gsplat_source = fixture.job.m_checkpoint;
    }
    SECTION("executable alias stays protected") {
        fixture.job.m_fitting_worker.m_python = test::path_utf8(fixture.job.m_checkpoint);
    }
    SECTION("start may not replace a checkpoint") {
        fixture.job.m_action = FitAction::START;
    }
    if (!std::filesystem::exists(fixture.job.m_checkpoint)) {
        std::filesystem::create_directories(fixture.job.m_checkpoint.parent_path());
        std::ofstream(fixture.job.m_checkpoint) << "synthetic protected input";
    }
    const auto K_CHECKPOINT_HASH = sha256_file(fixture.job.m_checkpoint, 65536).value();
    std::string log;
    const auto K_REJECTED = reconstruct(fixture.job, {}, [&](const std::string& text) {
        log += text;
    });
    REQUIRE_FALSE(K_REJECTED);
    CHECK(K_REJECTED.error().m_code == ErrorCode::INVALID_ARGUMENT);
    CHECK(log.find("Worker accepted START") == std::string::npos);
    CHECK(sha256_file(fixture.job.m_checkpoint, 65536).value() == K_CHECKPOINT_HASH);
    CHECK(sha256_file(fixture.job.m_scene_output / "manifest.json", 65536).value() == K_PUBLISHED);
}

TEST_CASE("reconstruction telemetry bounds backend metadata and keeps ETA unknown", "[progress]") {
    const auto K_CHECKED = [](const std::string& message,
                              double elapsed = 12.5,
                              const std::string& stage = "appearance") {
        return checked_reconstruction_progress(
            WorkerProgress { stage, message, elapsed }, ReconstructionStage::FITTING, 7000);
    };
    const auto K_VALID = K_CHECKED(R"({"step":1101,"loss":0.4})");
    REQUIRE(K_VALID);
    if (!K_VALID) {
        return;
    }
    CHECK(K_VALID.value().m_fit_step == 1101);
    CHECK(K_VALID.value().m_fit_steps == 7000);
    CHECK(K_VALID.value().m_backend_elapsed_seconds == 12.5);
    for (const auto* message : { "not JSON",
                                R"({"step":-1})",
                                R"({"step":7001})",
                                R"({"step":1.5})",
                                R"({"step":true})",
                                R"({"step":18446744073709551615})",
                                "[]",
                                R"({"step":1,"nested":[[[[[[[[[[0]]]]]]]]]]})" }) {
        INFO(message);
        const auto K_VALUE = K_CHECKED(message);
        REQUIRE(K_VALUE);
        if (!K_VALUE) {
            return;
        }
        CHECK_FALSE(K_VALUE.value().m_fit_step);
        CHECK(K_VALUE.value().m_fit_steps == 0);
        CHECK(K_VALUE.value().m_backend_elapsed_seconds == 12.5);
    }
    const auto K_OVERSIZED = K_CHECKED(std::string(65537, 'x'));
    REQUIRE(K_OVERSIZED);
    if (K_OVERSIZED) {
        CHECK_FALSE(K_OVERSIZED->m_fit_step);
    }
    CHECK_FALSE(K_CHECKED("{}", -1));
    CHECK_FALSE(K_CHECKED("{}", std::numeric_limits<double>::infinity()));
    CHECK_FALSE(K_CHECKED("{}", std::numeric_limits<double>::quiet_NaN()));
    CHECK_FALSE(K_CHECKED("{}", 0, "arbitrary backend stage"));
    CHECK_FALSE(checked_reconstruction_progress(
        WorkerCompleted { "appearance", {} }, ReconstructionStage::FITTING, 7000));
    const auto K_ZERO = K_CHECKED(R"({"step":0})", 0);
    REQUIRE(K_ZERO);
    if (!K_ZERO) {
        return;
    }
    CHECK(K_ZERO.value().m_fit_step == 0);
    CHECK(K_ZERO.value().m_backend_elapsed_seconds == 0);
}

TEST_CASE("coordinator reports retained stages and stops progress at failed or cancelled stage",
          "[progress]") {
    JobFixture fixture;
    fixture.adopt_inspection();
    std::vector<ReconstructionStage> stages;
    std::stop_source cancellation;
    bool cancel = false;
    SECTION("checked completed reuse has no invented fitting progress") { }
    SECTION("cancel during map preparation") {
        cancel = true;
    }
    SECTION("bad fit retains failing verification stage") {
        fixture.job.m_completed_fit_identity = std::string(64, 'c');
    }
    const auto K_RESULT = reconstruct(
        fixture.job, cancellation.get_token(), {}, [&](const ReconstructionProgress& value) {
            stages.push_back(value.m_stage);
            CHECK_FALSE(value.m_fit_step);
            CHECK_FALSE(value.m_backend_elapsed_seconds);
            if (cancel && value.m_stage == ReconstructionStage::MAP) {
                cancellation.request_stop();
            }
        });
    const std::vector<ReconstructionStage> K_PREFIX { ReconstructionStage::VALIDATING,
                                                     ReconstructionStage::INSPECTING,
                                                     ReconstructionStage::POSES,
                                                     ReconstructionStage::MAP };
    REQUIRE(stages.size() >= K_PREFIX.size());
    CHECK(std::equal(K_PREFIX.begin(), K_PREFIX.end(), stages.begin()));
    if (cancel) {
        REQUIRE_FALSE(K_RESULT);
        CHECK(K_RESULT.error().m_code == ErrorCode::CANCELLED);
        CHECK(stages.back() == ReconstructionStage::MAP);
    } else if (fixture.job.m_completed_fit_identity != fixture.identity) {
        REQUIRE_FALSE(K_RESULT);
        CHECK(stages.back() == ReconstructionStage::VERIFYING_FIT);
    } else {
        REQUIRE(K_RESULT);
        CHECK(stages == std::vector<ReconstructionStage> { ReconstructionStage::VALIDATING,
                                                          ReconstructionStage::INSPECTING,
                                                          ReconstructionStage::POSES,
                                                          ReconstructionStage::MAP,
                                                          ReconstructionStage::VERIFYING_FIT,
                                                          ReconstructionStage::COMPILING });
    }
}

TEST_CASE("coordinator forwards verified fit steps without parsing diagnostic process output",
          "[progress]") {
    JobFixture fixture;
    const auto K_RETAINED = fixture.job.m_fit_directory;
    fixture.job.m_fit_directory = fixture.root / "new fit";
    fixture.job.m_action = FitAction::START;
    fixture.job.m_completed_fit_identity.clear();
    fixture.job.m_gsplat_source = fixture.root / "local source";
    fixture.job.m_fitting_worker = { fixture.root / "progress worker.py", WE_TEST_PYTHON };
    // Copies explicitly synthetic contract assets; never trains or qualifies a real gate.
    std::ofstream script(fixture.job.m_fitting_worker.m_script);
    script << "import sys,json,shutil\nfrom pathlib import Path\nsys.path.insert(0,"
           << nlohmann::json(WE_TEST_PROTOCOL_MODULE).dump() << ")\nimport we_protocol as p\n"
           << "p.configure(sys.argv,backend='synthetic_fit',backend_version='1',\n"
           << "sensors=['video'],devices=['cpu'],checkpoint_semantics='synthetic',\n"
           << "checkpoint_rights='diagnostic',limits={'max_frames':120,'max_source_bytes':16*1024**"
              "3,'max_output_bytes':32*1024**3})\n"
           << "p.progress('appearance',json.dumps({'step':1101}),42.655)\n"
           << "out=Path(sys.argv[sys.argv.index('--output')+1])\nshutil.copytree("
           << nlohmann::json(K_RETAINED.string()).dump() << ",out)\n"
           << "p.completed('appearance',out/'result.json')\n";
    script.close();
    std::vector<ReconstructionProgress> events;
    const auto K_RESULT =
        reconstruct(fixture.job, {}, {}, [&](const ReconstructionProgress& value) {
            events.push_back(value);
        });
    INFO((K_RESULT ? "published" : K_RESULT.error().m_message));
    REQUIRE(K_RESULT);
    const auto K_FIT = std::ranges::find_if(events, [](const auto& value) {
        return value.m_fit_step.has_value();
    });
    REQUIRE(K_FIT != events.end());
    CHECK(K_FIT->m_stage == ReconstructionStage::FITTING);
    CHECK(K_FIT->m_fit_step == 1101);
    CHECK(K_FIT->m_fit_steps == 7000);
    CHECK(K_FIT->m_backend_elapsed_seconds == 42.655);
    CHECK(events.back().m_stage == ReconstructionStage::COMPILING);
    CHECK_FALSE(events.back().m_fit_step);
}

TEST_CASE("fresh coordinator forwards checked inspection decode and pose progress", "[progress]") {
    JobFixture fixture;
    fixture.fresh("valid", true);
    test::inspection_fixture inspector { fixture.root / "inspection-inputs", fixture.job.m_video };
    fixture.job.m_inspection.reset();
    fixture.job.m_automatic_inspection = inspector.request.m_configuration;
    std::vector<ReconstructionProgress> events;
    const auto K_RESULT =
        reconstruct(fixture.job, {}, {}, [&](const ReconstructionProgress& value) {
            events.push_back(value);
        });
    INFO((K_RESULT ? "published synthetic fixture" : K_RESULT.error().m_message));
    REQUIRE(K_RESULT);
    std::vector<ReconstructionStage> backend_stages;
    for (const auto& value : events) {
        if (!value.m_backend_elapsed_seconds) {
            continue;
        }
        backend_stages.push_back(value.m_stage);
        CHECK_FALSE(value.m_fit_step);
        CHECK(*value.m_backend_elapsed_seconds ==
              (value.m_stage == ReconstructionStage::INSPECTING ? .25 : 1.25));
    }
    CHECK(backend_stages == std::vector<ReconstructionStage> { ReconstructionStage::INSPECTING,
                                                              ReconstructionStage::DECODING,
                                                              ReconstructionStage::POSES });
}

TEST_CASE("reconstruction reader binds exact reviewed bytes before accepting a request",
          "[review]") {
    JobFixture fixture;
    fixture.adopt_inspection();
    const auto K_REQUEST = fixture.write_retained_request();
    const auto K_DIGEST = sha256_file(K_REQUEST, 65536).value();
    REQUIRE(read_reconstruction_job(K_REQUEST, K_DIGEST));
    SECTION("invalid digest") {
        const auto K_INVALID = read_reconstruction_job(K_REQUEST, "bad");
        REQUIRE_FALSE(K_INVALID);
        CHECK(K_INVALID.error().m_code == ErrorCode::INVALID_ARGUMENT);
    }
    SECTION("different checked bytes") {
        std::ofstream(K_REQUEST, std::ios::app) << '\n';  // Same JSON, changed reviewed content.
        const auto K_CHANGED = read_reconstruction_job(K_REQUEST, K_DIGEST);
        REQUIRE_FALSE(K_CHANGED);
        CHECK(K_CHANGED.error().m_code == ErrorCode::INVALID_DATA);
        CHECK(K_CHANGED.error().m_message.find("checksum") != std::string::npos);
        CHECK(read_reconstruction_job(K_REQUEST));  // Legacy unpinned calls remain supported.
    }
    CHECK_FALSE(std::filesystem::exists(fixture.job.m_scene_output));
}
