#include "world_explorer/application/scenes.hpp"
#include "world_explorer/render_vulkan/renderer.hpp"
#include "world_explorer/scene_store/scene_store.hpp"
#include <SDL3/SDL.h>
#include <fstream>
#include <iostream>

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
} // namespace
int main(int argc, char** argv) {
    try {
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
        created = vulkan_renderer::create(window.get(), options);
        require(created.has_value(), created ? "" : created.error().message);
        renderer = std::move(*created);
        auto batch = make_snapshot_batch(overlap_fixture(), {1}).value();
        require(renderer->updates().enqueue(batch).has_value(), "initial scene admission failed");
        camera_view camera;
        camera.position = {0, 0, 4};
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
        }
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
        std::cout << "GPU verification: overlap pixel=" << static_cast<int>(pixel[0]) << ','
                  << static_cast<int>(pixel[1]) << ',' << static_cast<int>(pixel[2])
                  << "; replace/delete/resize passed; validation_errors=0; frames="
                  << stats.submitted_frames << "; framebuffer_bytes=" << stats.framebuffer_bytes
                  << "; framebuffer, composition-scratch and tile-reference budgets rejected\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "GPU verification failed: " << error.what() << '\n';
        return 2;
    }
}
