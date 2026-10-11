#include "world_explorer/application/reconstruction_job.hpp"
#include "world_explorer/application/scenes.hpp"
#include "world_explorer/core/hash.hpp"
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>

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
        stage = job.pose_job / "stages" / ("poses-" + std::string(64, 'a'));
        std::filesystem::create_directories(stage / "training/images");
        std::filesystem::create_directories(stage / "training/sparse");
        std::filesystem::create_directories(job.fit_directory / "assets");
        // Diagnostic byte fixtures exercise admission, not capture reconstruction.
        std::ofstream(job.video) << "synthetic contract input";
        source_hash = sha256_file(job.video, 1024).value();
        std::ofstream(stage / "training/images/frame.png") << "diagnostic image bytes";
        image_hash = sha256_file(stage / "training/images/frame.png", 1024).value();
        for (const auto* name : {"cameras.bin", "images.bin", "points3D.bin"})
            std::ofstream(stage / "training/sparse" / name) << "diagnostic model bytes";
        nlohmann::json files = nlohmann::json::array();
        for (const auto& entry : std::filesystem::recursive_directory_iterator(stage))
            if (entry.is_regular_file())
                files.push_back({{"path", entry.path().lexically_relative(stage).generic_string()},
                                 {"bytes", entry.file_size()},
                                 {"sha256", sha256_file(entry.path(), 1024).value()}});
        save_json(stage / "completion.json", {{"schema_version", 1},
                                              {"status", "complete"},
                                              {"key", std::string(64, 'a')},
                                              {"files", files}});
        save_json(job.pose_job / "poses.json",
                  {{"key", std::string(64, 'a')}, {"output", stage.string()}});
        save_json(job.pose_job / "result.json", {{"schema_version", 1},
                                                 {"status", "complete"},
                                                 {"stage", "poses"},
                                                 {"source_sha256", source_hash},
                                                 {"output", stage.string()}});
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
        save_json(
            job.fit_directory / "input_manifest.json",
            {{"recipe", "local_gsplat_v1"},
             {"source_sha256", source_hash},
             {"steps", 7000},
             {"max_image_dimension", 1280},
             {"max_splats", 2000000},
             {"dataset",
              {{"scene_scale", 2},
               {"frames", nlohmann::json::array(
                              {{{"name", "frame.png"},
                                {"sha256", image_hash},
                                {"height", 128},
                                {"view", {{1, 0, 0, 1}, {0, 1, 0, 2}, {0, 0, 1, 3}, {0, 0, 0, 1}}},
                                {"intrinsic", {{200, 0, 64}, {0, 200, 64}, {0, 0, 1}}}}})}}}});
    }
    ~job_fixture() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
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
    const auto manifest = fixture.job.scene_output / "manifest.json";
    const auto committed = sha256_file(manifest, 65536).value();
    SECTION("different completed identity") {
        fixture.job.completed_fit_identity = std::string(64, 'c');
    }
    SECTION("different recipe settings") { fixture.job.max_image_dimension = 640; }
    SECTION("changed source") { std::ofstream(fixture.job.video) << "different source"; }
    SECTION("changed image") {
        std::ofstream(fixture.stage / "training/images/frame.png") << "corrupt";
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
                          {"steps", 7000},
                          {"max_image_dimension", 1280},
                          {"max_splats", 2000000},
                          {"completed_fit_identity", fixture.identity}};
    save_json(request, config);
    const auto parsed = read_reconstruction_job(request);
    REQUIRE(parsed);
    CHECK(std::filesystem::equivalent(parsed->video, fixture.job.video));
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
    fixture.job.fitting_worker = {fixture.root / "worker.py", WE_TEST_PYTHON};
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
    fixture.job.fitting_worker = {fixture.root / "worker.py", WE_TEST_PYTHON};
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
