#include "../unit/reconstruction_fixture.hpp"
#include "job_state.hpp"
#include "reconstruction_gate.hpp"
#include "world_explorer/application/scenes.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace world_explorer;
TEST_CASE("viewer completion waits for published snapshots after the loader clears busy") {
    viewer::application_state state;
    CHECK_FALSE(state.has_pending_work());
    state.busy = true;
    CHECK(state.has_pending_work());
    const auto scene = make_diagnostic_scene();
    {
        std::lock_guard lock(state.mutex);
        state.loaded = scene;
    }
    // Reproduce the handoff window: loading is complete, UI has not taken the snapshot.
    state.busy = false;
    CHECK(state.has_pending_work());
    scene_snapshot admitted;
    {
        std::lock_guard lock(state.mutex);
        admitted = std::move(state.loaded);
    }
    CHECK(admitted == scene);
    CHECK_FALSE(state.has_pending_work());
}

TEST_CASE("viewer synthetic labels follow current provenance instead of startup selection") {
    CHECK(!viewer::is_synthetic_scene({}));
    scene_snapshot current = make_diagnostic_scene();
    CHECK(viewer::is_synthetic_scene(current));
    auto replacement = std::make_shared<scene_state>(*current);
    replacement->provenance = {
        {"diagnostic", "synthetic asymmetric render reference v2; not a capture"}};
    current = replacement;
    CHECK(viewer::is_synthetic_scene(current));
    replacement = std::make_shared<scene_state>(*current);
    replacement->provenance = {{"source_kind", "capture"},
                               {"fixture", "synthetic_named_real_capture"}};
    current = replacement;
    CHECK(!viewer::is_synthetic_scene(current));
    replacement = std::make_shared<scene_state>(*current);
    replacement->provenance.clear();
    current = replacement;
    CHECK(!viewer::is_synthetic_scene(current));
    current = make_diagnostic_scene();
    CHECK(viewer::is_synthetic_scene(current));
}

TEST_CASE("viewer processing status uses typed progress and clears it at terminal handoff") {
    viewer::application_state state;
    state.busy = true;
    state.processing = true;
    state.reconstructing = true;
    state.update_reconstruction_progress({reconstruction_stage::FITTING, 42.655, 1101, 7000});
    CHECK(state.status.find("Fitting appearance: step 1101/7000") != std::string::npos);
    CHECK(state.status.find("ETA unknown") != std::string::npos);
    REQUIRE(state.progress);
    if (!state.progress)
        return;
    CHECK(state.progress.value().backend_elapsed_seconds == 42.655);
    CHECK(state.log.find("Backend elapsed 42.655") != std::string::npos);
    // A new native stage clears the fitter's previous step and backend timing.
    state.update_reconstruction_progress({reconstruction_stage::COMPILING, {}, {}, 0});
    REQUIRE(state.progress);
    if (!state.progress)
        return;
    CHECK_FALSE(state.progress.value().fit_step);
    CHECK_FALSE(state.progress.value().backend_elapsed_seconds);
    SECTION("successful publication waits for renderer admission") {
        const auto scene = make_diagnostic_scene();
        state.complete_reconstruction(scene, std::filesystem::path{"published.wexplore"});
        CHECK(state.loaded == scene);
        CHECK(state.has_pending_work());
        CHECK(state.status.find("waiting for renderer admission") != std::string::npos);
    }
    SECTION("cancellation identifies the stage and retains the previous scene") {
        state.complete_reconstruction(fail(error_code::CANCELLED, "Requested stop"), {});
        CHECK_FALSE(state.loaded);
        CHECK_FALSE(state.has_pending_work());
        CHECK(state.status.find("Compiling and publishing scene: Cancelled") != std::string::npos);
        CHECK_FALSE(state.reconstruction_error.empty());
    }
    SECTION("failure identifies the stage and offers diagnostics") {
        state.complete_reconstruction(fail(error_code::BACKEND_FAILURE, "Backend exited"), {});
        CHECK(state.status.find("Compiling and publishing scene: Failed") != std::string::npos);
        CHECK(state.reconstruction_error.find("Backend exited") != std::string::npos);
        CHECK_FALSE(state.has_pending_work());
    }
    SECTION("published scene with failed viewer load keeps its location") {
        state.update_reconstruction_progress({reconstruction_stage::LOADING_SCENE, {}, {}, 0});
        state.complete_reconstruction(fail(error_code::INVALID_DATA, "Bad asset"),
                                      std::filesystem::path{"published.wexplore"});
        CHECK(state.status.find("published.wexplore") != std::string::npos);
        CHECK(state.reconstruction_error.find("viewer load failed") != std::string::npos);
        CHECK_FALSE(state.has_pending_work());
    }
    CHECK_FALSE(state.busy);
    CHECK_FALSE(state.processing);
    CHECK_FALSE(state.reconstructing);
    CHECK_FALSE(state.progress);
    const auto terminal = state.status;
    state.update_reconstruction_progress({reconstruction_stage::FITTING, 99, 1201, 7000});
    CHECK(state.status == terminal);
    CHECK_FALSE(state.progress);
}

TEST_CASE("viewer owned reconstruction thread handles bad requests and resets previous progress") {
    viewer::application_state state;
    state.progress = reconstruction_progress{reconstruction_stage::FITTING, 42, 1101, 7000};
    state.reconstruction_error = "previous failure";
    // A missing file must fail on the actual owned job thread without any backend launch.
    const auto missing =
        std::filesystem::temp_directory_path() /
        ("we_missing_request_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    state.reconstruction(missing);
    state.job.join();
    CHECK_FALSE(state.busy);
    CHECK_FALSE(state.processing);
    CHECK_FALSE(state.progress);
    CHECK_FALSE(state.has_pending_work());
    CHECK(state.reconstruction_error != "previous failure");
    CHECK(state.status.find("step 1101") == std::string::npos);
    CHECK(state.status.find("Validating reconstruction request: Failed") != std::string::npos);
}

TEST_CASE("viewer owned job cancels at checked stages and reopens retained publication",
          "[owned-job]") {
    test::reconstruction_fixture fixture;
    fixture.adopt_inspection();
    REQUIRE(reconstruct(fixture.job, {}, {}));
    const auto manifest = fixture.job.scene_output / "manifest.json";
    const auto before = sha256_file(manifest, 65536).value();
    const auto request = fixture.write_retained_request();
    const auto stage = GENERATE(
        reconstruction_stage::VALIDATING, reconstruction_stage::INSPECTING,
        reconstruction_stage::POSES, reconstruction_stage::MAP, reconstruction_stage::VERIFYING_FIT,
        reconstruction_stage::COMPILING, reconstruction_stage::LOADING_SCENE);
    test::reconstruction_gate gate(stage); // Outlives the job, including failing assertions.
    viewer::application_state state;
    std::atomic<bool> stop_observed{};
    state.reconstruction(request,
                         [&](const reconstruction_progress& value, const std::stop_token& stop) {
                             std::stop_callback observe_stop(stop, [&] {
                                 // UI cancellation must permit stop callbacks to access the state
                                 // mutex.
                                 std::lock_guard lock(state.mutex);
                                 stop_observed = true;
                             });
                             gate.observe(value, stop);
                         });
    REQUIRE(gate.wait_until_reached());
    {
        std::lock_guard lock(state.mutex);
        REQUIRE(state.progress);
        if (state.progress)
            CHECK(state.progress->stage == stage);
        CHECK(state.busy);
        CHECK(state.processing);
        CHECK_FALSE(state.loaded);
    }
    const auto started = state.started;
    state.load(fixture.root / "missing-scene");
    state.worker({}, {});
    state.reconstruction(fixture.root / "missing-request");
    CHECK(state.started == started);
    state.request_cancel();
    state.request_cancel(); // Repeated Cancel is harmless.
    state.job.join();
    CHECK(stop_observed);
    CHECK_FALSE(gate.timed_out());
    CHECK_FALSE(state.busy);
    CHECK_FALSE(state.processing);
    CHECK_FALSE(state.progress);
    CHECK_FALSE(state.reconstructing);
    CHECK(sha256_file(manifest, 65536).value() == before);
    if (stage == reconstruction_stage::LOADING_SCENE) {
        // Atomic publication cannot be undone by a later stop.
        REQUIRE(state.loaded);
        CHECK(state.reconstruction_error.empty());
        CHECK(state.has_pending_work());
        std::lock_guard lock(state.mutex);
        state.loaded.reset(); // Model renderer admission before opening a new job.
    } else {
        CHECK_FALSE(state.loaded);
        CHECK_FALSE(state.has_pending_work());
        CHECK(state.status.starts_with(reconstruction_stage_name(stage)));
        CHECK(state.status.find("Cancelled") != std::string::npos);
        CHECK_FALSE(state.reconstruction_error.empty());
    }
    bool stale_stop = false;
    state.reconstruction(request, [&](const reconstruction_progress&, const std::stop_token& stop) {
        stale_stop = stale_stop || stop.stop_requested();
    });
    state.job.join();
    CHECK_FALSE(stale_stop);
    CHECK_FALSE(state.busy);
    CHECK_FALSE(state.processing);
    CHECK_FALSE(state.progress);
    CHECK(state.reconstruction_error.empty());
    REQUIRE(state.loaded);
    CHECK(state.has_pending_work());
    CHECK(sha256_file(manifest, 65536).value() == before);
    CHECK(state.status.find("waiting for renderer admission") != std::string::npos);
}

TEST_CASE("viewer refuses new jobs until a completed snapshot is admitted", "[owned-job]") {
    test::reconstruction_fixture fixture;
    fixture.adopt_inspection();
    const auto request = fixture.write_retained_request();
    viewer::application_state state;
    state.reconstruction(request);
    state.job.join();
    REQUIRE(state.loaded);
    CHECK_FALSE(state.busy);
    const auto pending = state.loaded;
    const auto status = state.status;
    const auto started = state.started;
    SECTION("load cannot replace pending status") { state.load(fixture.root / "missing-scene"); }
    SECTION("worker cannot replace pending status") { state.worker({}, {}); }
    SECTION("reconstruction cannot replace pending status") {
        state.reconstruction(fixture.root / "missing-request");
    }
    if (state.job.joinable())
        state.job.join();
    CHECK(state.loaded == pending);
    CHECK(state.started == started);
    CHECK(state.status == status);
    CHECK(state.reconstruction_error.empty());
    CHECK_FALSE(state.busy);
    CHECK_FALSE(state.processing);
    CHECK(state.has_pending_work());
}
