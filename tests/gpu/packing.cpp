#include "world_explorer/application/scenes.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/render_vulkan/renderer.hpp"
#include <SDL3/SDL.h>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <numbers>
#include <stdexcept>

using namespace world_explorer;
namespace {
using json = nlohmann::json;
void require(bool valid, const std::string& message) {
    if (!valid)
        throw std::runtime_error(message);
}
void save(const std::filesystem::path& path, const json& value) {
    std::ofstream stream(path, std::ios::binary);
    stream << value.dump(2) << '\n';
    require(static_cast<bool>(stream), "cannot write packing report");
}
std::string digest(const std::filesystem::path& path) {
    const auto result = sha256_file(path, 16ULL * 1024 * 1024);
    require(result.has_value(), result ? "" : result.error().message);
    return *result;
}
json vector(vec3d value) { return {value.x, value.y, value.z}; }
} // namespace
int main(int argc, char** argv) try {
    require(argc == 3, "usage: we_gpu_packing <scene.wexplore> <new-output-directory>");
    const std::filesystem::path output(argv[2]);
    require(std::filesystem::create_directory(output), "output must be a new directory");
    bundle_limits limits;
    limits.scene.max_splats = 1000000;
    limits.scene.max_payload_bytes = 512ULL * 1024 * 1024;
    const auto scene = load_bundle(argv[1], limits);
    require(scene.has_value(), scene ? "" : scene.error().message);
    const auto inspection = source_view(**scene);
    require(inspection.has_value(), inspection ? "" : inspection.error().message);
    std::size_t count{};
    for (const auto& [id, chunk] : (*scene)->chunks) {
        static_cast<void>(id);
        count += chunk->splats.size();
    }
    require(count > 0, "packing comparison requires a nonempty retained scene");
    // Freeze all views before either render. This local loop has no captured
    // target images: its pairwise distortion is not Q6's ground-truth PSNR loss.
    std::array<camera_view, 100> cameras;
    json views = json::array();
    for (std::size_t index = 0; index < cameras.size(); ++index) {
        const auto angle = 2 * std::numbers::pi * static_cast<double>(index) / 100;
        auto spectator = inspection->camera;
        spectator.position = spectator.position + spectator.right() * (0.1 * std::sin(angle)) +
                             spectator.down() * (0.05 * std::sin(2 * angle)) +
                             spectator.forward() * (0.05 * std::cos(angle));
        spectator.yaw += 0.15 * std::sin(angle);
        spectator.pitch += 0.1 * std::cos(angle);
        auto& camera = cameras[index];
        camera.position = spectator.position;
        camera.right = spectator.right();
        camera.down = spectator.down();
        camera.forward = spectator.forward();
        camera.vertical_fov_radians = inspection->vertical_fov_radians;
        require(raster_intrinsics(camera, 1280, 720).has_value(), "invalid frozen camera");
        views.push_back({{"index", index},
                         {"position", vector(camera.position)},
                         {"right", vector(camera.right)},
                         {"down", vector(camera.down)},
                         {"forward", vector(camera.forward)},
                         {"vertical_fov_radians", camera.vertical_fov_radians},
                         {"near", camera.near_plane},
                         {"far", camera.far_plane}});
    }
    const auto content = scene_content_hash(**scene);
    const auto manifest = std::filesystem::path(argv[1]) / "manifest.json";
    const auto manifest_hash = digest(manifest);
    save(output / "path.json", {{"schema_version", 1},
                                {"profile", "source_view_local_loop_v1"},
                                {"scene_content_sha256", content},
                                {"width", 1280},
                                {"height", 720},
                                {"world_units", "canonical scene units; may be arbitrary"},
                                {"views", views}});
    const auto path_hash = digest(output / "path.json");
    require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
    struct sdl_owner {
        ~sdl_owner() { SDL_Quit(); }
    } sdl;
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("World Explorer local packing comparison", 1280, 720,
                         SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN),
        SDL_DestroyWindow);
    require(window != nullptr, SDL_GetError());
    json report{{"schema_version", 1},
                {"scope", "local retained-scene GPU packing distortion"},
                {"q6_qualified", false},
                {"scene_content_sha256", content},
                {"manifest_sha256", manifest_hash},
                {"path_sha256", path_hash},
                {"scene_splats", count},
                {"width", 1280},
                {"height", 720},
                {"color_domain", "display-encoded RGB8 in [0,1]; full image; no fitted correction"},
                {"sh_order", "gsplat real basis; coefficient-major RGB; degree per splat"},
                {"geometry_quantization", "identical FP32 geometry bytes in both GPU profiles"},
                {"ground_truth_available", false},
                {"runs", json::array()}};
    for (const auto profile : {gpu_splat_storage::FP32, gpu_splat_storage::FP32_GEOMETRY_FP16_SH}) {
        const std::string name = profile == gpu_splat_storage::FP32 ? "fp32" : "fp16_sh";
        renderer_options options;
        options.max_splats = 1000000;
        options.splat_storage = profile;
        options.shader_directory = WE_SHADER_DIRECTORY;
        auto created = vulkan_renderer::create(window.get(), options);
        require(created.has_value(), created ? "" : created.error().message);
        auto renderer = std::move(*created);
        require(renderer->capabilities().validation_enabled, "Vulkan validation is required");
        report["device"] = renderer->capabilities().device_name;
        auto batch = make_snapshot_batch(*scene, {1}).value();
        const auto ticket = renderer->updates().enqueue(batch);
        require(ticket.has_value(), "scene admission failed");
        bool visible = false;
        for (int frame = 0; frame < 120 && !visible; ++frame) {
            SDL_PumpEvents();
            const auto drawn = renderer->draw(cameras.front());
            require(drawn.has_value(), drawn ? "" : drawn.error().message);
            const auto progress = renderer->updates().query_progress(*ticket);
            require(progress && progress->state != update_state::FAILED, "scene upload failed");
            visible = progress->state == update_state::RENDER_VISIBLE;
        }
        require(visible, "scene never became visible");
        json images = json::array();
        std::size_t peak{};
        for (std::size_t index = 0; index < cameras.size(); ++index) {
            const auto file = name + "-" + std::to_string(index) + ".ppm";
            require(renderer->request_screenshot(output / file).has_value(),
                    "capture request failed");
            SDL_PumpEvents();
            const auto drawn = renderer->draw(cameras[index]);
            require(drawn.has_value(), drawn ? "" : drawn.error().message);
            require(renderer->wait_for_idle().has_value(), "capture drain failed");
            const auto stats = renderer->diagnostics();
            require(stats.validation_errors == 0 && stats.resident_splat_count == count &&
                        stats.allocated_bytes <= options.residency_bytes,
                    "validation/count/budget error");
            peak = std::max(peak, stats.allocated_bytes);
            images.push_back({{"index", index}, {"file", file}, {"sha256", digest(output / file)}});
        }
        report["runs"].push_back(
            {{"profile", name},
             {"splat_stride_bytes", renderer->capabilities().splat_stride_bytes},
             {"peak_sampled_owned_bytes", peak},
             {"validation_errors", 0},
             {"images", images}});
    }
    require(digest(manifest) == manifest_hash && digest(output / "path.json") == path_hash &&
                scene_content_hash(**scene) == content,
            "comparison inputs changed");
    report["status"] = "complete";
    save(output / "comparison.json", report);
    std::cout << "100 views per GPU profile saved; validation_errors=0; Q6 remains unqualified\n";
    return 0;
} catch (const std::exception& exception) {
    std::cerr << "Packing comparison failed: " << exception.what() << '\n';
    return 2;
}
