#include "../ui/reconstruction_gate.hpp"
#include "job_state.hpp"
#include "world_explorer/application/reconstruction_job.hpp"
#include "world_explorer/application/scenes.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/render_vulkan/renderer.hpp"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>

using namespace world_explorer;

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::size_t splat_count(const SceneSnapshot& scene) {
    std::size_t count {};
    for (const auto& [K_ID, K_CHUNK] : scene->m_chunks) {
        (void)K_ID;
        count += K_CHUNK->m_splats.size();
    }
    return count;
}

}  // namespace
int main(int argc, char** argv) try {
    if (argc != 4) {
        std::cerr << "usage: we_gpu_reconstruction_handoff <prior scene> <completed request> "
                     "<new report directory>\n";
        return 2;
    }
    const std::filesystem::path K_OUTPUT = argv[3];
    require(!std::filesystem::exists(K_OUTPUT), "report directory already exists");
    const auto K_REQUEST = read_reconstruction_job(argv[2]);
    require(K_REQUEST.has_value(), K_REQUEST ? "" : K_REQUEST.error().m_message);
    require(K_REQUEST->m_action == FitAction::REUSE_COMPLETED &&
                std::filesystem::is_regular_file(K_REQUEST->m_pose_job / "result.json") &&
                std::filesystem::is_regular_file(K_REQUEST->m_fit_directory / "result.json"),
            "diagnostic requires completed poses/fit and explicit reuse; no solver/optimizer run");
    std::filesystem::create_directories(K_OUTPUT);
    const auto K_PRIOR = load_bundle(argv[1]);
    require(K_PRIOR.has_value(), K_PRIOR ? "" : K_PRIOR.error().m_message);
    const auto K_SOURCE = source_view(**K_PRIOR);
    require(K_SOURCE.has_value(), K_SOURCE ? "" : K_SOURCE.error().m_message);
    require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());

    struct SdlGuard {
        ~SdlGuard() {
            SDL_Quit();
        }
    } sdl;

    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("World Explorer reconstruction handoff diagnostic",
                         1280,
                         720,
                         SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN),
        SDL_DestroyWindow);
    require(window != nullptr, SDL_GetError());
    RendererOptions options;
    options.m_max_splats = 1000000;
    options.m_shader_directory = WE_SHADER_DIRECTORY;
    auto created = VulkanRenderer::create(window.get(), options);
    require(created.has_value(), created ? "" : created.error().m_message);
    auto renderer = std::move(*created);
    require(renderer->capabilities().m_validation_enabled, "native validation required");
    CameraView camera { K_SOURCE->m_camera.m_position,
                       K_SOURCE->m_camera.right(),
                       K_SOURCE->m_camera.down(),
                       K_SOURCE->m_camera.forward(),
                       K_SOURCE->m_vertical_fov_radians };
    const auto K_DRAW = [&](bool appearance = true) {
        SDL_PumpEvents();
        const auto K_RESULT = renderer->draw(camera, appearance);
        require(K_RESULT.has_value(), K_RESULT ? "" : K_RESULT.error().m_message);
    };
    const auto K_CAPTURE = [&](const char* name, bool appearance = true) {
        const auto K_REQUESTED = renderer->request_screenshot(K_OUTPUT / name);
        require(K_REQUESTED.has_value(), K_REQUESTED ? "" : K_REQUESTED.error().m_message);
        for (int frame { 0 }; frame < 3; ++frame) {
            K_DRAW(appearance);
        }
        const auto K_IDLE = renderer->wait_for_idle();
        require(K_IDLE.has_value(), K_IDLE ? "" : K_IDLE.error().m_message);
    };
    const auto K_ADMIT = [&](const SceneSnapshot& scene, OperationId operation) {
        auto batch = make_snapshot_batch(scene, operation);
        require(batch.has_value(), batch ? "" : batch.error().m_message);
        const auto K_TICKET = renderer->updates().enqueue(*batch);
        require(K_TICKET.has_value(), K_TICKET ? "" : K_TICKET.error().m_message);
        const auto K_DEADLINE = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        for (;;) {
            K_DRAW();
            const auto K_PROGRESS = renderer->updates().query_progress(*K_TICKET);
            require(K_PROGRESS.has_value(), K_PROGRESS ? "" : K_PROGRESS.error().m_message);
            if (K_PROGRESS->m_state == UpdateState::RENDER_VISIBLE) {
                break;
            }
            require(std::chrono::steady_clock::now() < K_DEADLINE, "visibility deadline exceeded");
        }
        require(renderer->diagnostics().m_resident_splat_count == splat_count(scene),
                "full scene must remain resident");
    };
    K_ADMIT(*K_PRIOR, { 1 });
    K_CAPTURE("prior.ppm");
    const auto K_PRIOR_HASH = sha256_file(K_OUTPUT / "prior.ppm", 16uz * 1024 * 1024);
    require(K_PRIOR_HASH.has_value(), K_PRIOR_HASH ? "" : K_PRIOR_HASH.error().m_message);
    test::reconstruction_gate gate(ReconstructionStage::POSES);
    viewer::application_state state;  // Cancels/joins before the observed gate is destroyed.
    state.load_limits.m_scene.m_max_splats = options.m_max_splats;
    state.review_request(argv[2]);
    const auto K_REVIEW_DEADLINE = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (state.busy) {
        require(!state.processing, "request review must keep appearance active");
        K_DRAW();
        require(std::chrono::steady_clock::now() < K_REVIEW_DEADLINE, "review deadline exceeded");
    }
    state.job.join();
    require(state.reviewed_request && !state.loaded, "read-only review must not publish a scene");
    state.reconstruction(argv[2],
                         [&](const ReconstructionProgress& progress, const std::stop_token& stop) {
                             gate.observe(progress, stop);
                         });
    const auto K_GATE_DEADLINE = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    while (!gate.wait_until_reached(std::chrono::seconds(0))) {
        K_DRAW(!state.processing.load());
        require(state.busy, "job ended before selected pose-verification boundary");
        require(std::chrono::steady_clock::now() < K_GATE_DEADLINE, "stage deadline exceeded");
    }
    nlohmann::json report { { "scope", "native retained Cancel/reopen; not gate certification" },
                           { "device", renderer->capabilities().m_device_name },
                           { "resolution", { 1280, 720 } },
                           { "prior_scene_content", scene_content_hash(**K_PRIOR) },
                           { "prior_splats", splat_count(*K_PRIOR) } };
    {
        std::lock_guard lock(state.mutex);
        require(state.progress && state.progress->m_stage == ReconstructionStage::POSES,
                "checked stage must reach actual owned job state");
        require(state.processing && !state.loaded, "active job must pause before publication");
        report["processing_status"] = state.status;
    }
    K_CAPTURE("processing.ppm", false);  // Renderer readback only; no desktop capture.
    require(renderer->diagnostics().m_resident_splat_count == splat_count(*K_PRIOR),
            "pausing must retain previous GPU assets");
    state.request_cancel();  // The same command used by the GUI Cancel button.
    state.job.join();
    require(!gate.timed_out(), "gate must wake on stop, not timeout");
    require(!state.busy && !state.processing && !state.progress && !state.loaded,
            "cancelled verification must clear active state without publishing a snapshot");
    require(state.status.starts_with(reconstruction_stage_name(ReconstructionStage::POSES)) &&
                state.status.find("Cancelled") != std::string::npos,
            "cancellation must identify the actual stage");
    require(state.reviewed_request != nullptr, "review must survive cancellation");
    report["reviewed_request_sha256"] = state.reviewed_request->request_sha256;
    report["cancelled_status"] = state.status;
    K_CAPTURE("cancelled.ppm");
    const auto K_CANCELLED_HASH = sha256_file(K_OUTPUT / "cancelled.ppm", 16uz * 1024 * 1024);
    require(K_CANCELLED_HASH.has_value(),
            K_CANCELLED_HASH ? "" : K_CANCELLED_HASH.error().m_message);
    require(*K_PRIOR_HASH == *K_CANCELLED_HASH, "Cancel must preserve previous visible pixels");
    report["previous_pixels_sha256"] = *K_PRIOR_HASH;
    state.run_reviewed_request();
    const auto K_REOPEN_DEADLINE = std::chrono::steady_clock::now() + std::chrono::seconds(180);
    while (state.busy) {
        K_DRAW(!state.processing.load());
        require(std::chrono::steady_clock::now() < K_REOPEN_DEADLINE, "reopen deadline exceeded");
    }
    state.job.join();
    require(state.reconstruction_error.empty(), state.reconstruction_error);
    require(!state.progress && !state.processing && state.has_pending_work(),
            "completed reopen must await renderer admission");
    const auto K_STARTED = state.started;
    state.reconstruction(argv[2]);
    require(state.started == K_STARTED && !state.busy,
            "pending publication must reject a second job");
    SceneSnapshot published;
    {
        std::lock_guard lock(state.mutex);
        report["pending_status"] = state.status;
        published = std::move(state.loaded);
    }
    require(published != nullptr, "checked publication must be available");
    require(!state.has_pending_work(), "UI consumption must clear pending state");
    const auto K_REOPENED_SOURCE = source_view(*published);
    require(K_REOPENED_SOURCE.has_value(),
            K_REOPENED_SOURCE ? "" : K_REOPENED_SOURCE.error().m_message);
    camera = { K_REOPENED_SOURCE->m_camera.m_position,
              K_REOPENED_SOURCE->m_camera.right(),
              K_REOPENED_SOURCE->m_camera.down(),
              K_REOPENED_SOURCE->m_camera.forward(),
              K_REOPENED_SOURCE->m_vertical_fov_radians };
    K_ADMIT(published, { 2 });
    K_CAPTURE("reopened.ppm");
    report["reopened_scene_content"] = scene_content_hash(*published);
    report["reopened_splats"] = splat_count(published);
    const auto K_STATISTICS = renderer->diagnostics();
    require(K_STATISTICS.m_validation_errors == 0, "Vulkan validation errors");
    report["validation_errors"] = K_STATISTICS.m_validation_errors;
    report["submitted_frames"] = K_STATISTICS.m_submitted_frames;
    report["final_owned_buffer_bytes"] = K_STATISTICS.m_allocated_bytes;
    report["framebuffer_bytes"] = K_STATISTICS.m_framebuffer_bytes;
    std::ofstream(K_OUTPUT / "report.json") << report.dump(2) << '\n';
    std::cout << report.dump() << '\n';
    return 0;
} catch (const std::exception& error) {
    std::cerr << "Handoff diagnostic failed: " << error.what() << '\n';
    return 2;
}
