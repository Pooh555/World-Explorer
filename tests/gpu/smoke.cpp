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
    if (!condition) {
        throw std::runtime_error(message);
    }
}

SceneSnapshot overlap_fixture() {
    auto scene = std::make_shared<SceneState>();
    scene->m_id = { 19 };
    scene->m_revision = { 1 };
    auto chunk = std::make_shared<SceneChunk>();
    chunk->m_id = { 1 };
    chunk->m_bounds = { { -1, -1, -2 }, { 1, 1, 1 } };
    Gaussian near, far;
    near.m_mean = { 0, 0, 0 };
    near.m_scale = { 0.35F, 0.2F, 0.15F };
    near.m_opacity = 0.5F;
    far = near;
    far.m_mean[2] = -1;
    for (std::size_t channel { 0uz }; channel < 3uz; ++channel) {
        near.m_sh[channel] = (channel == 0 ? 0.5F : -0.5F) / 0.2820947918F;
        far.m_sh[channel] = (channel == 2 ? 0.5F : -0.5F) / 0.2820947918F;
    }
    chunk->m_splats = { far, near };  // Intentionally opposite the required front-to-back order.
    scene->m_chunks.emplace(chunk->m_id, chunk);
    return scene;
}
#if defined(WE_VULKAN_TEST_FAULTS)
void verify_acquire_deferral(SDL_Window* window,
                             const RendererOptions& options,
                             const std::filesystem::path& output) {
    auto created = VulkanRenderer::create(window, options);
    require(created.has_value(), created ? "" : created.error().m_message);
    auto renderer = std::move(*created);
    CameraView camera;
    camera.m_position = { 0, 0, 4 };
    auto initial = make_snapshot_batch(overlap_fixture(), { 1 }).value();
    require(renderer->updates().enqueue(initial).has_value(), "acquire fixture admission failed");
    require(renderer->draw(camera).has_value(), "acquire fixture initial draw failed");
    const auto K_BASELINE_PATH = output.parent_path() / "acquire-baseline.ppm";
    require(renderer->request_screenshot(K_BASELINE_PATH).has_value(),
            "acquire baseline request failed");
    require(renderer->draw(camera).has_value(), "acquire baseline capture failed");
    const auto K_BASELINE = sha256_file(K_BASELINE_PATH, 4uz * 1024 * 1024);
    require(K_BASELINE.has_value(), "acquire baseline missing");
    const auto K_BEFORE = renderer->diagnostics().m_submitted_frames;
    auto replacement = std::make_shared<SceneState>(*renderer->updates().visible_snapshot());
    replacement->m_revision = { 2 };
    auto batch = make_snapshot_batch(replacement, { 2 }).value();
    const auto K_TICKET = renderer->updates().enqueue(batch);
    require(K_TICKET.has_value(), "deferred-acquire update admission failed");
    const auto K_PENDING_PATH = output.parent_path() / "acquire-recovered.ppm";
    require(!std::filesystem::exists(K_PENDING_PATH), "deferred-acquire capture must be new");
    require(renderer->request_screenshot(K_PENDING_PATH).has_value(),
            "deferred capture request failed");
    for (const auto K_DELAY : { detail::frame_acquire_delay::TIMEOUT,
                               detail::frame_acquire_delay::NOT_READY,
                               detail::frame_acquire_delay::TIMEOUT,
                               detail::frame_acquire_delay::NOT_READY }) {
        detail::defer_next_frame_acquisition(K_DELAY);
        const auto K_DEFERRED = renderer->draw(camera);
        require(K_DEFERRED.has_value(), K_DEFERRED ? "" : K_DEFERRED.error().m_message);
        const auto K_PROGRESS = renderer->updates().query_progress(*K_TICKET);
        require(K_PROGRESS && K_PROGRESS->m_state == UpdateState::ACCEPTED &&
                    renderer->updates().visible_snapshot()->m_revision.m_value == 1 &&
                    renderer->diagnostics().m_submitted_frames == K_BEFORE &&
                    !std::filesystem::exists(K_PENDING_PATH),
                "deferred acquisition consumed update, frame or screenshot");
    }
    for (int frame { 0 }; frame < 120 && !std::filesystem::exists(K_PENDING_PATH); ++frame) {
        SDL_PumpEvents();
        require(renderer->draw(camera).has_value(), "deferred acquisition did not resume");
    }
    const auto K_PROGRESS = renderer->updates().query_progress(*K_TICKET);
    const auto K_RECOVERED = sha256_file(K_PENDING_PATH, 4uz * 1024 * 1024);
    require(K_PROGRESS && K_PROGRESS->m_state == UpdateState::RENDER_VISIBLE && K_RECOVERED &&
                *K_RECOVERED == *K_BASELINE && renderer->diagnostics().m_validation_errors == 0,
            "deferred acquisition lost update, pixels or semaphore validity");
    std::cout << "Acquire retry: 4 deferred draws preserve update/readback; exact recovery; "
                 "validation_errors=0\n";
}
#endif
bool wait_for_window_flag(SDL_Window* window, SDL_WindowFlags flag, bool enabled) {
    const auto K_DEADLINE = SDL_GetTicks() + 2000;
    do {
        SDL_PumpEvents();
        if (((SDL_GetWindowFlags(window) & flag) != 0) == enabled) {
            return true;
        }
        SDL_Delay(10);
    } while (SDL_GetTicks() < K_DEADLINE);
    return false;
}

bool check_minimize(VulkanRenderer& renderer,
                    SDL_Window* window,
                    const CameraView& camera,
                    const std::filesystem::path& output,
                    bool required) {
    require(SDL_ShowWindow(window), "cannot show minimize fixture");
    require(wait_for_window_flag(window, SDL_WINDOW_HIDDEN, false),
            "native compositor did not show minimize fixture");
    const auto K_BASELINE_PATH = output.parent_path() / "minimize-baseline.ppm";
    require(renderer.request_screenshot(K_BASELINE_PATH).has_value(),
            "minimize baseline readback request failed");
    const auto K_SHOWN = renderer.draw(camera);
    require(K_SHOWN.has_value(), K_SHOWN ? "" : K_SHOWN.error().m_message);
    const auto K_BASELINE_HASH = sha256_file(K_BASELINE_PATH, 64uz * 1024 * 1024);
    require(K_BASELINE_HASH.has_value(), "cannot hash minimize baseline");
    require(SDL_MinimizeWindow(window), "native minimize request failed");
    // SyncWindow can return false while MINIMIZED is already reported. Check
    // the actual asynchronous window state used by the renderer.
    const bool K_OBSERVED = wait_for_window_flag(window, SDL_WINDOW_MINIMIZED, true);
    require(K_OBSERVED || !required, "native compositor did not accept required minimize");
    if (K_OBSERVED) {
        const auto K_BEFORE = renderer.diagnostics().m_submitted_frames;
        auto replacement = std::make_shared<SceneState>(*renderer.updates().visible_snapshot());
        replacement->m_revision = { 2 };
        auto batch = make_snapshot_batch(replacement, { 2 }).value();
        const auto K_TICKET = renderer.updates().enqueue(batch);
        require(K_TICKET.has_value(), "minimized update admission failed");
        for (int index { 0 }; index < 3; ++index) {
            require(renderer.draw(camera).has_value(), "minimized draw failed");
        }
        const auto K_PROGRESS = renderer.updates().query_progress(*K_TICKET);
        require(renderer.diagnostics().m_submitted_frames == K_BEFORE && K_PROGRESS &&
                    K_PROGRESS->m_state == UpdateState::ACCEPTED &&
                    renderer.updates().visible_snapshot()->m_revision.m_value == 1,
                "minimized draw submitted work or advanced the queued scene");
        require(SDL_RestoreWindow(window), "native restore request failed");
        require(wait_for_window_flag(window, SDL_WINDOW_MINIMIZED, false) &&
                    wait_for_window_flag(window, SDL_WINDOW_HIDDEN, false),
                "native compositor did not restore the window");
        require(renderer.draw(camera).has_value(), "restored scene draw failed");
        const auto K_RESTORED = renderer.updates().query_progress(*K_TICKET);
        require(renderer.diagnostics().m_submitted_frames > K_BEFORE && K_RESTORED &&
                    K_RESTORED->m_state == UpdateState::RENDER_VISIBLE &&
                    renderer.updates().visible_snapshot()->m_revision.m_value == 2 &&
                    renderer.diagnostics().m_resident_splat_count == 2 &&
                    renderer.diagnostics().m_validation_errors == 0,
                "restore did not resume rendering and publish the queued scene");
        const auto K_RESTORED_PATH = output.parent_path() / "minimize-restored.ppm";
        require(renderer.request_screenshot(K_RESTORED_PATH).has_value(),
                "minimize restored readback request failed");
        require(renderer.draw(camera).has_value(), "restored readback draw failed");
        const auto K_RESTORED_HASH = sha256_file(K_RESTORED_PATH, 64uz * 1024 * 1024);
        require(K_RESTORED_HASH && *K_RESTORED_HASH == *K_BASELINE_HASH,
                "minimize/restore changed the overlap readback");
    }
    require(SDL_HideWindow(window), "cannot hide minimize fixture");
    return K_OBSERVED;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        bool timings = false, require_minimize = false;
        for (int index { 2 }; index < argc; ++index) {
            const auto K_OPTION = std::string_view(argv[index]);
            if (K_OPTION == "--timings") {
                timings = true;
            } else if (K_OPTION == "--require-minimize") {
                require_minimize = true;
            } else {
                require(false, "usage: we_gpu_smoke [output.ppm] [--timings] [--require-minimize]");
            }
        }
        const auto K_OUTPUT =
            argc > 1 ? std::filesystem::path(argv[1])
                     : std::filesystem::temp_directory_path() / "world_explorer_gpu.ppm";
        require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());

        struct SdlGuard {
            ~SdlGuard() {
                SDL_Quit();
            }
        } sdl;

        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("World Explorer GPU verification",
                             512,
                             512,
                             SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN),
            SDL_DestroyWindow);
        require(window != nullptr, SDL_GetError());
        RendererOptions options;
        options.m_collect_timings = timings;
        options.m_max_splats = 1024;
        options.m_shader_directory = WE_SHADER_DIRECTORY;
        auto constrained = options;
        constrained.m_max_splats = 1;
        constrained.m_residency_bytes = 1024uz * 1024;
        const auto K_REJECTED = VulkanRenderer::create(window.get(), constrained);
        require(!K_REJECTED && K_REJECTED.error().m_message.find("buffer") != std::string::npos,
                "framebuffer allocation escaped the residency budget");
        auto created = VulkanRenderer::create(window.get(), options);
        require(created.has_value(), created ? "" : created.error().m_message);
        auto renderer = std::move(*created);
        require(renderer->capabilities().m_validation_enabled,
                "Validation layer unavailable: this is not a passing GPU test");
        require(renderer->capabilities().m_fp32_compositing &&
                    renderer->diagnostics().m_framebuffer_bytes > 0,
                "FP32 framebuffer capability/accounting missing");
        const auto K_INITIAL = renderer->diagnostics();
        const auto K_PIXEL_BYTES = static_cast<std::size_t>(K_INITIAL.m_framebuffer_width) *
                                   K_INITIAL.m_framebuffer_height * 16;
        require(K_INITIAL.m_framebuffer_bytes >= 2 * K_PIXEL_BYTES,
                "per-frame composition scratch was not charged");
        constrained = options;
        constrained.m_residency_bytes = K_INITIAL.m_allocated_bytes - K_PIXEL_BYTES / 2;
        renderer.reset();
        const auto K_SCRATCH_REJECTED = VulkanRenderer::create(window.get(), constrained);
        require(!K_SCRATCH_REJECTED &&
                    K_SCRATCH_REJECTED.error().m_message.find("buffer") != std::string::npos,
                "composition scratch escaped the residency budget");
        constrained = options;
        constrained.m_residency_bytes = K_INITIAL.m_allocated_bytes + 8uz * 1024 * 1024;
        created = VulkanRenderer::create(window.get(), constrained);
        require(created.has_value(), created ? "" : created.error().m_message);
        renderer = std::move(*created);
        auto retained = make_snapshot_batch(overlap_fixture(), { 1 }).value();
        require(renderer->updates().enqueue(retained).has_value(),
                "resize failure fixture admission failed");
        CameraView retained_camera;
        retained_camera.m_position = { 0, 0, 4 };
        const auto K_RETAINED_DRAW = renderer->draw(retained_camera);
        require(K_RETAINED_DRAW.has_value(),
                K_RETAINED_DRAW ? "" : K_RETAINED_DRAW.error().m_message);
        require(SDL_SetWindowSize(window.get(), 768, 768), SDL_GetError());
        require(SDL_SyncWindow(window.get()), SDL_GetError());
        const auto K_RESIZE_REJECTED = renderer->draw(retained_camera);
        require(!K_RESIZE_REJECTED &&
                    K_RESIZE_REJECTED.error().m_message.find("budget") != std::string::npos,
                "staged resize allocation failure was not reported");
        require(SDL_SetWindowSize(window.get(), 512, 512), SDL_GetError());
        require(SDL_SyncWindow(window.get()), SDL_GetError());
        require(renderer->draw(retained_camera).has_value(),
                "previous swapchain did not render after rejected resize");
        require(renderer->updates().visible_snapshot()->m_revision.m_value == 1 &&
                    renderer->diagnostics().m_resident_splat_count == 2 &&
                    renderer->diagnostics().m_validation_errors == 0,
                "rejected resize changed scene revision, residency or Vulkan validity");
        renderer.reset();
#if defined(WE_VULKAN_TEST_FAULTS)
        verify_acquire_deferral(window.get(), options, K_OUTPUT);
        created = VulkanRenderer::create(window.get(), options);
        require(created.has_value(), created ? "" : created.error().m_message);
        renderer = std::move(*created);
        auto recovery_scene = make_snapshot_batch(overlap_fixture(), { 1 }).value();
        require(renderer->updates().enqueue(recovery_scene).has_value(),
                "post-create failure fixture admission failed");
        require(renderer->draw(retained_camera).has_value(),
                "post-create failure fixture initial draw failed");
        require(SDL_SetWindowSize(window.get(), 640, 360), SDL_GetError());
        require(SDL_SyncWindow(window.get()), SDL_GetError());
        detail::fail_next_swapchain_after_create();
        const auto K_HANDOFF_FAILED = renderer->draw(retained_camera);
        require(!K_HANDOFF_FAILED && K_HANDOFF_FAILED.error().m_message.find(
                                         "injected post-create") != std::string::npos,
                "post-create failure was not observed");
        require(renderer->overlay_context().m_image_count == 0 &&
                    renderer->diagnostics().m_framebuffer_width == 0 &&
                    renderer->updates().visible_snapshot()->m_revision.m_value == 1,
                "retired presentation remained exposed or scene revision changed");
        const auto K_RECOVERED = renderer->draw(retained_camera);
        require(K_RECOVERED.has_value(), K_RECOVERED ? "" : K_RECOVERED.error().m_message);
        require(renderer->diagnostics().m_framebuffer_width == 640 &&
                    renderer->diagnostics().m_framebuffer_height == 360 &&
                    renderer->diagnostics().m_resident_splat_count == 2 &&
                    renderer->diagnostics().m_validation_errors == 0,
                "post-create recovery did not render the retained scene cleanly");
        require(renderer->wait_for_idle().has_value(), "post-create recovery completion failed");
        const auto K_REPLAY = renderer->updates().visible_snapshot();
        require(K_REPLAY && K_REPLAY->m_revision.m_value == 1,
                "device-loss fixture did not retain the visible revision");
        detail::fail_next_frame_with_device_loss();
        const auto K_LOST = renderer->draw(retained_camera);
        require(!K_LOST && K_LOST.error().m_code == ErrorCode::DEVICE_LOST,
                "injected Vulkan device loss was not typed");
        renderer.reset();
        created = VulkanRenderer::create(window.get(), options);
        require(created.has_value(), created ? "" : created.error().m_message);
        renderer = std::move(*created);
        auto replay_batch = make_snapshot_batch(K_REPLAY, { 2 }).value();
        const auto K_REPLAY_TICKET = renderer->updates().enqueue(replay_batch);
        require(K_REPLAY_TICKET.has_value(), "device-loss scene replay admission failed");
        const auto K_RECOVERY_READBACK = K_OUTPUT.parent_path() / "device-recovery.ppm";
        require(renderer->request_screenshot(K_RECOVERY_READBACK).has_value(),
                "device-loss recovery readback request failed");
        const auto K_REPLAY_DRAW = renderer->draw(retained_camera);
        require(K_REPLAY_DRAW.has_value(), K_REPLAY_DRAW ? "" : K_REPLAY_DRAW.error().m_message);
        const auto K_REPLAY_PROGRESS = renderer->updates().query_progress(*K_REPLAY_TICKET);
        require(K_REPLAY_PROGRESS && K_REPLAY_PROGRESS->m_state == UpdateState::RENDER_VISIBLE &&
                    renderer->updates().visible_snapshot()->m_revision == K_REPLAY->m_revision &&
                    renderer->diagnostics().m_resident_splat_count == 2 &&
                    renderer->diagnostics().m_validation_errors == 0,
                "device-loss recreation did not restore the exact scene revision");
        std::ifstream recovered_image(K_RECOVERY_READBACK, std::ios::binary);
        std::string recovered_magic;
        unsigned recovered_width {}, recovered_height {}, recovered_maximum {};
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
        std::array<unsigned char, 3> recovered_pixel {};
        recovered_image.read(reinterpret_cast<char*>(recovered_pixel.data()), 3);
        require(recovered_image && recovered_pixel == std::array<unsigned char, 3> { 130, 3, 68 },
                "device-loss recreation changed the overlap readback");
        renderer.reset();
        require(SDL_HideWindow(window.get()), SDL_GetError());
        require(SDL_SetWindowSize(window.get(), 512, 512), SDL_GetError());
#endif
        created = VulkanRenderer::create(window.get(), options);
        require(created.has_value(), created ? "" : created.error().m_message);
        renderer = std::move(*created);
        auto batch = make_snapshot_batch(overlap_fixture(), { 1 }).value();
        require(renderer->updates().enqueue(batch).has_value(), "initial scene admission failed");
        CameraView camera;
        camera.m_position = { 0, 0, 4 };
        std::set<std::uint64_t> paused_samples;
        for (int frame { 0 }; frame < 11; ++frame) {
            SDL_PumpEvents();
            if (frame == 9) {
                require(renderer->request_screenshot(K_OUTPUT).has_value(), "capture unsupported");
            }
            const bool K_PAUSED = frame >= 4 && frame <= 6;
            const auto K_DRAWN = renderer->draw(camera, !K_PAUSED);
            require(K_DRAWN.has_value(), K_DRAWN ? "" : K_DRAWN.error().m_message);
            if (K_PAUSED) {
                require(renderer->diagnostics().m_resident_splat_count == 2,
                        "Paused appearance lost resident scene assets");
            }
            if (timings) {
                const auto K_STATS = renderer->diagnostics();
                require(K_STATS.m_submitted_frames == static_cast<std::uint64_t>(frame) + 1,
                        "timed smoke draw did not submit its frame");
                for (const auto& sample : K_STATS.m_completed_timings) {
                    if (sample.m_sequence >= 5 && sample.m_sequence <= 7) {
                        require(sample.m_count_submissions == 0 && sample.m_tile_batches == 1 &&
                                    sample.m_gpu_ms[static_cast<std::size_t>(
                                        GpuTimingStage::PROJECTION)] == 0 &&
                                    sample.m_gpu_ms[static_cast<std::size_t>(
                                        GpuTimingStage::DEPTH_SORT)] == 0 &&
                                    sample.m_gpu_ms[static_cast<std::size_t>(
                                        GpuTimingStage::COUNT_SCAN)] == 0,
                                "paused timing retained stale appearance work");
                        paused_samples.insert(sample.m_sequence);
                    }
                }
            }
        }
        require(!timings || paused_samples.size() == 3,
                "paused completed timing samples were lost");
        std::ifstream image(K_OUTPUT, std::ios::binary);
        std::string magic;
        unsigned width {}, height {}, maximum {};
        image >> magic >> width >> height >> maximum;
        image.get();
        require(magic == "P6" && width >= 256 && height >= 256 && maximum == 255,
                "Invalid screenshot");
        image.seekg(static_cast<std::streamoff>(
                        (static_cast<std::size_t>(height / 2) * width + width / 2) * 3),
                    std::ios::cur);
        std::array<unsigned char, 3> pixel {};
        image.read(reinterpret_cast<char*>(pixel.data()), 3);
        // Independent two-layer alpha result: near red .5, far blue .5 over background.
        const std::array<double, 3> K_EXPECTED {
            (0.5 + 0.25 * 0.045) * 255, 0.25 * 0.051 * 255, (0.25 + 0.25 * 0.063) * 255 };
        for (std::size_t channel { 0uz }; channel < 3uz; ++channel) {
            require(std::abs(pixel[channel] - K_EXPECTED[channel]) <= 3,
                    "Two-splat compositing or depth sort mismatch");
        }
        auto replacement = std::make_shared<SceneState>(*make_diagnostic_scene());
        replacement->m_id = { 19 };
        replacement->m_revision = { 2 };
        auto delta = make_snapshot_batch(replacement, { 2 }).value();
        delta.m_full_snapshot = false;
        delta.m_base_revision = { 1 };
        require(renderer->updates().enqueue(delta).has_value(), "replacement admission failed");
        bool resized = false, restored = false;
        for (int frame { 0 }; frame < 40; ++frame) {
            SDL_PumpEvents();
            if (frame == 8) {
                SDL_SetWindowSize(window.get(), 640, 360);
            }
            if (frame == 16) {
                SDL_SetWindowSize(window.get(), 512, 512);
            }
            if (frame == 24) {
                auto empty = std::make_shared<SceneState>();
                empty->m_id = { 19 };
                empty->m_revision = { 3 };
                auto deletion = make_snapshot_batch(empty, { 3 }).value();
                deletion.m_full_snapshot = false;
                deletion.m_base_revision = { 2 };
                require(renderer->updates().enqueue(deletion).has_value(),
                        "deletion admission failed");
            }
            const auto K_DRAWN = renderer->draw(camera);
            require(K_DRAWN.has_value(), K_DRAWN ? "" : K_DRAWN.error().m_message);
            const auto K_EXTENT = renderer->diagnostics();
            if (frame >= 8 && frame < 16) {
                resized = resized || (K_EXTENT.m_framebuffer_width == 640 &&
                                      K_EXTENT.m_framebuffer_height == 360);
            }
            if (frame >= 16) {
                restored = restored || (K_EXTENT.m_framebuffer_width == 512 &&
                                        K_EXTENT.m_framebuffer_height == 512);
            }
        }
        require(resized && restored, "native framebuffer resize/restore was not observed");
        require(renderer->wait_for_idle().has_value(), "final GPU completion failed");
        const auto K_STATS = renderer->diagnostics();
        if (timings) {
            for (const auto& sample : K_STATS.m_completed_timings) {
                require(sample.m_sequence > 0 && sample.m_count_submissions == 0 &&
                            sample.m_tile_batches == 1 &&
                            sample.m_width == K_STATS.m_framebuffer_width &&
                            sample.m_height == K_STATS.m_framebuffer_height &&
                            sample.m_gpu_valid ==
                                renderer->capabilities().m_gpu_timestamps_supported,
                        "empty/resize timing was missing or retained stale counts");
            }
        }
        require(K_STATS.m_allocated_bytes <= options.m_residency_bytes &&
                    K_STATS.m_allocated_bytes >=
                        K_STATS.m_framebuffer_bytes + K_STATS.m_scratch_bytes,
                "owned allocation accounting escaped its budget");
        require(K_STATS.m_validation_errors == 0, "Vulkan validation reported errors");
        require(K_STATS.m_resident_splat_count == 0 &&
                    renderer->updates().visible_snapshot()->m_revision.m_value == 3,
                "Deleted scene still visible");
        renderer.reset();
        constrained = std::move(options);
        constrained.m_max_tile_references = 1;
        created = VulkanRenderer::create(window.get(), constrained);
        require(created.has_value(), created ? "" : created.error().m_message);
        renderer = std::move(*created);
        auto overflowing = make_snapshot_batch(overlap_fixture(), { 1 }).value();
        require(renderer->updates().enqueue(overflowing).has_value(),
                "reference overflow fixture admission failed");
        const auto K_OVERFLOW = renderer->draw(camera);
        require(!K_OVERFLOW && K_OVERFLOW.error().m_message.find("one tile") != std::string::npos,
                "tile reference overflow was not rejected before emission");
        require(renderer->wait_for_idle().has_value(), "overflow GPU completion failed");
        require(renderer->diagnostics().m_validation_errors == 0,
                "reference overflow caused a Vulkan validation error");
        renderer.reset();
        // A shown window may be resized by a tiling compositor. Isolate this
        // lifecycle from the hidden-window resize and budget-rejection fixtures.
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> minimize_window(
            SDL_CreateWindow("World Explorer minimize verification",
                             512,
                             512,
                             SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN),
            SDL_DestroyWindow);
        require(minimize_window != nullptr, "cannot create minimize fixture window");
        RendererOptions minimize_options;
        minimize_options.m_max_splats = 1024;
        minimize_options.m_collect_timings = timings;
        minimize_options.m_shader_directory = WE_SHADER_DIRECTORY;
        auto minimize_created = VulkanRenderer::create(minimize_window.get(), minimize_options);
        require(minimize_created.has_value(),
                minimize_created ? "" : minimize_created.error().m_message);
        auto minimize_renderer = std::move(*minimize_created);
        auto minimize_scene = make_snapshot_batch(overlap_fixture(), { 1 }).value();
        require(minimize_renderer->updates().enqueue(minimize_scene).has_value(),
                "minimize fixture admission failed");
        require(minimize_renderer->draw(retained_camera).has_value(),
                "minimize fixture initial draw failed");
        const bool K_MINIMIZED_OBSERVED = check_minimize(
            *minimize_renderer, minimize_window.get(), retained_camera, K_OUTPUT, require_minimize);
        const auto K_MINIMIZE_DEVICE = minimize_renderer->capabilities().m_device_name;
        minimize_renderer.reset();  // Destroy the Vulkan surface before its SDL window.
        std::cout << "GPU verification: overlap pixel=" << static_cast<int>(pixel[0]) << ','
                  << static_cast<int>(pixel[1]) << ',' << static_cast<int>(pixel[2])
                  << "; replace/delete/resize passed; validation_errors=0; frames="
                  << K_STATS.m_submitted_frames
                  << "; framebuffer_bytes=" << K_STATS.m_framebuffer_bytes
                  << "; framebuffer, composition-scratch and tile-reference budgets rejected";
#if defined(WE_VULKAN_TEST_FAULTS)
        std::cout << "; post-create recovery passed";
#endif
        std::cout << "; minimize_device=" << K_MINIMIZE_DEVICE << "; minimize/restore "
                  << (K_MINIMIZED_OBSERVED ? "passed" : "denied by compositor") << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "GPU verification failed: " << error.what() << '\n';
        return 2;
    }
}
