#include "world_explorer/render_vulkan/renderer.hpp"
#include "world_explorer/scene_store/bundle.hpp"
#include <SDL3/SDL.h>
#include <cmath>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

using namespace world_explorer;
namespace {
using json = nlohmann::json;
constexpr std::uint32_t WIDTH = 1280, HEIGHT = 720;
void require(bool condition, const std::string& message) {
    if (!condition)
        throw std::runtime_error(message);
}
json vector(vec3d value) { return {value.x, value.y, value.z}; }
void write_json(const std::filesystem::path& path, const json& value) {
    std::ofstream stream(path);
    stream << value.dump(2) << '\n';
    require(stream.good(), "cannot write reference fixture/result");
}
scene_snapshot fixture() {
    auto scene = std::make_shared<scene_state>();
    scene->id = {71};
    scene->revision = {1};
    scene->provenance["diagnostic"] = "synthetic asymmetric render reference v2; not a capture";
    auto chunk = std::make_shared<scene_chunk>();
    chunk->id = {1};
    chunk->bounds = {{-10, -10, -60}, {10, 10, 6}};
    for (int index = 0; index < 12; ++index) {
        gaussian splat;
        const auto row = index / 4;
        splat.mean = {static_cast<float>((index % 4 - 1.5) * 0.7),
                      static_cast<float>((row - 1) * 0.65), -0.1F * static_cast<float>(index)};
        splat.scale = {0.25F, 0.08F + 0.015F * static_cast<float>(index % 3), 0.12F};
        const auto angle = 0.17 * index;
        splat.rotation = {static_cast<float>(std::cos(angle)), 0, 0,
                          static_cast<float>(std::sin(angle))};
        splat.opacity = 0.35F + 0.045F * static_cast<float>(index);
        splat.sh_degree = 3;
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const auto color =
                0.12F + 0.22F * static_cast<float>((static_cast<std::size_t>(index) + channel) % 4);
            splat.sh[channel] = (color - 0.5F) / 0.2820947918F;
            for (std::size_t coefficient = 1; coefficient < 16; ++coefficient)
                splat.sh[coefficient * 3 + channel] = static_cast<float>(
                    0.025 * std::sin(index + static_cast<double>(coefficient) * 1.3 +
                                     static_cast<double>(channel)));
        }
        chunk->splats.push_back(splat);
    }
    auto edge = chunk->splats.front();
    // An off-screen center with a broad, oblique footprint exercises Jacobian clipping.
    edge.mean = {6, 0.2F, 2};
    edge.scale = {0.3F, 0.4F, 4.F};
    edge.rotation = {1, 0, 0, 0};
    edge.opacity = 0.9F;
    for (std::size_t channel = 0; channel < 3; ++channel)
        edge.sh[channel] = (0.9F - 0.5F) / 0.2820947918F;
    chunk->splats.push_back(edge);
    auto clipped = edge;
    clipped.mean = {0, 0, 5.995F}; // Behind the 0.01 near plane.
    chunk->splats.push_back(clipped);
    clipped.mean = {0, 0, -50}; // Beyond the 20-unit far plane.
    chunk->splats.push_back(clipped);
    scene->chunks.emplace(chunk->id, chunk);
    auto transformed = std::make_shared<scene_chunk>();
    transformed->id = {2};
    transformed->bounds = {{-1, -1, -1}, {1, 1, 1}};
    transformed->world_from_chunk = {{std::cos(0.3), 0, 0, std::sin(0.3)}, {0.3, 0.2, 1}, 0.9};
    for (std::size_t index = 0; index < 3; ++index) {
        auto overlap = chunk->splats[index + 4];
        overlap.mean = {0.1F * static_cast<float>(index), 0.1F * static_cast<float>(index),
                        -0.2F * static_cast<float>(index)};
        overlap.scale = {0.45F, 0.16F, 0.09F};
        overlap.opacity = 0.65F;
        transformed->splats.push_back(overlap);
    }
    scene->chunks.emplace(transformed->id, transformed);
    return scene;
}
json export_chunks(const scene_state& scene) {
    json chunks = json::array();
    for (const auto& [id, chunk] : scene.chunks) {
        static_cast<void>(id);
        const auto& transform = chunk->world_from_chunk;
        const auto q = transform.rotation;
        json splats = json::array();
        for (const auto& splat : chunk->splats)
            splats.push_back({{"mean", splat.mean},
                              {"scale", splat.scale},
                              {"rotation", splat.rotation},
                              {"opacity", splat.opacity},
                              {"sh", splat.sh},
                              {"sh_degree", splat.sh_degree}});
        chunks.push_back({{"rotation", {q.w, q.x, q.y, q.z}},
                          {"translation", vector(transform.translation)},
                          {"scale", transform.scale},
                          {"splats", std::move(splats)}});
    }
    return chunks;
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "usage: we_gpu_reference <new output directory>");
        const std::filesystem::path output(argv[1]);
        require(std::filesystem::create_directory(output), "output directory must not exist");
        const auto scene = fixture();
        auto saved = write_bundle(output / "fixture.wexplore", scene);
        require(saved.has_value(), saved ? "" : saved.error().message);
        require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        struct sdl_guard {
            ~sdl_guard() { SDL_Quit(); }
        } sdl;
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Synthetic fixed-camera comparison", WIDTH, HEIGHT,
                             SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN),
            SDL_DestroyWindow);
        require(window != nullptr, SDL_GetError());
        renderer_options options;
        options.max_splats = 250000;
        options.shader_directory = WE_SHADER_DIRECTORY;
        auto created = vulkan_renderer::create(window.get(), options);
        require(created.has_value(), created ? "" : created.error().message);
        auto renderer = std::move(*created);
        require(renderer->capabilities().validation_enabled, "Vulkan validation is required");
        auto batch = make_snapshot_batch(scene, {1}).value();
        auto admitted = renderer->updates().enqueue(batch);
        require(admitted.has_value(), admitted ? "" : admitted.error().message);
        camera_view wrong_extent;
        wrong_extent.calibration = calibrated_projection{{300, 300, 100, 100}, 640, 360};
        require(!renderer->draw(wrong_extent), "mismatched calibration was admitted");
        const auto unchanged = renderer->updates().query_progress(*admitted);
        require(unchanged.has_value(), "invalid camera lost update ticket");
        require(renderer->diagnostics().submitted_frames == 0 &&
                    unchanged->state == update_state::ACCEPTED,
                "invalid camera consumed the pending scene update");
        json views = json::array();
        for (const std::string name : {"centered", "off_center", "rolled", "translated"}) {
            camera_view camera;
            camera.position = {0, 0, 6};
            camera.far_plane = 20;
            camera.calibration = calibrated_projection{{900, 840, 639.5, 359.5}, WIDTH, HEIGHT};
            if (name != "centered")
                camera.calibration->intrinsics = {930, 790, 481.25, 302.75};
            if (name == "rolled" || name == "translated") {
                camera.right = {std::cos(0.37), -std::sin(0.37), 0};
                camera.down = {-std::sin(0.37), -std::cos(0.37), 0};
            }
            if (name == "translated")
                camera.position = {-0.3, 0.15, 6.4};
            const auto k = camera.calibration->intrinsics;
            views.push_back({{"name", name},
                             {"position", vector(camera.position)},
                             {"right", vector(camera.right)},
                             {"down", vector(camera.down)},
                             {"forward", vector(camera.forward)},
                             {"intrinsics", {k.fx, k.fy, k.cx, k.cy}},
                             {"near", camera.near_plane},
                             {"far", camera.far_plane}});
            bool captured = false;
            for (int frame = 0; frame < 120 && !captured; ++frame) {
                SDL_PumpEvents();
                int width{}, height{};
                SDL_GetWindowSizeInPixels(window.get(), &width, &height);
                require(width == WIDTH && height == HEIGHT, "native framebuffer extent changed");
                auto status = renderer->updates().query_progress(*admitted);
                require(status.has_value(), status ? "" : status.error().message);
                if (status->state == update_state::RENDER_VISIBLE) {
                    auto capture = renderer->request_screenshot(output / (name + ".ppm"));
                    require(capture.has_value(), capture ? "" : capture.error().message);
                    captured = true;
                }
                const auto drawn = renderer->draw(camera);
                require(drawn.has_value(), drawn ? "" : drawn.error().message);
            }
            require(captured && std::filesystem::exists(output / (name + ".ppm")),
                    "scene never became visible/captured");
        }
        require(renderer->wait_for_idle().has_value(), "GPU completion failed");
        const auto stats = renderer->diagnostics();
        require(stats.validation_errors == 0, "Vulkan validation errors");
        write_json(output / "fixture.json",
                   {{"schema", 1},
                    {"label", "synthetic asymmetric reference v2; not a capture"},
                    {"width", WIDTH},
                    {"height", HEIGHT},
                    {"background", {0.045F, 0.051F, 0.063F}},
                    {"chunks", export_chunks(*scene)},
                    {"views", views},
                    {"scene_content_sha256", scene_content_hash(*scene)}});
        write_json(output / "native.json", {{"device", renderer->capabilities().device_name},
                                            {"validation_errors", stats.validation_errors},
                                            {"splat_count", stats.resident_splat_count},
                                            {"submitted_frames", stats.submitted_frames}});
        std::cout << "Synthetic fixed-camera fixture: " << stats.resident_splat_count
                  << " splats, 4 views, 1280x720, validation_errors=0\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Fixed-camera rendering failed: " << error.what() << '\n';
        return 2;
    }
}
