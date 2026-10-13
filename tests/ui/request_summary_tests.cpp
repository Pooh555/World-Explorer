#include "../unit/reconstruction_fixture.hpp"
#include "job_state.hpp"
#include "request_summary.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace world_explorer;
namespace {
std::string row(const viewer::request_summary& summary, const std::string& label) {
    for (const auto* rows : {&summary.rows, &summary.advanced})
        for (const auto& field : *rows)
            if (field.label == label)
                return field.value;
    return {};
}
} // namespace
TEST_CASE("review distinguishes retained compatibility limits from new optimizer work",
          "[review]") {
    test::reconstruction_fixture fixture;
    fixture.adopt_inspection();
    const auto request = fixture.write_retained_request();
    auto config = nlohmann::json::parse(std::ifstream(request));
    const auto action = GENERATE("reuse_completed", "start", "resume");
    config["action"] = action;
    if (std::string_view(action) != "reuse_completed") {
        config.erase("completed_fit_identity");
        config["fit_directory"] = test::path_utf8(fixture.root / "new fit");
        config["gsplat_source"] = test::path_utf8(fixture.root / "backend source");
        config["fit_worker_script"] = test::path_utf8(fixture.root / "worker.py");
        config["fit_python"] = WE_TEST_PYTHON;
        if (std::string_view(action) == "resume") {
            std::ofstream(fixture.root / "checkpoint.pt") << "synthetic optimizer bytes";
            config["checkpoint"] = "checkpoint.pt";
        }
    }
    test::save_reconstruction_json(request, config);
    const auto summary = viewer::review_reconstruction_request(request);
    INFO((summary ? "checked synthetic request" : summary.error().message));
    REQUIRE(summary);
    CHECK(summary->request == std::filesystem::canonical(request));
    CHECK(summary->request_sha256 == sha256_file(request, 65536).value());
    CHECK(row(*summary, "Video") == test::path_utf8(fixture.job.video));
    CHECK(row(*summary, "Pose action") == "Adopt completed poses");
    CHECK(row(*summary, "Minimum free space").starts_with("1 bytes"));
    CHECK(row(*summary, "Selected frames").empty()); // No guessed counts/rate for retained poses.
    if (std::string_view(action) == "reuse_completed") {
        CHECK(row(*summary, "Appearance action").find("optimization skipped") != std::string::npos);
        CHECK(row(*summary, "Fit steps") == "7000 (fit compatibility)");
        CHECK(row(*summary, "Fit Python").empty());
    } else {
        CHECK(row(*summary, "Fit steps") == "7000");
        CHECK(row(*summary, "Fit Python") == WE_TEST_PYTHON);
        CHECK(row(*summary, "Appearance action").find("skipped") == std::string::npos);
    }
    if (std::string_view(action) == "resume")
        CHECK(row(*summary, "Checkpoint") == test::path_utf8(fixture.root / "checkpoint.pt"));
    CHECK_FALSE(std::filesystem::exists(fixture.job.scene_output));
    CHECK_FALSE(std::filesystem::exists(fixture.root / "new fit"));
}

TEST_CASE("review shows fresh frame pose storage and resource choices without running workers",
          "[review]") {
    test::reconstruction_fixture fixture;
    fixture.fresh("failure", true);
    // Start from a retained request, then explicitly select the fresh pipeline.
    const auto request = fixture.write_retained_request();
    auto config = nlohmann::json::parse(std::ifstream(request));
    config["pose_action"] = "solve";
    REQUIRE(fixture.job.fresh_poses);
    if (!fixture.job.fresh_poses)
        return;
    const auto& pose = *fixture.job.fresh_poses;
    config["pose_worker"] = {{"worker_script", test::path_utf8(pose.worker.script)},
                             {"worker_python", WE_TEST_PYTHON},
                             {"pose_python", WE_TEST_PYTHON},
                             {"recipe", "exhaustive_guided"},
                             {"cpu_threads", 2},
                             {"journal_bytes", 67108864}};
    REQUIRE(fixture.job.automatic_decode);
    if (!fixture.job.automatic_decode)
        return;
    const auto& decode = *fixture.job.automatic_decode;
    config["decode"] = {{"action", "decode"},
                        {"job_directory", test::path_utf8(decode.job_directory)},
                        {"worker_script", test::path_utf8(decode.worker.script)},
                        {"python", WE_TEST_PYTHON},
                        {"sample_fps", 1.75},
                        {"max_frames", 40},
                        {"cpu_threads", 2},
                        {"max_output_bytes", 1073741824}};
    config["steps"] = 30000;
    config["max_image_dimension"] = 1024;
    config["max_splats"] = 100000;
    test::save_reconstruction_json(request, config);
    const auto summary = viewer::review_reconstruction_request(request);
    INFO((summary ? "checked synthetic fresh configuration" : summary.error().message));
    REQUIRE(summary);
    CHECK(row(*summary, "Pose action").starts_with("Solve selected frames"));
    CHECK(row(*summary, "Selected frames") ==
          "Timestamp sampling: 1.75 frames/s, at most 40 frames");
    CHECK(row(*summary, "Decoded output cap") == "1073741824 bytes");
    CHECK(row(*summary, "Pose matching") == "Exhaustive guided");
    CHECK(row(*summary, "Pose CPU threads") == "2");
    CHECK(row(*summary, "RGB journal cap") == "67108864 bytes");
    CHECK(row(*summary, "Fit steps").starts_with("30000"));
    CHECK(row(*summary, "Training image cap").starts_with("1024 pixels"));
    CHECK(row(*summary, "Transient splat cap").starts_with("100000 splats"));
    CHECK_FALSE(std::filesystem::exists(fixture.job.pose_job));
    CHECK_FALSE(std::filesystem::exists(decode.job_directory));
    CHECK_FALSE(std::filesystem::exists(fixture.job.scene_output));
}

TEST_CASE("review rejects malformed oversized and cancelled requests without writes", "[review]") {
    test::reconstruction_fixture fixture;
    fixture.adopt_inspection();
    const auto request = fixture.write_retained_request();
    SECTION("invalid action") {
        auto config = nlohmann::json::parse(std::ifstream(request));
        config["action"] = "restart";
        test::save_reconstruction_json(request, config);
    }
    SECTION("invalid resource cap") {
        auto config = nlohmann::json::parse(std::ifstream(request));
        config["max_splats"] = 2000001;
        test::save_reconstruction_json(request, config);
    }
    SECTION("malformed JSON") { std::ofstream(request) << "{invalid"; }
    SECTION("too large") { std::ofstream(request) << std::string(65537, ' '); }
    SECTION("missing") { std::filesystem::remove(request); }
    SECTION("directory") {
        std::filesystem::remove(request);
        std::filesystem::create_directory(request);
    }
    SECTION("explicit stop") {
        std::stop_source cancellation;
        cancellation.request_stop();
        const auto result =
            viewer::review_reconstruction_request(request, cancellation.get_token());
        REQUIRE_FALSE(result);
        CHECK(result.error().code == error_code::CANCELLED);
        return;
    }
    CHECK_FALSE(viewer::review_reconstruction_request(request));
    CHECK_FALSE(std::filesystem::exists(fixture.job.scene_output));
}

TEST_CASE("owned request review preserves the scene and rejects changed content at launch",
          "[review]") {
    test::reconstruction_fixture fixture;
    fixture.adopt_inspection();
    REQUIRE(reconstruct(fixture.job, {}, {}));
    const auto request = fixture.write_retained_request();
    const auto before = sha256_file(fixture.job.scene_output / "manifest.json", 65536).value();
    viewer::application_state state;
    state.review_request(request);
    CHECK_FALSE(state.processing); // Reviewing is not reconstruction and must not pause appearance.
    state.job.join();
    REQUIRE(state.reviewed_request);
    CHECK_FALSE(state.loaded);
    CHECK_FALSE(state.busy);
    CHECK(state.review_error.empty());
    SECTION("unchanged checked request publishes through normal admission") {
        state.run_reviewed_request();
        state.job.join();
        REQUIRE(state.loaded);
        CHECK(state.reconstruction_error.empty());
        CHECK(state.has_pending_work());
    }
    SECTION("changed valid request cannot silently run") {
        auto config = nlohmann::json::parse(std::ifstream(request));
        config["scene_output"] = test::path_utf8(fixture.root / "different scene.wexplore");
        test::save_reconstruction_json(request, config);
        state.run_reviewed_request();
        state.job.join();
        CHECK_FALSE(state.loaded);
        CHECK_FALSE(state.reviewed_request);
        CHECK(state.review_error.find("Review again") != std::string::npos);
        CHECK(state.reconstruction_error.find("checksum") != std::string::npos);
        CHECK_FALSE(std::filesystem::exists(fixture.root / "different scene.wexplore"));
    }
    SECTION("deleted reviewed file cannot launch") {
        std::filesystem::remove(request);
        state.run_reviewed_request();
        state.job.join();
        CHECK_FALSE(state.loaded);
        CHECK_FALSE(state.reviewed_request);
        CHECK(state.review_error.find("Review again") != std::string::npos);
        CHECK(state.reconstruction_error.find("path changed") != std::string::npos);
    }
    SECTION("field editing clears eligibility to run") {
        state.clear_request_review();
        state.run_reviewed_request();
        CHECK_FALSE(state.busy);
        CHECK_FALSE(state.reviewed_request);
        CHECK_FALSE(state.loaded);
    }
    CHECK(sha256_file(fixture.job.scene_output / "manifest.json", 65536).value() == before);
}

TEST_CASE("legacy review exposes missing inspection without invented frame or storage limits",
          "[review]") {
    test::reconstruction_fixture fixture;
    fixture.adopt_inspection();
    const auto request = fixture.write_retained_request();
    auto config = nlohmann::json::parse(std::ifstream(request));
    config["schema_version"] = 1;
    config.erase("pose_action");
    config.erase("inspection");
    test::save_reconstruction_json(request, config);
    const auto summary = viewer::review_reconstruction_request(request);
    REQUIRE(summary);
    CHECK(row(*summary, "Inspection") == "Not selected (legacy request)");
    CHECK(row(*summary, "Minimum free space").empty());
    CHECK(row(*summary, "Selected frames").empty());
    CHECK_FALSE(std::filesystem::exists(fixture.job.scene_output));
}

TEST_CASE("invalidating an in-flight review cannot leave an eligible stale launch", "[review]") {
    test::reconstruction_fixture fixture;
    fixture.adopt_inspection();
    const auto request = fixture.write_retained_request();
    viewer::application_state state;
    state.review_request(request);
    state.clear_request_review(); // Safe both before and after the fast reader finishes.
    state.job.join();
    CHECK_FALSE(state.reviewed_request);
    CHECK_FALSE(state.busy);
    CHECK_FALSE(state.processing);
    const auto started = state.started;
    state.run_reviewed_request();
    CHECK(state.started == started);
    CHECK_FALSE(state.loaded);
    CHECK_FALSE(std::filesystem::exists(fixture.job.scene_output));
}
