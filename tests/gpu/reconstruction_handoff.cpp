#include "../ui/reconstruction_gate.hpp"
#include "job_state.hpp"
#include "world_explorer/application/reconstruction_job.hpp"
#include "world_explorer/application/scenes.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/render_vulkan/renderer.hpp"
#include <SDL3/SDL.h>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

using namespace world_explorer;
namespace {
void require(bool condition, const std::string& message) {
    if (!condition)
        throw std::runtime_error(message);
}
std::size_t splat_count(const scene_snapshot& scene) {
    std::size_t count{};
    for (const auto& [id, chunk] : scene->chunks) {
        (void)id;
        count += chunk->splats.size();
    }
    return count;
}
} // namespace
int main(int argc, char** argv) try {
    if (argc != 4) {
        std::cerr << "usage: we_gpu_reconstruction_handoff <prior scene> <completed request> "
                     "<new report directory>\n";
        return 2;
    }
    const std::filesystem::path output = argv[3];
    require(!std::filesystem::exists(output), "report directory already exists");
    const auto request = read_reconstruction_job(argv[2]);
    require(request.has_value(), request ? "" : request.error().message);
    require(request->action == fit_action::REUSE_COMPLETED &&
                std::filesystem::is_regular_file(request->pose_job / "result.json") &&
                std::filesystem::is_regular_file(request->fit_directory / "result.json"),
            "diagnostic requires completed poses/fit and explicit reuse; no solver/optimizer run");
    std::filesystem::create_directories(output);
    const auto prior = load_bundle(argv[1]);
    require(prior.has_value(), prior ? "" : prior.error().message);
    const auto source = source_view(**prior);
    require(source.has_value(), source ? "" : source.error().message);
    require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
    struct sdl_guard {
        ~sdl_guard() { SDL_Quit(); }
    } sdl;
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("World Explorer reconstruction handoff diagnostic", 1280, 720,
                         SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN),
        SDL_DestroyWindow);
    require(window != nullptr, SDL_GetError());
    renderer_options options;
    options.max_splats = 1000000;
    options.shader_directory = WE_SHADER_DIRECTORY;
    auto created = vulkan_renderer::create(window.get(), options);
    require(created.has_value(), created ? "" : created.error().message);
    auto renderer = std::move(*created);
    require(renderer->capabilities().validation_enabled, "native validation required");
    camera_view camera{source->camera.position, source->camera.right(), source->camera.down(),
                       source->camera.forward(), source->vertical_fov_radians};
    const auto draw = [&](bool appearance = true) {
        SDL_PumpEvents();
        const auto result = renderer->draw(camera, appearance);
        require(result.has_value(), result ? "" : result.error().message);
    };
    const auto capture = [&](const char* name, bool appearance = true) {
        const auto requested = renderer->request_screenshot(output / name);
        require(requested.has_value(), requested ? "" : requested.error().message);
        for (int frame = 0; frame < 3; ++frame)
            draw(appearance);
        const auto idle = renderer->wait_for_idle();
        require(idle.has_value(), idle ? "" : idle.error().message);
    };
    const auto admit = [&](const scene_snapshot& scene, operation_id operation) {
        auto batch = make_snapshot_batch(scene, operation);
        require(batch.has_value(), batch ? "" : batch.error().message);
        const auto ticket = renderer->updates().enqueue(*batch);
        require(ticket.has_value(), ticket ? "" : ticket.error().message);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        for (;;) {
            draw();
            const auto progress = renderer->updates().query_progress(*ticket);
            require(progress.has_value(), progress ? "" : progress.error().message);
            if (progress->state == update_state::RENDER_VISIBLE)
                break;
            require(std::chrono::steady_clock::now() < deadline, "visibility deadline exceeded");
        }
        require(renderer->diagnostics().resident_splat_count == splat_count(scene),
                "full scene must remain resident");
    };
    admit(*prior, {1});
    capture("prior.ppm");
    const auto prior_hash = sha256_file(output / "prior.ppm", std::size_t{16} * 1024 * 1024);
    require(prior_hash.has_value(), prior_hash ? "" : prior_hash.error().message);
    test::reconstruction_gate gate(reconstruction_stage::POSES);
    viewer::application_state state; // Cancels/joins before the observed gate is destroyed.
    state.load_limits.scene.max_splats = options.max_splats;
    state.reconstruction(argv[2],
                         [&](const reconstruction_progress& progress, const std::stop_token& stop) {
                             gate.observe(progress, stop);
                         });
    const auto gate_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    while (!gate.wait_until_reached(std::chrono::seconds(0))) {
        draw(!state.processing.load());
        require(state.busy, "job ended before selected pose-verification boundary");
        require(std::chrono::steady_clock::now() < gate_deadline, "stage deadline exceeded");
    }
    nlohmann::json report{{"scope", "native retained Cancel/reopen; not gate certification"},
                          {"device", renderer->capabilities().device_name},
                          {"resolution", {1280, 720}},
                          {"prior_scene_content", scene_content_hash(**prior)},
                          {"prior_splats", splat_count(*prior)}};
    {
        std::lock_guard lock(state.mutex);
        require(state.progress && state.progress->stage == reconstruction_stage::POSES,
                "checked stage must reach actual owned job state");
        require(state.processing && !state.loaded, "active job must pause before publication");
        report["processing_status"] = state.status;
    }
    capture("processing.ppm", false); // Renderer readback only; no desktop capture.
    require(renderer->diagnostics().resident_splat_count == splat_count(*prior),
            "pausing must retain previous GPU assets");
    state.request_cancel(); // The same command used by the GUI Cancel button.
    state.job.join();
    require(!gate.timed_out(), "gate must wake on stop, not timeout");
    require(!state.busy && !state.processing && !state.progress && !state.loaded,
            "cancelled verification must clear active state without publishing a snapshot");
    require(state.status.starts_with(reconstruction_stage_name(reconstruction_stage::POSES)) &&
                state.status.find("Cancelled") != std::string::npos,
            "cancellation must identify the actual stage");
    report["cancelled_status"] = state.status;
    capture("cancelled.ppm");
    const auto cancelled_hash =
        sha256_file(output / "cancelled.ppm", std::size_t{16} * 1024 * 1024);
    require(cancelled_hash.has_value(), cancelled_hash ? "" : cancelled_hash.error().message);
    require(*prior_hash == *cancelled_hash, "Cancel must preserve previous visible pixels");
    report["previous_pixels_sha256"] = *prior_hash;
    state.reconstruction(argv[2]);
    const auto reopen_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(180);
    while (state.busy) {
        draw(!state.processing.load());
        require(std::chrono::steady_clock::now() < reopen_deadline, "reopen deadline exceeded");
    }
    state.job.join();
    require(state.reconstruction_error.empty(), state.reconstruction_error);
    require(!state.progress && !state.processing && state.has_pending_work(),
            "completed reopen must await renderer admission");
    const auto started = state.started;
    state.reconstruction(argv[2]);
    require(state.started == started && !state.busy,
            "pending publication must reject a second job");
    scene_snapshot published;
    {
        std::lock_guard lock(state.mutex);
        report["pending_status"] = state.status;
        published = std::move(state.loaded);
    }
    require(published != nullptr, "checked publication must be available");
    require(!state.has_pending_work(), "UI consumption must clear pending state");
    const auto reopened_source = source_view(*published);
    require(reopened_source.has_value(), reopened_source ? "" : reopened_source.error().message);
    camera = {reopened_source->camera.position, reopened_source->camera.right(),
              reopened_source->camera.down(), reopened_source->camera.forward(),
              reopened_source->vertical_fov_radians};
    admit(published, {2});
    capture("reopened.ppm");
    report["reopened_scene_content"] = scene_content_hash(*published);
    report["reopened_splats"] = splat_count(published);
    const auto statistics = renderer->diagnostics();
    require(statistics.validation_errors == 0, "Vulkan validation errors");
    report["validation_errors"] = statistics.validation_errors;
    report["submitted_frames"] = statistics.submitted_frames;
    report["final_owned_buffer_bytes"] = statistics.allocated_bytes;
    report["framebuffer_bytes"] = statistics.framebuffer_bytes;
    std::ofstream(output / "report.json") << report.dump(2) << '\n';
    std::cout << report.dump() << '\n';
    return 0;
} catch (const std::exception& error) {
    std::cerr << "Handoff diagnostic failed: " << error.what() << '\n';
    return 2;
}
