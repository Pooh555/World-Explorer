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
    chunk->splats = {near, far}; // Intentionally opposite the required draw order.
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
                             SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE),
            SDL_DestroyWindow);
        require(window != nullptr, SDL_GetError());
        renderer_options options;
        options.max_splats = 1024;
        options.shader_directory = WE_SHADER_DIRECTORY;
        auto created = vulkan_renderer::create(window.get(), options);
        require(created.has_value(), created ? "" : created.error().message);
        auto renderer = std::move(*created);
        require(renderer->capabilities().validation_enabled,
                "Validation layer unavailable: this is not a passing GPU test");
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
        }
        require(renderer->wait_for_idle().has_value(), "final GPU completion failed");
        const auto stats = renderer->diagnostics();
        require(stats.validation_errors == 0, "Vulkan validation reported errors");
        require(stats.resident_splat_count == 0 &&
                    renderer->updates().visible_snapshot()->revision.value == 3,
                "Deleted scene still visible");
        std::cout << "GPU verification: overlap pixel=" << static_cast<int>(pixel[0]) << ','
                  << static_cast<int>(pixel[1]) << ',' << static_cast<int>(pixel[2])
                  << "; replace/delete/resize passed; validation_errors=0; frames="
                  << stats.submitted_frames << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "GPU verification failed: " << error.what() << '\n';
        return 2;
    }
}
