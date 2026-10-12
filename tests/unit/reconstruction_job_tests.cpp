#include "inspection_fixture.hpp"
#include "pose_fixture.hpp"
#include "stage_chain_fixture.hpp"
#include "world_explorer/application/pose_preparation.hpp"
#include "world_explorer/application/reconstruction_job.hpp"
#include "world_explorer/application/scenes.hpp"
#include "world_explorer/core/hash.hpp"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

using namespace world_explorer;
namespace {
void save_json(const std::filesystem::path& path, const nlohmann::json& value) {
    std::ofstream file(path);
    file << value.dump();
}
struct job_fixture {
    std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("we_job_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    test::pose_fixture poses{root / "poses"};
    reconstruction_job job;
    std::filesystem::path stage;
    std::string source_hash;
    std::string image_hash;
    std::string identity = std::string(64, 'b');
    job_fixture() {
        job.video = root / "capture.mp4";
        job.pose_job = root / "poses";
        job.fit_directory = root / "fit";
        job.scene_output = root / "scene.wexplore";
        job.action = fit_action::REUSE_COMPLETED;
        job.completed_fit_identity = identity;
        stage = poses.posed;
        std::filesystem::create_directories(stage / "training/images");
        std::filesystem::create_directories(stage / "training/sparse");
        std::filesystem::create_directories(job.fit_directory / "assets");
        // Diagnostic byte fixtures exercise admission, not capture reconstruction.
        std::filesystem::copy_file(poses.root / "source.mp4", job.video);
        source_hash = sha256_file(job.video, 1024).value();
        std::ofstream(stage / "training/images/frame.png") << "diagnostic image bytes";
        image_hash = sha256_file(stage / "training/images/frame.png", 1024).value();
        for (const auto* name : {"cameras.bin", "images.bin", "points3D.bin"})
            std::ofstream(stage / "training/sparse" / name) << "diagnostic model bytes";
        poses.inventory(stage);
        std::ofstream(job.fit_directory / "assets/fit.ply")
            << "ply\nformat ascii 1.0\ncomment exporter gsplat-1.5.3\ncomment color_domain srgb\n"
               "element vertex 1\nproperty float x\nproperty float y\nproperty float z\n"
               "property float f_dc_0\nproperty float f_dc_1\nproperty float f_dc_2\n"
               "property float opacity\nproperty float scale_0\nproperty float scale_1\n"
               "property float scale_2\nproperty float rot_0\nproperty float rot_1\n"
               "property float rot_2\nproperty float rot_3\nend_header\n"
               "0 0 2 0 0 0 0 -2 -2 -2 1 0 0 0\n";
        save_json(
            job.fit_directory / "result.json",
            {{"schema_version", 1},
             {"status", "complete"},
             {"recipe", "local_gsplat_v1"},
             {"identity", identity},
             {"source_sha256", source_hash},
             {"steps", 7000},
             {"splats", 1},
             {"ply", "assets/fit.ply"},
             {"ply_sha256", sha256_file(job.fit_directory / "assets/fit.ply", 65536).value()}});
        const auto dataset = poses.load().value();
        nlohmann::json frames = nlohmann::json::array();
        std::filesystem::remove(stage / "training/images/frame.png");
        for (const auto& [name, frame] : dataset->training_frames) {
            std::ofstream(stage / "training/images" / name) << "diagnostic image bytes";
            const auto pose = std::ranges::find(dataset->poses, frame, &pose_estimate::frame);
            const auto& transform = pose->submap_from_camera;
            const auto q = transform.rotation;
            const quatd inverse{q.w, -q.x, -q.y, -q.z};
            const auto x = rotate(inverse, {1, 0, 0});
            const auto y = rotate(inverse, {0, -1, 0});
            const auto z = rotate(inverse, {0, 0, -1});
            const auto t = rotate(inverse, transform.translation) * -1;
            frames.push_back(
                {{"name", name},
                 {"sha256", image_hash},
                 {"height", 128},
                 {"view",
                  {{x.x, y.x, z.x, t.x}, {x.y, y.y, z.y, t.y}, {x.z, y.z, z.z, t.z}, {0, 0, 0, 1}}},
                 {"intrinsic", {{200, 0, 64}, {0, 200, 64}, {0, 0, 1}}}});
        }
        poses.inventory(stage);
        save_json(job.fit_directory / "input_manifest.json",
                  {{"recipe", "local_gsplat_v1"},
                   {"source_sha256", source_hash},
                   {"steps", 7000},
                   {"max_image_dimension", 1280},
                   {"max_splats", 2000000},
                   {"dataset", {{"scene_scale", 2}, {"frames", frames}}}});
    }
    ~job_fixture() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
    void adopt_inspection() {
        const auto directory = poses.root / "stages" / ("inspect-" + poses.pose_key);
        std::filesystem::create_directory(directory);
        save_json(directory / "result.json",
                  {{"schema_version", 1},
                   {"source",
                    {{"sha256", source_hash}, {"bytes", std::filesystem::file_size(job.video)}}},
                   {"stream",
                    {{"codec_type", "video"},
                     {"codec_name", "synthetic_diagnostic"},
                     {"width", 2},
                     {"height", 1},
                     {"time_base", "1/30"},
                     {"color_transfer", "bt709"}}},
                   {"duration_seconds", 1.},
                   {"rotation_degrees", -90},
                   {"audio", "ignored"},
                   {"scale_status", "ARBITRARY"},
                   {"color_domain", "srgb"}});
        poses.inventory(directory);
        job.inspection = retained_video_inspection{
            directory, sha256_file(directory / "completion.json", 65536).value(), 1};
    }
    retained_video_inspection& inspection() {
        if (!job.inspection)
            throw std::logic_error("Fixture inspection has not been prepared");
        return *job.inspection;
    }
    void fresh(const std::string& mode = "valid", bool automatic = false) {
        adopt_inspection();
        auto metadata = nlohmann::json::parse(std::ifstream(inspection().stage / "result.json"));
        metadata["rotation_degrees"] = 0;
        save_json(inspection().stage / "result.json", metadata);
        poses.inventory(inspection().stage);
        inspection().completion_sha256 =
            sha256_file(inspection().stage / "completion.json", 65536).value();
        job.pose_job = root / "fresh poses";
        job.fresh_poses =
            fresh_pose_configuration{{root / "pose worker.py", WE_TEST_PYTHON}, WE_TEST_PYTHON};
        test::stage_worker(root / "pose worker.py", poses, mode);
        if (automatic) {
            test::stage_worker(root / "decode worker.py", poses);
            job.automatic_decode = automatic_video_decode{
                root / "decoded video", {root / "decode worker.py", WE_TEST_PYTHON}};
        } else
            job.selected_decode = retained_video_decode{
                poses.decoded, sha256_file(poses.decoded / "completion.json", 65536).value()};
    }
};
} // namespace

TEST_CASE(
    "coordinator reuses an explicitly selected fit and preserves publication on changed inputs") {
    job_fixture fixture;
    std::string log;
    const auto initial =
        reconstruct(fixture.job, {}, [&](const std::string& text) { log += text; });
    INFO((initial ? "published" : initial.error().message));
    REQUIRE(initial);
    REQUIRE(log.find("optimization skipped") != std::string::npos);
    const auto scene = load_bundle(fixture.job.scene_output).value();
    CHECK(scene->provenance.at("fit_identity") == fixture.identity);
    CHECK(scene->provenance.at("map_revision") == "1");
    CHECK(scene->provenance.at("map_epoch") == "0");
    CHECK(scene->provenance.at("pose_asset_sha256").size() == 64);
    CHECK(scene->chunks.begin()->second->source_frames.size() == 3);
    const auto manifest = fixture.job.scene_output / "manifest.json";
    const auto committed = sha256_file(manifest, 65536).value();
    SECTION("different completed identity") {
        fixture.job.completed_fit_identity = std::string(64, 'c');
    }
    SECTION("different recipe settings") { fixture.job.max_image_dimension = 640; }
    SECTION("fit from a different camera solve") {
        const auto path = fixture.job.fit_directory / "input_manifest.json";
        auto input = nlohmann::json::parse(std::ifstream(path));
        input["dataset"]["frames"][0]["view"][0][3] = 100.;
        save_json(path, input);
    }
    SECTION("incomplete camera inventory") {
        const auto path = fixture.job.fit_directory / "input_manifest.json";
        auto input = nlohmann::json::parse(std::ifstream(path));
        input["dataset"]["frames"].erase(0);
        save_json(path, input);
    }
    SECTION("changed source") { std::ofstream(fixture.job.video) << "different source"; }
    SECTION("changed image") {
        std::ofstream(fixture.stage / "training/images/0.png") << "corrupt";
    }
    SECTION("extra uncommitted file") { std::ofstream(fixture.stage / "extra") << "uncommitted"; }
    SECTION("escaping pose pointer") {
        save_json(fixture.job.pose_job / "poses.json",
                  {{"key", std::string(64, 'a')}, {"output", fixture.root.string()}});
    }
    SECTION("cancel before publication") {
        std::stop_source cancellation;
        cancellation.request_stop();
        const auto result = reconstruct(fixture.job, cancellation.get_token(), {});
        REQUIRE_FALSE(result);
        CHECK(result.error().code == error_code::CANCELLED);
        CHECK(sha256_file(manifest, 65536).value() == committed);
        return;
    }
    SECTION("cancel at compilation boundary") {
        std::stop_source cancellation;
        const auto result =
            reconstruct(fixture.job, cancellation.get_token(), [&](const std::string& text) {
                if (text.find("Compiling checked fit") != std::string::npos)
                    cancellation.request_stop();
            });
        REQUIRE_FALSE(result);
        CHECK(result.error().code == error_code::CANCELLED);
        CHECK(sha256_file(manifest, 65536).value() == committed);
        return;
    }
    REQUIRE_FALSE(reconstruct(fixture.job, {}, {}));
    CHECK(sha256_file(manifest, 65536).value() == committed);
}

TEST_CASE(
    "reconstruction request resolves relative paths and rejects implicit or malformed recipes") {
    job_fixture fixture;
    const auto request = fixture.root / "request.json";
    nlohmann::json config{{"schema_version", 1},
                          {"recipe", "local_gsplat_v1"},
                          {"action", "reuse_completed"},
                          {"video", "capture.mp4"},
                          {"pose_job", "poses"},
                          {"fit_directory", "fit"},
                          {"scene_output", "scene.wexplore"},
                          {"map_directory", "shared-map"},
                          {"decode_stage", "poses/stages/decode-" + fixture.poses.decode_key},
                          {"steps", 7000},
                          {"max_image_dimension", 1280},
                          {"max_splats", 2000000},
                          {"completed_fit_identity", fixture.identity}};
    save_json(request, config);
    const auto parsed = read_reconstruction_job(request);
    REQUIRE(parsed);
    CHECK(std::filesystem::equivalent(parsed->video, fixture.job.video));
    CHECK(parsed->map_directory == std::filesystem::canonical(fixture.root) / "shared-map");
    CHECK(std::filesystem::equivalent(parsed->decode_stage, fixture.poses.decoded));
    SECTION("unknown field") { config["remote_fallback"] = true; }
    SECTION("different recipe") { config["recipe"] = "standard_nvidia"; }
    SECTION("missing recipe") { config.erase("recipe"); }
    SECTION("signed resource") { config["max_splats"] = -1; }
    SECTION("fractional resource") { config["steps"] = 7000.5; }
    SECTION("overflow") { config["max_image_dimension"] = 4294967297ULL; }
    SECTION("output overwrites source") { config["scene_output"] = "capture.mp4"; }
    SECTION("output overwrites inputs") { config["scene_output"] = "."; }
    SECTION("output mutates pose stage") { config["scene_output"] = "poses/scene.wexplore"; }
    SECTION("output mutates fit") { config["scene_output"] = "fit/scene.wexplore"; }
    SECTION("map overlaps source") { config["map_directory"] = "capture.mp4"; }
    SECTION("map mutates pose job") { config["map_directory"] = "poses/map"; }
    SECTION("map mutates fit") { config["map_directory"] = "fit/map"; }
    SECTION("map overlaps scene") { config["map_directory"] = "scene.wexplore"; }
    SECTION("map includes all outputs") { config["map_directory"] = "."; }
    SECTION("start would clobber completed fit") { config["action"] = "start"; }
    SECTION("reuse needs explicit identity") { config.erase("completed_fit_identity"); }
    save_json(request, config);
    CHECK_FALSE(read_reconstruction_job(request));
}

TEST_CASE("coordinator worker failure and missing completion cannot replace an existing scene") {
    job_fixture fixture;
    REQUIRE(reconstruct(fixture.job, {}, {}));
    const auto manifest = fixture.job.scene_output / "manifest.json";
    const auto committed = sha256_file(manifest, 65536).value();
    fixture.job.action = fit_action::START;
    fixture.job.completed_fit_identity.clear();
    fixture.job.fit_directory = fixture.root / "fresh-fit";
    fixture.job.gsplat_source = fixture.root / "backend source with spaces";
    fixture.job.fitting_worker = {fixture.root / "worker.py", WE_TEST_PYTHON, false};
    int exit_code = 7;
    SECTION("backend nonzero exit") {}
    SECTION("zero exit without a fit result") { exit_code = 0; }
    std::ofstream(fixture.job.fitting_worker.script)
        << "import sys\nprint('diagnostic backend output')\nsys.exit(" << exit_code << ")\n";
    const auto result = reconstruct(fixture.job, {}, {});
    REQUIRE_FALSE(result);
    if (exit_code)
        CHECK(result.error().code == error_code::BACKEND_FAILURE);
    CHECK(sha256_file(manifest, 65536).value() == committed);
}

TEST_CASE("coordinator forwards bounded start and resume arguments and rechecks inputs after "
          "worker completion") {
    job_fixture fixture;
    REQUIRE(reconstruct(fixture.job, {}, {}));
    const auto manifest = fixture.job.scene_output / "manifest.json";
    const auto committed = sha256_file(manifest, 65536).value();
    const auto previous_fit = fixture.job.fit_directory;
    fixture.job.fit_directory = fixture.root / "new fit";
    fixture.job.gsplat_source = fixture.root / "source $literal ; with spaces";
    fixture.job.fitting_worker = {fixture.root / "worker.py", WE_TEST_PYTHON, false};
    fixture.job.action = fit_action::START;
    fixture.job.completed_fit_identity.clear();
    bool change_source = false;
    SECTION("start publishes a checked diagnostic result") {}
    SECTION("resume forwards explicit checkpoint") {
        fixture.job.action = fit_action::RESUME;
        fixture.job.checkpoint = fixture.root / "checkpoint with spaces.pt";
        std::ofstream(fixture.job.checkpoint) << "diagnostic checkpoint";
    }
    SECTION("source changed by worker prevents publication") { change_source = true; }
    // A synthetic subprocess contract fixture, never a reconstruction gate.
    const auto literal = [](const std::filesystem::path& path) {
        return nlohmann::json(path.string()).dump();
    };
    std::ofstream script(fixture.job.fitting_worker.script);
    script
        << "import argparse, shutil\nfrom pathlib import Path\np=argparse.ArgumentParser()\n"
           "for k in "
           "('source','data','video','output','steps','max-image-dimension','max-splats'):\n"
           " p.add_argument('--'+k,required=True)\np.add_argument('--resume')\na=p.parse_args()\n"
        << "assert a.source == " << literal(fixture.job.gsplat_source) << "\n"
        << "assert Path(a.data).samefile(" << literal(fixture.stage / "training") << ")\n"
        << "assert a.video == " << literal(fixture.job.video) << "\n"
        << "assert a.steps == '7000' and a.max_image_dimension == '1280' and a.max_splats == "
           "'2000000'\n"
        << "assert a.resume == "
        << (fixture.job.checkpoint.empty() ? std::string("None") : literal(fixture.job.checkpoint))
        << "\n"
        << "shutil.copytree(" << literal(previous_fit) << ",a.output)\n";
    if (change_source)
        script << "Path(a.video).write_text('changed capture')\n";
    script << "print('synthetic subprocess contract fixture')\n";
    script.close();
    std::string worker_log;
    const auto result =
        reconstruct(fixture.job, {}, [&](const std::string& text) { worker_log += text; });
    INFO(worker_log);
    INFO((result ? "published" : result.error().message));
    REQUIRE(worker_log.find("synthetic subprocess contract fixture") != std::string::npos);
    if (change_source) {
        REQUIRE_FALSE(result);
        CHECK(result.error().code == error_code::INVALID_DATA);
        CHECK(sha256_file(manifest, 65536).value() == committed);
    } else {
        REQUIRE(result);
        const auto scene = load_bundle(*result);
        REQUIRE(scene);
        CHECK((*scene)->provenance.at("fit_identity") == fixture.identity);
    }
}

TEST_CASE("pose preparation reopens identical dependencies and holds the writer through use",
          "[poses][persistence][application]") {
    job_fixture fixture;
    const pose_preparation_request request{{fixture.job.pose_job, fixture.job.video, {}},
                                           fixture.root / "prepared-map"};
    auto prepared = prepare_completed_pose_map(request, {});
    REQUIRE(prepared);
    const auto hash = prepared->asset_sha256;
    const auto snapshot = prepared->store->acquire_snapshot();
    CHECK(snapshot->initial_pose_asset_sha256 == hash);
    CHECK(snapshot->poses.size() == 3);
    CHECK(prepared->provenance.source_sha256 == fixture.source_hash);
    CHECK(read_pose_asset(request.map_directory / "assets" / ("poses-" + hash + ".weposes"), hash));
    CHECK_FALSE(prepare_completed_pose_map(request, {}));
    const auto manifest = request.map_directory / "manifest.json";
    const auto before = sha256_file(manifest, 65536).value();
    prepared->store.reset();
    auto reopened = prepare_completed_pose_map(request, {});
    REQUIRE(reopened);
    CHECK(reopened->asset_sha256 == hash);
    CHECK(sha256_file(manifest, 65536).value() == before);
    CHECK(snapshot->poses.size() == 3);
    reopened->store.reset();
    SECTION("different canonical dependency is never reseeded") {
        fixture.poses.canonical["poses"][0]["world_from_camera"]["translation"][0] = 42.;
        fixture.poses.publish();
    }
    SECTION("corrections require explicit resync") {
        auto store = map_store::open_persistent({.directory = request.map_directory}).value();
        map_transaction transaction;
        transaction.operation = {2};
        transaction.base_revision = {1};
        transaction.new_revision = {2};
        auto pose = snapshot->poses.begin()->second;
        pose.revision = {2};
        pose.submap_from_camera.translation.x += 1.;
        transaction.poses.push_back(pose);
        REQUIRE(store->commit(transaction));
    }
    SECTION("new epoch requires explicit resync") {
        auto store = map_store::open_persistent({.directory = request.map_directory}).value();
        REQUIRE(store->reset({1}));
    }
    SECTION("corrupt manifest is never reset") { std::ofstream(manifest) << "broken"; }
    const auto preserved = sha256_file(manifest, 65536).value();
    CHECK_FALSE(prepare_completed_pose_map(request, {}));
    CHECK(sha256_file(manifest, 65536).value() == preserved);
}

TEST_CASE("coordinator cancellation retains durable preparation and restart reuses it",
          "[poses][persistence][application]") {
    job_fixture fixture;
    fixture.job.map_directory = fixture.root / "shared-map";
    const auto pointer = fixture.job.pose_job / "decode.json";
    save_json(pointer, {{"key", std::string(64, 'd')}, {"output", "missing stale stage"}});
    const auto pointer_hash = sha256_file(pointer, 65536).value();
    fixture.job.decode_stage = fixture.poses.decoded;
    std::stop_source cancellation;
    const auto interrupted =
        reconstruct(fixture.job, cancellation.get_token(), [&](const std::string& text) {
            if (text.find("Durable pose map ready") != std::string::npos)
                cancellation.request_stop();
        });
    REQUIRE_FALSE(interrupted);
    CHECK(interrupted.error().code == error_code::CANCELLED);
    CHECK_FALSE(std::filesystem::exists(fixture.job.scene_output / "manifest.json"));
    const auto manifest = fixture.job.map_directory / "manifest.json";
    const auto committed = sha256_file(manifest, 65536).value();
    REQUIRE(reconstruct(fixture.job, {}, {}));
    CHECK(sha256_file(manifest, 65536).value() == committed);
    CHECK(sha256_file(pointer, 65536).value() == pointer_hash);
    const auto scene = load_bundle(fixture.job.scene_output).value();
    auto map = map_store::open_persistent({.directory = fixture.job.map_directory}).value();
    CHECK(map->acquire_snapshot()->initial_pose_asset_sha256 ==
          scene->provenance.at("pose_asset_sha256"));
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
    CHECK(load_bundle(fixture.job.scene_output).value()->provenance == scene->provenance);
    CHECK(sha256_file(manifest, 65536).value() == committed);
}

TEST_CASE("schema 2 requires explicit checked inspection and retained pose adoption",
          "[preflight]") {
    job_fixture fixture;
    fixture.adopt_inspection();
    const auto request = fixture.root / "request.json";
    auto config = nlohmann::json{
        {"schema_version", 2},
        {"recipe", "local_gsplat_v1"},
        {"action", "reuse_completed"},
        {"pose_action", "adopt_completed"},
        {"video", "capture.mp4"},
        {"pose_job", "poses"},
        {"fit_directory", "fit"},
        {"scene_output", "scene.wexplore"},
        {"steps", 7000},
        {"max_image_dimension", 1280},
        {"max_splats", 2000000},
        {"completed_fit_identity", fixture.identity},
        {"inspection",
         {{"action", "adopt_completed"},
          {"stage", test::path_utf8(fixture.inspection().stage.lexically_relative(fixture.root))},
          {"completion_sha256", fixture.inspection().completion_sha256},
          {"minimum_free_bytes", 1}}}};
    save_json(request, config);
    auto parsed = read_reconstruction_job(request);
    REQUIRE(parsed);
    REQUIRE(parsed->inspection);
    if (parsed->inspection)
        CHECK(std::filesystem::equivalent(parsed->inspection->stage, fixture.inspection().stage));
    REQUIRE(reconstruct(*parsed, {}, {}));
    SECTION("implicit pose choice") { config.erase("pose_action"); }
    SECTION("unsupported fresh solve") { config["pose_action"] = "solve"; }
    SECTION("implicit inspection choice") { config["inspection"].erase("action"); }
    SECTION("implicit checksum") { config["inspection"].erase("completion_sha256"); }
    SECTION("unknown nested option") { config["inspection"]["remote"] = true; }
    SECTION("invalid digest") { config["inspection"]["completion_sha256"] = "not a digest"; }
    SECTION("zero storage requirement") { config["inspection"]["minimum_free_bytes"] = 0; }
    SECTION("negative storage requirement") { config["inspection"]["minimum_free_bytes"] = -1; }
    SECTION("fractional storage requirement") { config["inspection"]["minimum_free_bytes"] = 1.5; }
    SECTION("oversized storage requirement") {
        config["inspection"]["minimum_free_bytes"] = UINT64_MAX;
    }
    SECTION("float schema version") { config["schema_version"] = 2.; }
    SECTION("legacy schema cannot claim preflight") { config["schema_version"] = 1; }
    save_json(request, config);
    CHECK_FALSE(read_reconstruction_job(request));
}

TEST_CASE("retained video preflight checks metadata identity bounds and read-only admission",
          "[preflight]") {
    job_fixture fixture;
    fixture.adopt_inspection();
    video_preflight_request request{
        fixture.job.video, fixture.inspection(), {fixture.root / "new-volume/future-map"}};
    const auto initial = preflight_video(request, {});
    REQUIRE(initial);
    CHECK(initial->source_sha256 == fixture.source_hash);
    CHECK(initial->source_bytes == std::filesystem::file_size(fixture.job.video));
    CHECK(initial->rotation_degrees == -90);
    CHECK(initial->time_base_numerator == 1);
    CHECK(initial->time_base_denominator == 30);
    CHECK_FALSE(std::filesystem::exists(request.outputs[0].parent_path()));
    const auto metadata_path = request.inspection.stage / "result.json";
    auto metadata = nlohmann::json::parse(std::ifstream(metadata_path));
    const auto original_metadata = metadata.dump();
    SECTION("video differs") { std::ofstream(fixture.job.video) << "another capture"; }
    SECTION("checksum differs") { request.inspection.completion_sha256 = std::string(64, 'b'); }
    SECTION("missing inventory file") { std::filesystem::remove(metadata_path); }
    SECTION("uncommitted file") {
        std::ofstream(request.inspection.stage / "extra") << "uncommitted";
    }
    SECTION("inspection byte budget is checked before hashing oversized entries") {
        const auto path = request.inspection.stage / "completion.json";
        auto completion = nlohmann::json::parse(std::ifstream(path));
        completion["files"][0]["bytes"] = 17ULL * 1024 * 1024;
        save_json(path, completion);
        request.inspection.completion_sha256 = sha256_file(path, 65536).value();
    }
    SECTION("inspection file count is bounded independently of pose stages") {
        for (int index = 0; index < 16; ++index)
            std::ofstream(request.inspection.stage / ("extra-" + std::to_string(index)))
                << "diagnostic";
        fixture.poses.inventory(request.inspection.stage);
        request.inspection.completion_sha256 =
            sha256_file(request.inspection.stage / "completion.json", 65536).value();
    }
    SECTION("symlink output escape") { request.outputs[0] = request.inspection.stage / "output"; }
    SECTION("source output overlap") { request.outputs[0] = fixture.root; }
    SECTION("output parent must be a directory") {
        std::ofstream(fixture.root / "occupied-file") << "preserve";
        request.outputs[0] = fixture.root / "occupied-file/future-map";
    }
    SECTION("zero storage budget") { request.inspection.minimum_free_bytes = 0; }
#if defined(__linux__)
    SECTION("insufficient or unknown storage blocks retained work") {
        const auto available = std::filesystem::space("/proc").available;
        REQUIRE((available == 0 || available == UINTMAX_MAX));
        request.outputs[0] = "/proc/we-preflight-not-created";
    }
#endif
    SECTION("unsigned rotation cannot wrap") { metadata["rotation_degrees"] = UINT64_MAX - 89; }
    SECTION("fractional rotation") { metadata["rotation_degrees"] = 90.5; }
    SECTION("nonorthogonal rotation") { metadata["rotation_degrees"] = 45; }
    SECTION("unbounded pixel count") { metadata["stream"]["width"] = UINT64_MAX; }
    SECTION("empty dimensions") { metadata["stream"]["height"] = 0; }
    SECTION("excessive duration") { metadata["duration_seconds"] = 1201.; }
    SECTION("nonpositive duration") { metadata["duration_seconds"] = 0.; }
    SECTION("zero time denominator") { metadata["stream"]["time_base"] = "1/0"; }
    SECTION("extra time delimiter") { metadata["stream"]["time_base"] = "1/30/2"; }
    SECTION("time integer overflow") { metadata["stream"]["time_base"] = "1/9223372036854775808"; }
    SECTION("HDR requires explicit conversion") {
        metadata["stream"]["color_transfer"] = "smpte2084";
    }
    SECTION("wrong source hash") { metadata["source"]["sha256"] = std::string(64, 'a'); }
    SECTION("wrong source size") { metadata["source"]["bytes"] = 1; }
    SECTION("invalid stream type") { metadata["stream"]["codec_type"] = "audio"; }
    SECTION("fractional metadata version") { metadata["schema_version"] = 1.; }
    SECTION("signed dimensions") { metadata["stream"]["width"] = -2; }
    SECTION("cancelled admission") {
        std::stop_source cancellation;
        cancellation.request_stop();
        auto result = preflight_video(request, cancellation.get_token());
        REQUIRE_FALSE(result);
        CHECK(result.error().code == error_code::CANCELLED);
        return;
    }
    // Changed metadata is explicitly selected anew so semantic checks, rather
    // than only the inventory checksum, reject it. Byte fixtures are synthetic.
    if (std::filesystem::exists(metadata_path) && metadata.dump() != original_metadata) {
        save_json(metadata_path, metadata);
        fixture.poses.inventory(request.inspection.stage);
        request.inspection.completion_sha256 =
            sha256_file(request.inspection.stage / "completion.json", 65536).value();
    }
    INFO(metadata.dump());
    CHECK_FALSE(preflight_video(request, {}));
    CHECK_FALSE(std::filesystem::exists(fixture.root / "new-volume"));
}

TEST_CASE("coordinator preflight failure preserves output and inspection dependencies on restart",
          "[preflight]") {
    job_fixture fixture;
    fixture.adopt_inspection();
    fixture.job.map_directory = fixture.root / "prepared-map";
    const auto inspection_hash = fixture.inspection().completion_sha256;
    REQUIRE(reconstruct(fixture.job, {}, {}));
    CHECK(load_bundle(fixture.job.scene_output)
              .value()
              ->provenance.at("inspection_completion_sha256") == inspection_hash);
    const auto manifest = fixture.job.scene_output / "manifest.json";
    const auto committed = sha256_file(manifest, 65536).value();
    const auto map_hash = sha256_file(fixture.job.map_directory / "manifest.json", 65536).value();
    REQUIRE(reconstruct(fixture.job, {}, {}));
    CHECK(sha256_file(manifest, 65536).value() == committed);
    CHECK(sha256_file(fixture.job.map_directory / "manifest.json", 65536).value() == map_hash);
    SECTION("bad initial inspection creates no new map or fit") {
        fixture.inspection().completion_sha256 = std::string(64, 'b');
        fixture.job.map_directory = fixture.root / "must-not-create-map";
        fixture.job.fit_directory = fixture.root / "must-not-create-fit";
        fixture.job.action = fit_action::START;
        fixture.job.completed_fit_identity.clear();
        fixture.job.gsplat_source = fixture.root / "absent-source";
        fixture.job.fitting_worker = {fixture.root / "must-not-launch.py", WE_TEST_PYTHON, false};
        REQUIRE_FALSE(reconstruct(fixture.job, {}, {}));
        CHECK_FALSE(std::filesystem::exists(fixture.job.map_directory));
        CHECK_FALSE(std::filesystem::exists(fixture.job.fit_directory));
    }
    SECTION("inspection changes during fitting boundary") {
        const auto result = reconstruct(fixture.job, {}, [&](const std::string& text) {
            if (text.find("Durable pose map ready") != std::string::npos) {
                std::ofstream(fixture.inspection().stage / "result.json") << "changed metadata";
            }
        });
        REQUIRE_FALSE(result);
    }
    CHECK(sha256_file(manifest, 65536).value() == committed);
}

TEST_CASE("automatic inspection request resolves local worker paths and rejects protected overlaps",
          "[inspection]") {
    job_fixture fixture;
    test::inspection_fixture inspector{fixture.root / "inspection-inputs", fixture.job.video};
    const auto request = fixture.root / "request.json";
    auto config = nlohmann::json{{"schema_version", 2},
                                 {"recipe", "local_gsplat_v1"},
                                 {"action", "reuse_completed"},
                                 {"pose_action", "adopt_completed"},
                                 {"video", "capture.mp4"},
                                 {"pose_job", "poses"},
                                 {"fit_directory", "fit"},
                                 {"scene_output", "scene.wexplore"},
                                 {"steps", 7000},
                                 {"max_image_dimension", 1280},
                                 {"max_splats", 2000000},
                                 {"completed_fit_identity", fixture.identity},
                                 {"inspection",
                                  {{"action", "inspect"},
                                   {"job_directory", "inspection-inputs/metadata $ ; job"},
                                   {"worker_script", "inspection-inputs/worker with spaces.py"},
                                   {"python", WE_TEST_PYTHON},
                                   {"minimum_free_bytes", 1}}}};
    save_json(request, config);
    const auto parsed = read_reconstruction_job(request);
    REQUIRE(parsed);
    REQUIRE(parsed->automatic_inspection);
    CHECK_FALSE(parsed->inspection);
    CHECK(parsed->request_file == std::filesystem::canonical(request));
    if (parsed->automatic_inspection) {
        CHECK(std::filesystem::equivalent(parsed->automatic_inspection->worker.script,
                                          inspector.request.configuration.worker.script));
        CHECK(parsed->automatic_inspection->job_directory ==
              inspector.request.configuration.job_directory);
    }
    SECTION("missing local worker") { config["inspection"].erase("worker_script"); }
    SECTION("missing executable choice") { config["inspection"].erase("python"); }
    SECTION("unknown inspection option") { config["inspection"]["remote"] = true; }
    SECTION("implicit reuse is rejected") {
        config["inspection"]["completion_sha256"] = std::string(64, 'a');
    }
    SECTION("pose job protected") { config["inspection"]["job_directory"] = "poses/metadata"; }
    SECTION("map protected") { config["inspection"]["job_directory"] = "fit.map/metadata"; }
    SECTION("fit protected") { config["inspection"]["job_directory"] = "fit/metadata"; }
    SECTION("scene protected") {
        config["inspection"]["job_directory"] = "scene.wexplore/metadata";
    }
    SECTION("worker protected") { config["inspection"]["job_directory"] = "inspection-inputs"; }
    SECTION("request protected from scene output") { config["scene_output"] = "request.json"; }
    SECTION("request protected from fit output") { config["fit_directory"] = "request.json"; }
    SECTION("request protected from metadata output") {
        config["inspection"]["job_directory"] = ".";
    }
    SECTION("invalid resource threshold") { config["inspection"]["minimum_free_bytes"] = 0; }
    save_json(request, config);
    CHECK_FALSE(read_reconstruction_job(request));
    CHECK_FALSE(std::filesystem::exists(inspector.request.configuration.job_directory));
}

TEST_CASE(
    "shared coordinator inspects before retained map preparation and preserves scene on failure",
    "[inspection]") {
    job_fixture fixture;
    REQUIRE(reconstruct(fixture.job, {}, {}));
    const auto manifest = fixture.job.scene_output / "manifest.json";
    const auto original = sha256_file(manifest, 65536).value();
    const auto mode = GENERATE("valid", "failure", "escape", "no_video", "cancel_once");
    test::inspection_fixture inspector{fixture.root / "inspection-inputs", fixture.job.video, mode};
    fixture.job.automatic_inspection = inspector.request.configuration;
    fixture.job.map_directory = fixture.root / "automatic-map";
    std::stop_source cancellation;
    std::string log;
    const auto result =
        reconstruct(fixture.job, cancellation.get_token(), [&](const std::string& text) {
            log += text;
            if (std::string_view(mode) == "cancel_once" &&
                text.find("Worker accepted START") != std::string::npos)
                cancellation.request_stop();
        });
    INFO(log);
    if (std::string_view(mode) == "valid") {
        REQUIRE(result);
        const auto committed = sha256_file(manifest, 65536).value();
        const auto scene = load_bundle(fixture.job.scene_output).value();
        const auto stage = inspector.request.configuration.job_directory / "stages" /
                           ("inspect-" + std::string(64, 'c'));
        CHECK(scene->provenance.at("inspection_completion_sha256") ==
              sha256_file(stage / "completion.json", 65536).value());
        REQUIRE(reconstruct(fixture.job, {}, {}));
        CHECK(sha256_file(manifest, 65536).value() == committed);
    } else {
        REQUIRE_FALSE(result);
        CHECK(sha256_file(manifest, 65536).value() == original);
        CHECK_FALSE(std::filesystem::exists(fixture.job.map_directory));
        if (std::string_view(mode) == "cancel_once") {
            CHECK(result.error().code == error_code::CANCELLED);
            REQUIRE(reconstruct(fixture.job, {}, {}));
        }
    }
}

TEST_CASE("fresh coordinator chains decode RGB admission solve and publication with restart",
          "[stage-chain]") {
    job_fixture fixture;
    const auto automatic = GENERATE(false, true);
    fixture.fresh("valid", automatic);
    std::string log;
    const auto completed =
        reconstruct(fixture.job, {}, [&](const std::string& text) { log += text; });
    INFO(log);
    INFO((completed ? "complete" : completed.error().message));
    REQUIRE(completed);
    const auto manifest = fixture.job.scene_output / "manifest.json";
    const auto published = sha256_file(manifest, 65536).value();
    const auto counter = sha256_file(fixture.job.pose_job / "capabilities.json", 65536).value();
    log.clear();
    REQUIRE(reconstruct(fixture.job, {}, [&](const std::string& text) { log += text; }));
    CHECK(log.find("solver skipped") != std::string::npos);
    CHECK(sha256_file(fixture.job.pose_job / "capabilities.json", 65536).value() == counter);
    CHECK(sha256_file(manifest, 65536).value() == published);
    SECTION("corrupt RGB receipt") {
        std::ofstream(fixture.job.pose_job / "input/1.ppm") << "changed";
    }
    SECTION("different decode dependency") {
        fixture.poses.inventory(fixture.poses.decoded);
        if (fixture.job.selected_decode)
            fixture.job.selected_decode->completion_sha256 = std::string(64, 'd');
        else if (fixture.job.automatic_decode)
            fixture.job.automatic_decode->sample_fps = 2.;
    }
    SECTION("recipe change") {
        REQUIRE(fixture.job.fresh_poses);
        if (fixture.job.fresh_poses)
            fixture.job.fresh_poses->recipe = pose_recipe::EXHAUSTIVE_GUIDED;
    }
    SECTION("pose worker change") {
        std::ofstream(fixture.root / "pose worker.py", std::ios::app) << "\n# changed\n";
    }
    CHECK_FALSE(reconstruct(fixture.job, {}, {}));
    CHECK(sha256_file(manifest, 65536).value() == published);
    CHECK(sha256_file(fixture.job.pose_job / "capabilities.json", 65536).value() == counter);
}

TEST_CASE("fresh stage cancellation resumes checked journals and preserves existing scene",
          "[stage-chain]") {
    job_fixture fixture;
    REQUIRE(reconstruct(fixture.job, {}, {}));
    const auto manifest = fixture.job.scene_output / "manifest.json";
    const auto original = sha256_file(manifest, 65536).value();
    fixture.fresh("cancel_once");
    fixture.job.map_directory = fixture.root / "fresh-map";
    std::stop_source cancellation;
    std::string log;
    const auto failed =
        reconstruct(fixture.job, cancellation.get_token(), [&](const std::string& text) {
            log += text;
            if (text.find("Worker accepted START") != std::string::npos)
                cancellation.request_stop();
        });
    INFO(log);
    REQUIRE_FALSE(failed);
    CHECK(failed.error().code == error_code::CANCELLED);
    CHECK(sha256_file(manifest, 65536).value() == original);
    CHECK_FALSE(std::filesystem::exists(fixture.job.map_directory));
    const auto receipt = sha256_file(fixture.job.pose_job / "input/1.ppm", 4096).value();
    const auto journal = sha256_file(fixture.job.pose_job / "input/journal.json", 65536).value();
    const auto completed = reconstruct(fixture.job, {}, {});
    INFO((completed ? "restarted" : completed.error().message));
    REQUIRE(completed);
    CHECK(sha256_file(fixture.job.pose_job / "input/1.ppm", 4096).value() == receipt);
    CHECK(sha256_file(fixture.job.pose_job / "input/journal.json", 65536).value() == journal);
}

TEST_CASE("fresh stage failure and unowned poses cannot publish or invoke fitting",
          "[stage-chain]") {
    job_fixture fixture;
    REQUIRE(reconstruct(fixture.job, {}, {}));
    const auto manifest = fixture.job.scene_output / "manifest.json";
    const auto original = sha256_file(manifest, 65536).value();
    fixture.fresh("failure", true);
    fixture.job.map_directory = fixture.root / "fresh-map";
    SECTION("pose backend fails") {}
    SECTION("decode corrupt") {
        test::stage_worker(fixture.root / "decode worker.py", fixture.poses, "corrupt");
    }
    SECTION("unowned existing poses") {
        std::filesystem::create_directory(fixture.job.pose_job);
        std::ofstream(fixture.job.pose_job / "foreign") << "preserve";
    }
    SECTION("overlapping request") {
        fixture.job.request_file = fixture.job.pose_job / "request.json";
    }
    REQUIRE_FALSE(reconstruct(fixture.job, {}, {}));
    CHECK_FALSE(std::filesystem::exists(fixture.job.map_directory));
    CHECK(sha256_file(manifest, 65536).value() == original);
}

TEST_CASE("fresh reconstruction requests require explicit stages resources and disjoint jobs",
          "[stage-chain]") {
    job_fixture fixture;
    fixture.fresh("valid", true);
    const auto request = fixture.root / "request.json";
    auto config = nlohmann::json{{"schema_version", 2},
                                 {"recipe", "local_gsplat_v1"},
                                 {"action", "reuse_completed"},
                                 {"pose_action", "solve"},
                                 {"video", "capture.mp4"},
                                 {"pose_job", "fresh poses"},
                                 {"fit_directory", "fit"},
                                 {"scene_output", "scene.wexplore"},
                                 {"steps", 7000},
                                 {"max_image_dimension", 1280},
                                 {"max_splats", 2000000},
                                 {"completed_fit_identity", fixture.identity},
                                 {"inspection",
                                  {{"action", "adopt_completed"},
                                   {"stage", test::path_utf8(fixture.inspection().stage)},
                                   {"completion_sha256", fixture.inspection().completion_sha256},
                                   {"minimum_free_bytes", 1}}},
                                 {"pose_worker",
                                  {{"worker_script", "pose worker.py"},
                                   {"worker_python", WE_TEST_PYTHON},
                                   {"pose_python", WE_TEST_PYTHON},
                                   {"recipe", "sequential"},
                                   {"cpu_threads", 4},
                                   {"journal_bytes", 2147483648ULL}}},
                                 {"decode",
                                  {{"action", "decode"},
                                   {"job_directory", "decoded video"},
                                   {"worker_script", "decode worker.py"},
                                   {"python", WE_TEST_PYTHON},
                                   {"sample_fps", 3.},
                                   {"max_frames", 120},
                                   {"cpu_threads", 4},
                                   {"max_output_bytes", 34359738368ULL}}}};
    save_json(request, config);
    const auto parsed = read_reconstruction_job(request);
    INFO((parsed ? "parsed" : parsed.error().message));
    REQUIRE(parsed);
    REQUIRE(parsed->fresh_poses);
    REQUIRE(parsed->automatic_decode);
    REQUIRE(reconstruct(*parsed, {}, {}));
    SECTION("unknown decode option") { config["decode"]["remote"] = true; }
    SECTION("unknown pose option") { config["pose_worker"]["remote"] = true; }
    SECTION("missing executable") { config["pose_worker"].erase("pose_python"); }
    SECTION("different pose action") { config["pose_action"] = "adopt_completed"; }
    SECTION("invalid rate") { config["decode"]["sample_fps"] = 0.; }
    SECTION("non-numeric rate") { config["decode"]["sample_fps"] = "3"; }
    SECTION("fractional frame limit") { config["decode"]["max_frames"] = 120.5; }
    SECTION("oversized frames") { config["decode"]["max_frames"] = 121; }
    SECTION("negative bytes") { config["decode"]["max_output_bytes"] = -1; }
    SECTION("insufficient journal") { config["pose_worker"]["journal_bytes"] = 1; }
    SECTION("unknown recipe") { config["pose_worker"]["recipe"] = "automatic_fallback"; }
    SECTION("decode includes request") { config["decode"]["job_directory"] = "."; }
    SECTION("decode mutates retained input") { config["decode"]["job_directory"] = "poses"; }
    SECTION("pose output includes decoder") { config["pose_job"] = "decoded video"; }
    SECTION("map mutates pose intent") { config["map_directory"] = "fresh poses.plan/map"; }
    SECTION("scene overlaps intent") { config["scene_output"] = "fresh poses.plan"; }
    SECTION("legacy decode selection has different meaning") {
        config["decode_stage"] = "poses/stages/decode-" + fixture.poses.decode_key;
    }
    save_json(request, config);
    CHECK_FALSE(read_reconstruction_job(request));
}

TEST_CASE("pose preparation rejects unsafe destinations and cancelled admission",
          "[poses][persistence][application]") {
    job_fixture fixture;
    pose_preparation_request request{{fixture.job.pose_job, fixture.job.video, {}},
                                     fixture.root / "prepared-map"};
    SECTION("input overlap") { request.map_directory = fixture.job.pose_job / "map"; }
    SECTION("occupied directory") {
        std::filesystem::create_directory(request.map_directory);
        std::ofstream(request.map_directory / "unowned.txt") << "preserve";
    }
    SECTION("empty destination") { request.map_directory.clear(); }
    SECTION("cancel before initialization") {
        std::stop_source stop;
        stop.request_stop();
        const auto result = prepare_completed_pose_map(request, stop.get_token());
        REQUIRE_FALSE(result);
        CHECK(result.error().code == error_code::CANCELLED);
        CHECK_FALSE(std::filesystem::exists(request.map_directory));
        return;
    }
    CHECK_FALSE(prepare_completed_pose_map(request, {}));
    CHECK_FALSE(std::filesystem::exists(request.map_directory / "manifest.json"));
}
