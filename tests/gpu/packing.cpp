#include "world_explorer/application/scenes.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/render_vulkan/renderer.hpp"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numbers>
#include <stdexcept>

using namespace world_explorer;

namespace {

using Json = nlohmann::json;

void require(bool valid, const std::string& message) {
    if (!valid) {
        throw std::runtime_error(message);
    }
}

void save(const std::filesystem::path& path, const Json& value) {
    std::ofstream stream(path, std::ios::binary);
    stream << value.dump(2) << '\n';
    require(static_cast<bool>(stream), "cannot write packing report");
}

std::string digest(const std::filesystem::path& path) {
    const auto K_RESULT = sha256_file(path, 16ULL * 1024 * 1024);
    require(K_RESULT.has_value(), K_RESULT ? "" : K_RESULT.error().m_message);
    return *K_RESULT;
}

Json vector(Vec3d value) {
    return { value.m_x, value.m_y, value.m_z };
}

}  // namespace
int main(int argc, char** argv) try {
    require(argc == 3, "usage: we_gpu_packing <scene.wexplore> <new-output-directory>");
    const std::filesystem::path K_OUTPUT(argv[2]);
    require(std::filesystem::create_directory(K_OUTPUT), "output must be a new directory");
    BundleLimits limits;
    limits.m_scene.m_max_splats = 1000000;
    limits.m_scene.m_max_payload_bytes = 512ULL * 1024 * 1024;
    const auto K_SCENE = load_bundle(argv[1], limits);
    require(K_SCENE.has_value(), K_SCENE ? "" : K_SCENE.error().m_message);
    const auto K_INSPECTION = source_view(**K_SCENE);
    require(K_INSPECTION.has_value(), K_INSPECTION ? "" : K_INSPECTION.error().m_message);
    std::size_t count {};
    for (const auto& [K_ID, K_CHUNK] : (*K_SCENE)->m_chunks) {
        static_cast<void>(K_ID);
        count += K_CHUNK->m_splats.size();
    }
    require(count > 0, "packing comparison requires a nonempty retained scene");
    // Freeze all views before either render. This local loop has no captured
    // target images: its pairwise distortion is not Q6's ground-truth PSNR loss.
    std::array<CameraView, 100> cameras;
    Json views = Json::array();
    for (std::size_t index { 0uz }; index < cameras.size(); ++index) {
        const auto K_ANGLE = 2 * std::numbers::pi * static_cast<double>(index) / 100;
        auto spectator = K_INSPECTION->m_camera;
        spectator.m_position = spectator.m_position +
                               spectator.right() * (0.1 * std::sin(K_ANGLE)) +
                               spectator.down() * (0.05 * std::sin(2 * K_ANGLE)) +
                               spectator.forward() * (0.05 * std::cos(K_ANGLE));
        spectator.m_yaw += 0.15 * std::sin(K_ANGLE);
        spectator.m_pitch += 0.1 * std::cos(K_ANGLE);
        auto& camera = cameras[index];
        camera.m_position = spectator.m_position;
        camera.m_right = spectator.right();
        camera.m_down = spectator.down();
        camera.m_forward = spectator.forward();
        camera.m_vertical_fov_radians = K_INSPECTION->m_vertical_fov_radians;
        require(raster_intrinsics(camera, 1280, 720).has_value(), "invalid frozen camera");
        views.push_back({ { "index", index },
                         { "position", vector(camera.m_position) },
                         { "right", vector(camera.m_right) },
                         { "down", vector(camera.m_down) },
                         { "forward", vector(camera.m_forward) },
                         { "vertical_fov_radians", camera.m_vertical_fov_radians },
                         { "near", camera.m_near_plane },
                         { "far", camera.m_far_plane } });
    }
    const auto K_CONTENT = scene_content_hash(**K_SCENE);
    const auto K_MANIFEST = std::filesystem::path(argv[1]) / "manifest.json";
    const auto K_MANIFEST_HASH = digest(K_MANIFEST);
    save(K_OUTPUT / "path.json",
         { { "schema_version", 1 },
          { "profile", "source_view_local_loop_v1" },
          { "scene_content_sha256", K_CONTENT },
          { "width", 1280 },
          { "height", 720 },
          { "world_units", "canonical scene units; may be arbitrary" },
          { "views", views } });
    const auto K_PATH_HASH = digest(K_OUTPUT / "path.json");
    require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());

    struct SdlOwner {
        ~SdlOwner() {
            SDL_Quit();
        }
    } sdl;

    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("World Explorer local packing comparison",
                         1280,
                         720,
                         SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN),
        SDL_DestroyWindow);
    require(window != nullptr, SDL_GetError());
    Json report {
        { "schema_version", 1 },
        { "scope", "local retained-scene GPU packing distortion" },
        { "q6_qualified", false },
        { "scene_content_sha256", K_CONTENT },
        { "manifest_sha256", K_MANIFEST_HASH },
        { "path_sha256", K_PATH_HASH },
        { "scene_splats", count },
        { "width", 1280 },
        { "height", 720 },
        { "color_domain", "display-encoded RGB8 in [0,1]; full image; no fitted correction" },
        { "sh_order", "gsplat real basis; coefficient-major RGB; degree per splat" },
        { "geometry_quantization", "identical FP32 geometry bytes in both GPU profiles" },
        { "ground_truth_available", false },
        { "runs", Json::array() } };
    for (const auto K_PROFILE : { GpuSplatStorage::FP32, GpuSplatStorage::FP32_GEOMETRY_FP16_SH }) {
        const std::string K_NAME = K_PROFILE == GpuSplatStorage::FP32 ? "fp32" : "fp16_sh";
        RendererOptions options;
        options.m_max_splats = 1000000;
        options.m_splat_storage = K_PROFILE;
        options.m_shader_directory = WE_SHADER_DIRECTORY;
        auto created = VulkanRenderer::create(window.get(), options);
        require(created.has_value(), created ? "" : created.error().m_message);
        auto renderer = std::move(*created);
        require(renderer->capabilities().m_validation_enabled, "Vulkan validation is required");
        report["device"] = renderer->capabilities().m_device_name;
        auto batch = make_snapshot_batch(*K_SCENE, { 1 }).value();
        const auto K_TICKET = renderer->updates().enqueue(batch);
        require(K_TICKET.has_value(), "scene admission failed");
        bool visible = false;
        for (int frame { 0 }; frame < 120 && !visible; ++frame) {
            SDL_PumpEvents();
            const auto K_DRAWN = renderer->draw(cameras.front());
            require(K_DRAWN.has_value(), K_DRAWN ? "" : K_DRAWN.error().m_message);
            const auto K_PROGRESS = renderer->updates().query_progress(*K_TICKET);
            require(K_PROGRESS && K_PROGRESS->m_state != UpdateState::FAILED,
                    "scene upload failed");
            visible = K_PROGRESS->m_state == UpdateState::RENDER_VISIBLE;
        }
        require(visible, "scene never became visible");
        Json images = Json::array();
        std::size_t peak {};
        for (std::size_t index { 0uz }; index < cameras.size(); ++index) {
            const auto K_FILE = K_NAME + "-" + std::to_string(index) + ".ppm";
            require(renderer->request_screenshot(K_OUTPUT / K_FILE).has_value(),
                    "capture request failed");
            SDL_PumpEvents();
            const auto K_DRAWN = renderer->draw(cameras[index]);
            require(K_DRAWN.has_value(), K_DRAWN ? "" : K_DRAWN.error().m_message);
            require(renderer->wait_for_idle().has_value(), "capture drain failed");
            const auto K_STATS = renderer->diagnostics();
            require(K_STATS.m_validation_errors == 0 && K_STATS.m_resident_splat_count == count &&
                        K_STATS.m_allocated_bytes <= options.m_residency_bytes,
                    "validation/count/budget error");
            peak = std::max(peak, K_STATS.m_allocated_bytes);
            images.push_back(
                { { "index", index }, { "file", K_FILE }, { "sha256", digest(K_OUTPUT / K_FILE) } });
        }
        report["runs"].push_back(
            { { "profile", K_NAME },
             { "splat_stride_bytes", renderer->capabilities().m_splat_stride_bytes },
             { "peak_sampled_owned_bytes", peak },
             { "validation_errors", 0 },
             { "images", images } });
    }
    require(digest(K_MANIFEST) == K_MANIFEST_HASH &&
                digest(K_OUTPUT / "path.json") == K_PATH_HASH &&
                scene_content_hash(**K_SCENE) == K_CONTENT,
            "comparison inputs changed");
    report["status"] = "complete";
    save(K_OUTPUT / "comparison.json", report);
    std::cout << "100 views per GPU profile saved; validation_errors=0; Q6 remains unqualified\n";
    return 0;
} catch (const std::exception& exception) {
    std::cerr << "Packing comparison failed: " << exception.what() << '\n';
    return 2;
}
