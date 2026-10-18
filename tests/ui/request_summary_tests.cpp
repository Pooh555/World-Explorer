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
            if (field.label == label) return field.value;
    return {};
}

}  // namespace

TEST_CASE("review distinguishes retained compatibility limits from new optimizer work",
          "[review]") {
    test::reconstruction_fixture fixture;
    fixture.adopt_inspection();
    const auto K_REQUEST = fixture.write_retained_request();
    auto config = nlohmann::json::parse(std::ifstream(K_REQUEST));
    const auto K_ACTION = GENERATE("reuse_completed", "start", "resume");
    config["action"] = K_ACTION;
    if (std::string_view(K_ACTION) != "reuse_completed") {
        config.erase("completed_fit_identity");
        config["fit_directory"] = test::path_utf8(fixture.root / "new fit");
        config["gsplat_source"] = test::path_utf8(fixture.root / "backend source");
        config["fit_worker_script"] = test::path_utf8(fixture.root / "worker.py");
        config["fit_python"] = WE_TEST_PYTHON;
        if (std::string_view(K_ACTION) == "resume") {
            std::ofstream(fixture.root / "checkpoint.pt") << "synthetic optimizer bytes";
            config["checkpoint"] = "checkpoint.pt";
        }
    }
    test::save_reconstruction_json(K_REQUEST, config);
    const auto K_SUMMARY = viewer::review_reconstruction_request(K_REQUEST);
    INFO((K_SUMMARY ? "checked synthetic request" : K_SUMMARY.error().m_message));
    REQUIRE(K_SUMMARY);
    CHECK(K_SUMMARY->request == std::filesystem::canonical(K_REQUEST));
    CHECK(K_SUMMARY->request_sha256 == sha256_file(K_REQUEST, 65536).value());
    CHECK(row(*K_SUMMARY, "Video") == test::path_utf8(fixture.job.m_video));
    CHECK(row(*K_SUMMARY, "Pose action") == "Adopt completed poses");
    CHECK(row(*K_SUMMARY, "Minimum free space").starts_with("1 bytes"));
    CHECK(row(*K_SUMMARY, "Checked video") ==
          "synthetic_diagnostic; 2 x 1; 1.000 s; " +
              std::to_string(std::filesystem::file_size(fixture.job.m_video)) + " source bytes");
    CHECK(row(*K_SUMMARY, "Source timing") ==
          "Original time base 1/30; display rotation -90 degrees");
    CHECK(row(*K_SUMMARY, "Appearance and scale").find("arbitrary scale") != std::string::npos);
    CHECK(row(*K_SUMMARY, "Current output storage")
              .find(test::path_utf8(fixture.job.m_scene_output)) != std::string::npos);
    CHECK(
        row(*K_SUMMARY, "Selected frames").empty());  // No guessed counts/rate for retained poses.
    if (std::string_view(K_ACTION) == "reuse_completed") {
        CHECK(row(*K_SUMMARY, "Appearance action").find("optimization skipped") !=
              std::string::npos);
        CHECK(row(*K_SUMMARY, "Fit steps") == "7000 (fit compatibility)");
        CHECK(row(*K_SUMMARY, "Fit Python").empty());
    } else {
        CHECK(row(*K_SUMMARY, "Fit steps") == "7000");
        CHECK(row(*K_SUMMARY, "Fit Python") == WE_TEST_PYTHON);
        CHECK(row(*K_SUMMARY, "Appearance action").find("skipped") == std::string::npos);
    }
    if (std::string_view(K_ACTION) == "resume")
        CHECK(row(*K_SUMMARY, "Checkpoint") == test::path_utf8(fixture.root / "checkpoint.pt"));
    CHECK_FALSE(std::filesystem::exists(fixture.job.m_scene_output));
    CHECK_FALSE(std::filesystem::exists(fixture.root / "new fit"));
}

TEST_CASE("review shows fresh frame pose storage and resource choices without running workers",
          "[review]") {
    test::reconstruction_fixture fixture;
    fixture.fresh("failure", true);
    // Start from a retained request, then explicitly select the fresh pipeline.
    const auto K_REQUEST = fixture.write_retained_request();
    auto config = nlohmann::json::parse(std::ifstream(K_REQUEST));
    config["pose_action"] = "solve";
    REQUIRE(fixture.job.m_fresh_poses);
    if (!fixture.job.m_fresh_poses) return;
    const auto& pose = *fixture.job.m_fresh_poses;
    config["pose_worker"] = { { "worker_script", test::path_utf8(pose.m_worker.m_script) },
                             { "worker_python", WE_TEST_PYTHON },
                             { "pose_python", WE_TEST_PYTHON },
                             { "recipe", "exhaustive_guided" },
                             { "cpu_threads", 2 },
                             { "journal_bytes", 67108864 } };
    REQUIRE(fixture.job.m_automatic_decode);
    if (!fixture.job.m_automatic_decode) return;
    const auto& decode = *fixture.job.m_automatic_decode;
    config["decode"] = { { "action", "decode" },
                        { "job_directory", test::path_utf8(decode.m_job_directory) },
                        { "worker_script", test::path_utf8(decode.m_worker.m_script) },
                        { "python", WE_TEST_PYTHON },
                        { "sample_fps", 1.75 },
                        { "max_frames", 40 },
                        { "cpu_threads", 2 },
                        { "max_output_bytes", 1073741824 } };
    config["steps"] = 30000;
    config["max_image_dimension"] = 1024;
    config["max_splats"] = 100000;
    test::save_reconstruction_json(K_REQUEST, config);
    const auto K_SUMMARY = viewer::review_reconstruction_request(K_REQUEST);
    INFO((K_SUMMARY ? "checked synthetic fresh configuration" : K_SUMMARY.error().m_message));
    REQUIRE(K_SUMMARY);
    CHECK(row(*K_SUMMARY, "Pose action").starts_with("Solve selected frames"));
    CHECK(row(*K_SUMMARY, "Selected frames") ==
          "Timestamp sampling: 1.75 frames/s, at most 40 frames");
    CHECK(row(*K_SUMMARY, "Decoded output cap") == "1073741824 bytes");
    CHECK(row(*K_SUMMARY, "Pose matching") == "Exhaustive guided");
    CHECK(row(*K_SUMMARY, "Pose CPU threads") == "2");
    CHECK(row(*K_SUMMARY, "RGB journal cap") == "67108864 bytes");
    CHECK(row(*K_SUMMARY, "Fit steps").starts_with("30000"));
    CHECK(row(*K_SUMMARY, "Training image cap").starts_with("1024 pixels"));
    CHECK(row(*K_SUMMARY, "Transient splat cap").starts_with("100000 splats"));
    CHECK(row(*K_SUMMARY, "Current output storage").find(test::path_utf8(decode.m_job_directory)) !=
          std::string::npos);
    CHECK(row(*K_SUMMARY, "Current output storage").find(test::path_utf8(fixture.job.m_pose_job)) !=
          std::string::npos);
    CHECK_FALSE(std::filesystem::exists(fixture.job.m_pose_job));
    CHECK_FALSE(std::filesystem::exists(decode.m_job_directory));
    CHECK_FALSE(std::filesystem::exists(fixture.job.m_scene_output));
}

TEST_CASE("review rejects malformed oversized and cancelled requests without writes", "[review]") {
    test::reconstruction_fixture fixture;
    fixture.adopt_inspection();
    const auto K_REQUEST = fixture.write_retained_request();
    SECTION("invalid action") {
        auto config = nlohmann::json::parse(std::ifstream(K_REQUEST));
        config["action"] = "restart";
        test::save_reconstruction_json(K_REQUEST, config);
    }
    SECTION("invalid resource cap") {
        auto config = nlohmann::json::parse(std::ifstream(K_REQUEST));
        config["max_splats"] = 2000001;
        test::save_reconstruction_json(K_REQUEST, config);
    }
    SECTION("malformed JSON") {
        std::ofstream(K_REQUEST) << "{invalid";
    }
    SECTION("too large") {
        std::ofstream(K_REQUEST) << std::string(65537, ' ');
    }
    SECTION("missing") {
        std::filesystem::remove(K_REQUEST);
    }
    SECTION("directory") {
        std::filesystem::remove(K_REQUEST);
        std::filesystem::create_directory(K_REQUEST);
    }
    SECTION("explicit stop") {
        std::stop_source cancellation;
        cancellation.request_stop();
        const auto K_RESULT =
            viewer::review_reconstruction_request(K_REQUEST, cancellation.get_token());
        REQUIRE_FALSE(K_RESULT);
        CHECK(K_RESULT.error().m_code == ErrorCode::CANCELLED);
        return;
    }
    CHECK_FALSE(viewer::review_reconstruction_request(K_REQUEST));
    CHECK_FALSE(std::filesystem::exists(fixture.job.m_scene_output));
}

TEST_CASE("owned request review preserves the scene and rejects changed content at launch",
          "[review]") {
    test::reconstruction_fixture fixture;
    fixture.adopt_inspection();
    REQUIRE(reconstruct(fixture.job, {}, {}));
    const auto K_REQUEST = fixture.write_retained_request();
    const auto K_BEFORE = sha256_file(fixture.job.m_scene_output / "manifest.json", 65536).value();
    viewer::application_state state;
    state.review_request(K_REQUEST);
    CHECK_FALSE(
        state.processing);  // Reviewing is not reconstruction and must not pause appearance.
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
        auto config = nlohmann::json::parse(std::ifstream(K_REQUEST));
        config["scene_output"] = test::path_utf8(fixture.root / "different scene.wexplore");
        test::save_reconstruction_json(K_REQUEST, config);
        state.run_reviewed_request();
        state.job.join();
        CHECK_FALSE(state.loaded);
        CHECK_FALSE(state.reviewed_request);
        CHECK(state.review_error.find("Review again") != std::string::npos);
        CHECK(state.reconstruction_error.find("checksum") != std::string::npos);
        CHECK_FALSE(std::filesystem::exists(fixture.root / "different scene.wexplore"));
    }
    SECTION("deleted reviewed file cannot launch") {
        std::filesystem::remove(K_REQUEST);
        state.run_reviewed_request();
        state.job.join();
        CHECK_FALSE(state.loaded);
        CHECK_FALSE(state.reviewed_request);
        CHECK(state.review_error.find("Review again") != std::string::npos);
        CHECK(state.reconstruction_error.find("path changed") != std::string::npos);
    }
    SECTION("source changes after review are rejected by running preflight") {
        const auto K_BYTES = std::filesystem::file_size(fixture.job.m_video);
        std::ofstream(fixture.job.m_video) << std::string(K_BYTES, 'x');
        state.run_reviewed_request();
        state.job.join();
        CHECK_FALSE(state.loaded);
        CHECK(state.reconstruction_error.find("different video") != std::string::npos);
    }
    SECTION("field editing clears eligibility to run") {
        state.clear_request_review();
        state.run_reviewed_request();
        CHECK_FALSE(state.busy);
        CHECK_FALSE(state.reviewed_request);
        CHECK_FALSE(state.loaded);
    }
    CHECK(sha256_file(fixture.job.m_scene_output / "manifest.json", 65536).value() == K_BEFORE);
}

TEST_CASE("legacy review exposes missing inspection without invented frame or storage limits",
          "[review]") {
    test::reconstruction_fixture fixture;
    fixture.adopt_inspection();
    const auto K_REQUEST = fixture.write_retained_request();
    auto config = nlohmann::json::parse(std::ifstream(K_REQUEST));
    config["schema_version"] = 1;
    config.erase("pose_action");
    config.erase("inspection");
    test::save_reconstruction_json(K_REQUEST, config);
    const auto K_SUMMARY = viewer::review_reconstruction_request(K_REQUEST);
    REQUIRE(K_SUMMARY);
    CHECK(row(*K_SUMMARY, "Inspection") == "Not selected (legacy request)");
    CHECK(row(*K_SUMMARY, "Minimum free space").empty());
    CHECK(row(*K_SUMMARY, "Selected frames").empty());
    CHECK(row(*K_SUMMARY, "Checked video").empty());
    CHECK(row(*K_SUMMARY, "Current output storage").empty());
    CHECK_FALSE(std::filesystem::exists(fixture.job.m_scene_output));
}

TEST_CASE("review checks retained source metadata and storage before enabling a job", "[review]") {
    test::reconstruction_fixture fixture;
    fixture.adopt_inspection();
    const auto K_REQUEST = fixture.write_retained_request();
    const auto K_COMPLETION =
        sha256_file(fixture.inspection().m_stage / "completion.json", 65536).value();
    const auto K_FIT = sha256_file(fixture.job.m_fit_directory / "result.json", 65536).value();
    SECTION("changed same-size source") {
        const auto K_BYTES = std::filesystem::file_size(fixture.job.m_video);
        std::ofstream(fixture.job.m_video) << std::string(K_BYTES, 'x');
    }
    SECTION("changed metadata bytes") {
        std::ofstream(fixture.inspection().m_stage / "result.json", std::ios::app) << " ";
    }
    SECTION("missing selected metadata") {
        std::filesystem::remove(fixture.inspection().m_stage / "result.json");
    }
    SECTION("output volume is a file") {
        std::ofstream(fixture.job.m_scene_output) << "preserve";
    }
    SECTION("stop before metadata verification") {
        std::stop_source cancellation;
        cancellation.request_stop();
        const auto K_SUMMARY =
            viewer::review_reconstruction_request(K_REQUEST, cancellation.get_token());
        REQUIRE_FALSE(K_SUMMARY);
        CHECK(K_SUMMARY.error().m_code == ErrorCode::CANCELLED);
        return;
    }
    const auto K_SUMMARY = viewer::review_reconstruction_request(K_REQUEST);
    INFO((K_SUMMARY ? "unexpected eligible review" : K_SUMMARY.error().m_message));
    CHECK_FALSE(K_SUMMARY);
    CHECK(sha256_file(fixture.inspection().m_stage / "completion.json", 65536).value() ==
          K_COMPLETION);
    CHECK(sha256_file(fixture.job.m_fit_directory / "result.json", 65536).value() == K_FIT);
    CHECK_FALSE(std::filesystem::exists(fixture.root / "fit.map"));
}

TEST_CASE("automatic inspection review checks storage while leaving metadata pending", "[review]") {
    test::reconstruction_fixture fixture;
    fixture.fresh("failure", true);
    const auto K_REQUEST = fixture.write_retained_request();
    auto config = nlohmann::json::parse(std::ifstream(K_REQUEST));
    REQUIRE(fixture.job.m_automatic_decode);
    REQUIRE(fixture.job.m_fresh_poses);
    if (!fixture.job.m_automatic_decode || !fixture.job.m_fresh_poses) return;
    const auto K_INSPECT_DIRECTORY = fixture.root / "new inspection";
    config["inspection"] = { { "action", "inspect" },
                            { "job_directory", test::path_utf8(K_INSPECT_DIRECTORY) },
                            { "worker_script", test::path_utf8(fixture.root / "unused worker.py") },
                            { "python", WE_TEST_PYTHON },
                            { "minimum_free_bytes", 1 } };
    config["pose_action"] = "solve";
    config["pose_worker"] = {
        { "worker_script", test::path_utf8(fixture.job.m_fresh_poses->m_worker.m_script) },
        { "worker_python", WE_TEST_PYTHON },
        { "pose_python", WE_TEST_PYTHON },
        { "recipe", "sequential" },
        { "cpu_threads", 2 },
        { "journal_bytes", 67108864 } };
    config["decode"] = {
        { "action", "decode" },
        { "job_directory", test::path_utf8(fixture.job.m_automatic_decode->m_job_directory) },
        { "worker_script", test::path_utf8(fixture.job.m_automatic_decode->m_worker.m_script) },
        { "python", WE_TEST_PYTHON },
        { "sample_fps", 3.0 },
        { "max_frames", 120 },
        { "cpu_threads", 2 },
        { "max_output_bytes", 1073741824 } };
    config["action"] = "resume";
    config.erase("completed_fit_identity");
    config["fit_directory"] = test::path_utf8(fixture.root / "new fit");
    config["gsplat_source"] = test::path_utf8(fixture.root / "backend source");
    config["fit_worker_script"] = test::path_utf8(fixture.root / "unused fit.py");
    config["fit_python"] = WE_TEST_PYTHON;
    std::ofstream(fixture.root / "checkpoint.pt") << "synthetic checkpoint";
    config["checkpoint"] = "checkpoint.pt";
    test::save_reconstruction_json(K_REQUEST, config);
    SECTION("stage volume is a file") {
        std::ofstream(K_INSPECT_DIRECTORY) << "preserve";
        CHECK_FALSE(viewer::review_reconstruction_request(K_REQUEST));
    }
    SECTION("fully populated request remains within presentation budget") {
        const auto K_SUMMARY = viewer::review_reconstruction_request(K_REQUEST);
        INFO((K_SUMMARY ? "checked" : K_SUMMARY.error().m_message));
        REQUIRE(K_SUMMARY);
        CHECK(K_SUMMARY->rows.size() + K_SUMMARY->advanced.size() <= 32);
        CHECK(row(*K_SUMMARY, "Checked video").empty());
        CHECK(
            row(*K_SUMMARY, "Current output storage").find(test::path_utf8(K_INSPECT_DIRECTORY)) !=
            std::string::npos);
        CHECK_FALSE(std::filesystem::exists(K_INSPECT_DIRECTORY));
    }
    SECTION("retained metadata and resumed fresh poses fit the full row budget") {
        config["inspection"] = { { "action", "adopt_completed" },
                                { "stage", test::path_utf8(fixture.inspection().m_stage) },
                                { "completion_sha256", fixture.inspection().m_completion_sha256 },
                                { "minimum_free_bytes", 1 } };
        test::save_reconstruction_json(K_REQUEST, config);
        const auto K_SUMMARY = viewer::review_reconstruction_request(K_REQUEST);
        INFO((K_SUMMARY ? "checked" : K_SUMMARY.error().m_message));
        REQUIRE(K_SUMMARY);
        CHECK(K_SUMMARY->rows.size() + K_SUMMARY->advanced.size() == 32);
        CHECK_FALSE(row(*K_SUMMARY, "Checked video").empty());
    }
    CHECK_FALSE(std::filesystem::exists(fixture.root / "new fit"));
    CHECK_FALSE(std::filesystem::exists(fixture.job.m_pose_job));
    CHECK_FALSE(std::filesystem::exists(fixture.job.m_automatic_decode->m_job_directory));
}

TEST_CASE("invalidating an in-flight review cannot leave an eligible stale launch", "[review]") {
    test::reconstruction_fixture fixture;
    fixture.adopt_inspection();
    const auto K_REQUEST = fixture.write_retained_request();
    viewer::application_state state;
    state.review_request(K_REQUEST);
    state.clear_request_review();  // Safe both before and after the fast reader finishes.
    state.job.join();
    CHECK_FALSE(state.reviewed_request);
    CHECK_FALSE(state.busy);
    CHECK_FALSE(state.processing);
    const auto K_STARTED = state.started;
    state.run_reviewed_request();
    CHECK(state.started == K_STARTED);
    CHECK_FALSE(state.loaded);
    CHECK_FALSE(std::filesystem::exists(fixture.job.m_scene_output));
}
