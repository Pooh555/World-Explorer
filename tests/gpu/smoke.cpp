#include "world_explorer/application/scenes.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/render_vulkan/renderer.hpp"
#include "world_explorer/scene_store/scene_store.hpp"
#if defined(WE_VULKAN_TEST_FAULTS)
#include "renderer_faults.hpp"
#include "vulkan_swapchain.hpp"
#endif
#include <SDL3/SDL.h>
#include <fstream>
#include <iostream>
#include <set>

using namespace world_explorer;
namespace {
void require(bool condition, const std::string& message) {
    if (!condition)
        throw std::runtime_error(message);
}
scene_snapshot overlap_fixture() {
    auto scene = std::make_shared<scene_state>();
    scene->id = {19};
    scene->revision = {1};
    auto chunk = std::make_shared<scene_chunk>();
    chunk->id = {1};
    chunk->bounds = {{-1, -1, -2}, {1, 1, 1}};
    gaussian near, far;
    near.mean = {0, 0, 0};
    near.scale = {0.35F, 0.2F, 0.15F};
    near.opacity = 0.5F;
    far = near;
    far.mean[2] = -1;
    for (std::size_t channel = 0; channel < 3; ++channel) {
        near.sh[channel] = (channel == 0 ? 0.5F : -0.5F) / 0.2820947918F;
        far.sh[channel] = (channel == 2 ? 0.5F : -0.5F) / 0.2820947918F;
    }
    chunk->splats = {far, near}; // Intentionally opposite the required front-to-back order.
    scene->chunks.emplace(chunk->id, chunk);
    return scene;
}
bool wait_for_window_flag(SDL_Window* window, SDL_WindowFlags flag, bool enabled) {
    const auto deadline = SDL_GetTicks() + 2000;
    do {
        SDL_PumpEvents();
        if (((SDL_GetWindowFlags(window) & flag) != 0) == enabled)
            return true;
        SDL_Delay(10);
    } while (SDL_GetTicks() < deadline);
    return false;
}
bool check_minimize(vulkan_renderer& renderer, SDL_Window* window, const camera_view& camera,
                    const std::filesystem::path& output, bool required) {
    require(SDL_ShowWindow(window), "cannot show minimize fixture");
    require(wait_for_window_flag(window, SDL_WINDOW_HIDDEN, false),
            "native compositor did not show minimize fixture");
    const auto baseline_path = output.parent_path() / "minimize-baseline.ppm";
    require(renderer.request_screenshot(baseline_path).has_value(),
            "minimize baseline readback request failed");
    const auto shown = renderer.draw(camera);
    require(shown.has_value(), shown ? "" : shown.error().message);
    const auto baseline_hash = sha256_file(baseline_path, std::size_t{64} * 1024 * 1024);
    require(baseline_hash.has_value(), "cannot hash minimize baseline");
    require(SDL_MinimizeWindow(window), "native minimize request failed");
    // SyncWindow can return false while MINIMIZED is already reported. Check
    // the actual asynchronous window state used by the renderer.
    const bool observed = wait_for_window_flag(window, SDL_WINDOW_MINIMIZED, true);
    require(observed || !required, "native compositor did not accept required minimize");
    if (observed) {
        const auto before = renderer.diagnostics().submitted_frames;
        auto replacement = std::make_shared<scene_state>(*renderer.updates().visible_snapshot());
        replacement->revision = {2};
        auto batch = make_snapshot_batch(replacement, {2}).value();
        const auto ticket = renderer.updates().enqueue(batch);
        require(ticket.has_value(), "minimized update admission failed");
        for (int index = 0; index < 3; ++index)
            require(renderer.draw(camera).has_value(), "minimized draw failed");
        const auto progress = renderer.updates().query_progress(*ticket);
        require(renderer.diagnostics().submitted_frames == before && progress &&
                    progress->state == update_state::ACCEPTED &&
                    renderer.updates().visible_snapshot()->revision.value == 1,
                "minimized draw submitted work or advanced the queued scene");
        require(SDL_RestoreWindow(window), "native restore request failed");
        require(wait_for_window_flag(window, SDL_WINDOW_MINIMIZED, false) &&
                    wait_for_window_flag(window, SDL_WINDOW_HIDDEN, false),
                "native compositor did not restore the window");
        require(renderer.draw(camera).has_value(), "restored scene draw failed");
        const auto restored = renderer.updates().query_progress(*ticket);
        require(renderer.diagnostics().submitted_frames > before && restored &&
                    restored->state == update_state::RENDER_VISIBLE &&
                    renderer.updates().visible_snapshot()->revision.value == 2 &&
                    renderer.diagnostics().resident_splat_count == 2 &&
                    renderer.diagnostics().validation_errors == 0,
                "restore did not resume rendering and publish the queued scene");
        const auto restored_path = output.parent_path() / "minimize-restored.ppm";
        require(renderer.request_screenshot(restored_path).has_value(),
                "minimize restored readback request failed");
        require(renderer.draw(camera).has_value(), "restored readback draw failed");
        const auto restored_hash = sha256_file(restored_path, std::size_t{64} * 1024 * 1024);
        require(restored_hash && *restored_hash == *baseline_hash,
                "minimize/restore changed the overlap readback");
    }
    require(SDL_HideWindow(window), "cannot hide minimize fixture");
    return observed;
}
} // namespace
int main(int argc, char** argv) {
    try {
        bool timings = false, require_minimize = false;
        for (int index = 2; index < argc; ++index) {
            const auto option = std::string_view(argv[index]);
            if (option == "--timings")
                timings = true;
            else if (option == "--require-minimize")
                require_minimize = true;
            else
                require(false, "usage: we_gpu_smoke [output.ppm] [--timings] [--require-minimize]");
        }
        const auto output = argc > 1
                                ? std::filesystem::path(argv[1])
                                : std::filesystem::temp_directory_path() / "world_explorer_gpu.ppm";
        require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        struct sdl_guard {
            ~sdl_guard() { SDL_Quit(); }
        } sdl;
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("World Explorer GPU verification", 512, 512,
                             SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN),
            SDL_DestroyWindow);
        require(window != nullptr, SDL_GetError());
        renderer_options options;
        options.collect_timings = timings;
        options.max_splats = 1024;
        options.shader_directory = WE_SHADER_DIRECTORY;
        auto constrained = options;
        constrained.max_splats = 1;
        constrained.residency_bytes = std::size_t{1024} * 1024;
        const auto rejected = vulkan_renderer::create(window.get(), constrained);
        require(!rejected && rejected.error().message.find("buffer") != std::string::npos,
                "framebuffer allocation escaped the residency budget");
        auto created = vulkan_renderer::create(window.get(), options);
        require(created.has_value(), created ? "" : created.error().message);
        auto renderer = std::move(*created);
        require(renderer->capabilities().validation_enabled,
                "Validation layer unavailable: this is not a passing GPU test");
        require(renderer->capabilities().fp32_compositing &&
                    renderer->diagnostics().framebuffer_bytes > 0,
                "FP32 framebuffer capability/accounting missing");
        const auto initial = renderer->diagnostics();
        const auto pixel_bytes =
            static_cast<std::size_t>(initial.framebuffer_width) * initial.framebuffer_height * 16;
        require(initial.framebuffer_bytes >= 2 * pixel_bytes,
                "per-frame composition scratch was not charged");
        constrained = options;
        constrained.residency_bytes = initial.allocated_bytes - pixel_bytes / 2;
        renderer.reset();
        const auto scratch_rejected = vulkan_renderer::create(window.get(), constrained);
        require(!scratch_rejected &&
                    scratch_rejected.error().message.find("buffer") != std::string::npos,
                "composition scratch escaped the residency budget");
        constrained = options;
        constrained.residency_bytes = initial.allocated_bytes + std::size_t{8} * 1024 * 1024;
        created = vulkan_renderer::create(window.get(), constrained);
        require(created.has_value(), created ? "" : created.error().message);
        renderer = std::move(*created);
        auto retained = make_snapshot_batch(overlap_fixture(), {1}).value();
        require(renderer->updates().enqueue(retained).has_value(),
                "resize failure fixture admission failed");
        camera_view retained_camera;
        retained_camera.position = {0, 0, 4};
        const auto retained_draw = renderer->draw(retained_camera);
        require(retained_draw.has_value(), retained_draw ? "" : retained_draw.error().message);
        require(SDL_SetWindowSize(window.get(), 768, 768), SDL_GetError());
        require(SDL_SyncWindow(window.get()), SDL_GetError());
        const auto resize_rejected = renderer->draw(retained_camera);
        require(!resize_rejected &&
                    resize_rejected.error().message.find("budget") != std::string::npos,
                "staged resize allocation failure was not reported");
        require(SDL_SetWindowSize(window.get(), 512, 512), SDL_GetError());
        require(SDL_SyncWindow(window.get()), SDL_GetError());
        require(renderer->draw(retained_camera).has_value(),
                "previous swapchain did not render after rejected resize");
        require(renderer->updates().visible_snapshot()->revision.value == 1 &&
                    renderer->diagnostics().resident_splat_count == 2 &&
                    renderer->diagnostics().validation_errors == 0,
                "rejected resize changed scene revision, residency or Vulkan validity");
        renderer.reset();
#if defined(WE_VULKAN_TEST_FAULTS)
        created = vulkan_renderer::create(window.get(), options);
        require(created.has_value(), created ? "" : created.error().message);
        renderer = std::move(*created);
        auto recovery_scene = make_snapshot_batch(overlap_fixture(), {1}).value();
        require(renderer->updates().enqueue(recovery_scene).has_value(),
                "post-create failure fixture admission failed");
        require(renderer->draw(retained_camera).has_value(),
                "post-create failure fixture initial draw failed");
        require(SDL_SetWindowSize(window.get(), 640, 360), SDL_GetError());
        require(SDL_SyncWindow(window.get()), SDL_GetError());
        detail::fail_next_swapchain_after_create();
        const auto handoff_failed = renderer->draw(retained_camera);
        require(!handoff_failed && handoff_failed.error().message.find("injected post-create") !=
                                       std::string::npos,
                "post-create failure was not observed");
        require(renderer->overlay_context().image_count == 0 &&
                    renderer->diagnostics().framebuffer_width == 0 &&
                    renderer->updates().visible_snapshot()->revision.value == 1,
                "retired presentation remained exposed or scene revision changed");
        const auto recovered = renderer->draw(retained_camera);
        require(recovered.has_value(), recovered ? "" : recovered.error().message);
        require(renderer->diagnostics().framebuffer_width == 640 &&
                    renderer->diagnostics().framebuffer_height == 360 &&
                    renderer->diagnostics().resident_splat_count == 2 &&
                    renderer->diagnostics().validation_errors == 0,
                "post-create recovery did not render the retained scene cleanly");
        require(renderer->wait_for_idle().has_value(), "post-create recovery completion failed");
        const auto replay = renderer->updates().visible_snapshot();
        require(replay && replay->revision.value == 1,
                "device-loss fixture did not retain the visible revision");
        detail::fail_next_frame_with_device_loss();
        const auto lost = renderer->draw(retained_camera);
        require(!lost && lost.error().code == error_code::DEVICE_LOST,
                "injected Vulkan device loss was not typed");
        renderer.reset();
        created = vulkan_renderer::create(window.get(), options);
        require(created.has_value(), created ? "" : created.error().message);
        renderer = std::move(*created);
        auto replay_batch = make_snapshot_batch(replay, {2}).value();
        const auto replay_ticket = renderer->updates().enqueue(replay_batch);
        require(replay_ticket.has_value(), "device-loss scene replay admission failed");
        const auto recovery_readback = output.parent_path() / "device-recovery.ppm";
        require(renderer->request_screenshot(recovery_readback).has_value(),
                "device-loss recovery readback request failed");
        const auto replay_draw = renderer->draw(retained_camera);
        require(replay_draw.has_value(), replay_draw ? "" : replay_draw.error().message);
        const auto replay_progress = renderer->updates().query_progress(*replay_ticket);
        require(replay_progress && replay_progress->state == update_state::RENDER_VISIBLE &&
                    renderer->updates().visible_snapshot()->revision == replay->revision &&
                    renderer->diagnostics().resident_splat_count == 2 &&
                    renderer->diagnostics().validation_errors == 0,
                "device-loss recreation did not restore the exact scene revision");
        std::ifstream recovered_image(recovery_readback, std::ios::binary);
        std::string recovered_magic;
        unsigned recovered_width{}, recovered_height{}, recovered_maximum{};
        recovered_image >> recovered_magic >> recovered_width >> recovered_height >>
            recovered_maximum;
        require(recovered_image && recovered_magic == "P6" && recovered_width >= 256 &&
                    recovered_height >= 256 && recovered_maximum == 255,
                "device-loss recreation did not produce a valid readback");
        recovered_image.get();
        recovered_image.seekg(
            static_cast<std::streamoff>(
                (static_cast<std::size_t>(recovered_height / 2) * recovered_width +
                 recovered_width / 2) *
                3),
            std::ios::cur);
        std::array<unsigned char, 3> recovered_pixel{};
        recovered_image.read(reinterpret_cast<char*>(recovered_pixel.data()), 3);
        require(recovered_image && recovered_pixel == std::array<unsigned char, 3>{130, 3, 68},
                "device-loss recreation changed the overlap readback");
        renderer.reset();
        require(SDL_HideWindow(window.get()), SDL_GetError());
        require(SDL_SetWindowSize(window.get(), 512, 512), SDL_GetError());
#endif
        created = vulkan_renderer::create(window.get(), options);
        require(created.has_value(), created ? "" : created.error().message);
        renderer = std::move(*created);
        auto batch = make_snapshot_batch(overlap_fixture(), {1}).value();
        require(renderer->updates().enqueue(batch).has_value(), "initial scene admission failed");
        camera_view camera;
        camera.position = {0, 0, 4};
        std::set<std::uint64_t> paused_samples;
        for (int frame = 0; frame < 11; ++frame) {
            SDL_PumpEvents();
            if (frame == 9)
                require(renderer->request_screenshot(output).has_value(), "capture unsupported");
            const bool paused = frame >= 4 && frame <= 6;
            const auto drawn = renderer->draw(camera, !paused);
            require(drawn.has_value(), drawn ? "" : drawn.error().message);
            if (paused)
                require(renderer->diagnostics().resident_splat_count == 2,
                        "Paused appearance lost resident scene assets");
            if (timings) {
                const auto stats = renderer->diagnostics();
                require(stats.submitted_frames == static_cast<std::uint64_t>(frame) + 1,
                        "timed smoke draw did not submit its frame");
                for (const auto& sample : stats.completed_timings)
                    if (sample.sequence >= 5 && sample.sequence <= 7) {
                        require(sample.count_submissions == 0 && sample.tile_batches == 1 &&
                                    sample.gpu_ms[static_cast<std::size_t>(
                                        gpu_timing_stage::PROJECTION)] == 0 &&
                                    sample.gpu_ms[static_cast<std::size_t>(
                                        gpu_timing_stage::DEPTH_SORT)] == 0 &&
                                    sample.gpu_ms[static_cast<std::size_t>(
                                        gpu_timing_stage::COUNT_SCAN)] == 0,
                                "paused timing retained stale appearance work");
                        paused_samples.insert(sample.sequence);
                    }
            }
        }
        require(!timings || paused_samples.size() == 3,
                "paused completed timing samples were lost");
        std::ifstream image(output, std::ios::binary);
        std::string magic;
        unsigned width{}, height{}, maximum{};
        image >> magic >> width >> height >> maximum;
        image.get();
        require(magic == "P6" && width >= 256 && height >= 256 && maximum == 255,
                "Invalid screenshot");
        image.seekg(static_cast<std::streamoff>(
                        (static_cast<std::size_t>(height / 2) * width + width / 2) * 3),
                    std::ios::cur);
        std::array<unsigned char, 3> pixel{};
        image.read(reinterpret_cast<char*>(pixel.data()), 3);
        // Independent two-layer alpha result: near red .5, far blue .5 over background.
        const std::array<double, 3> expected{(0.5 + 0.25 * 0.045) * 255, 0.25 * 0.051 * 255,
                                             (0.25 + 0.25 * 0.063) * 255};
        for (std::size_t channel = 0; channel < 3; ++channel)
            require(std::abs(pixel[channel] - expected[channel]) <= 3,
                    "Two-splat compositing or depth sort mismatch");
        auto replacement = std::make_shared<scene_state>(*make_diagnostic_scene());
        replacement->id = {19};
        replacement->revision = {2};
        auto delta = make_snapshot_batch(replacement, {2}).value();
        delta.full_snapshot = false;
        delta.base_revision = {1};
        require(renderer->updates().enqueue(delta).has_value(), "replacement admission failed");
        bool resized = false, restored = false;
        for (int frame = 0; frame < 40; ++frame) {
            SDL_PumpEvents();
            if (frame == 8)
                SDL_SetWindowSize(window.get(), 640, 360);
            if (frame == 16)
                SDL_SetWindowSize(window.get(), 512, 512);
            if (frame == 24) {
                auto empty = std::make_shared<scene_state>();
                empty->id = {19};
                empty->revision = {3};
                auto deletion = make_snapshot_batch(empty, {3}).value();
                deletion.full_snapshot = false;
                deletion.base_revision = {2};
                require(renderer->updates().enqueue(deletion).has_value(),
                        "deletion admission failed");
            }
            const auto drawn = renderer->draw(camera);
            require(drawn.has_value(), drawn ? "" : drawn.error().message);
            const auto extent = renderer->diagnostics();
            if (frame >= 8 && frame < 16)
                resized = resized ||
                          (extent.framebuffer_width == 640 && extent.framebuffer_height == 360);
            if (frame >= 16)
                restored = restored ||
                           (extent.framebuffer_width == 512 && extent.framebuffer_height == 512);
        }
        require(resized && restored, "native framebuffer resize/restore was not observed");
        require(renderer->wait_for_idle().has_value(), "final GPU completion failed");
        const auto stats = renderer->diagnostics();
        if (timings)
            for (const auto& sample : stats.completed_timings)
                require(sample.sequence > 0 && sample.count_submissions == 0 &&
                            sample.tile_batches == 1 && sample.width == stats.framebuffer_width &&
                            sample.height == stats.framebuffer_height &&
                            sample.gpu_valid == renderer->capabilities().gpu_timestamps_supported,
                        "empty/resize timing was missing or retained stale counts");
        require(stats.allocated_bytes <= options.residency_bytes &&
                    stats.allocated_bytes >= stats.framebuffer_bytes + stats.scratch_bytes,
                "owned allocation accounting escaped its budget");
        require(stats.validation_errors == 0, "Vulkan validation reported errors");
        require(stats.resident_splat_count == 0 &&
                    renderer->updates().visible_snapshot()->revision.value == 3,
                "Deleted scene still visible");
        renderer.reset();
        constrained = std::move(options);
        constrained.max_tile_references = 1;
        created = vulkan_renderer::create(window.get(), constrained);
        require(created.has_value(), created ? "" : created.error().message);
        renderer = std::move(*created);
        auto overflowing = make_snapshot_batch(overlap_fixture(), {1}).value();
        require(renderer->updates().enqueue(overflowing).has_value(),
                "reference overflow fixture admission failed");
        const auto overflow = renderer->draw(camera);
        require(!overflow && overflow.error().message.find("one tile") != std::string::npos,
                "tile reference overflow was not rejected before emission");
        require(renderer->wait_for_idle().has_value(), "overflow GPU completion failed");
        require(renderer->diagnostics().validation_errors == 0,
                "reference overflow caused a Vulkan validation error");
        renderer.reset();
        // A shown window may be resized by a tiling compositor. Isolate this
        // lifecycle from the hidden-window resize and budget-rejection fixtures.
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> minimize_window(
            SDL_CreateWindow("World Explorer minimize verification", 512, 512,
                             SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN),
            SDL_DestroyWindow);
        require(minimize_window != nullptr, "cannot create minimize fixture window");
        renderer_options minimize_options;
        minimize_options.max_splats = 1024;
        minimize_options.collect_timings = timings;
        minimize_options.shader_directory = WE_SHADER_DIRECTORY;
        auto minimize_created = vulkan_renderer::create(minimize_window.get(), minimize_options);
        require(minimize_created.has_value(),
                minimize_created ? "" : minimize_created.error().message);
        auto minimize_renderer = std::move(*minimize_created);
        auto minimize_scene = make_snapshot_batch(overlap_fixture(), {1}).value();
        require(minimize_renderer->updates().enqueue(minimize_scene).has_value(),
                "minimize fixture admission failed");
        require(minimize_renderer->draw(retained_camera).has_value(),
                "minimize fixture initial draw failed");
        const bool minimized_observed = check_minimize(*minimize_renderer, minimize_window.get(),
                                                       retained_camera, output, require_minimize);
        const auto minimize_device = minimize_renderer->capabilities().device_name;
        minimize_renderer.reset(); // Destroy the Vulkan surface before its SDL window.
        std::cout << "GPU verification: overlap pixel=" << static_cast<int>(pixel[0]) << ','
                  << static_cast<int>(pixel[1]) << ',' << static_cast<int>(pixel[2])
                  << "; replace/delete/resize passed; validation_errors=0; frames="
                  << stats.submitted_frames << "; framebuffer_bytes=" << stats.framebuffer_bytes
                  << "; framebuffer, composition-scratch and tile-reference budgets rejected";
#if defined(WE_VULKAN_TEST_FAULTS)
        std::cout << "; post-create recovery passed";
#endif
        std::cout << "; minimize_device=" << minimize_device << "; minimize/restore "
                  << (minimized_observed ? "passed" : "denied by compositor") << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "GPU verification failed: " << error.what() << '\n';
        return 2;
    }
}
