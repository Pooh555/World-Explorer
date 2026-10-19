#include "../fixtures/viewer_fixture.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/render_vulkan/renderer.hpp"
#include "world_explorer/scene_store/bundle.hpp"

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

}  // namespace
int main(int argc, char** argv) try {
    std::filesystem::path output;
    bool owned = false;
    nlohmann::json report { { "scope",
                            "synthetic 250k native load/readback only; no performance or "
                            "reconstruction certification" },
                           { "passed", false } };
    try {
        require(argc == 3, "usage: we_gpu_portable_fixture FIXED_BUNDLE NEW_DIRECTORY");
        output = argv[2];
        owned = std::filesystem::create_directory(output);
        require(owned, "output directory must not exist");
        BundleLimits limits;
        limits.m_scene.m_max_chunks = 4;
        limits.m_scene.m_max_splats = K_VIEWER_FIXTURE_SPLATS;
        limits.m_scene.m_max_payload_bytes = 128uz * 1024 * 1024;
        const auto K_SCENE = load_bundle(argv[1], limits);
        require(K_SCENE.has_value(), K_SCENE ? "" : K_SCENE.error().m_message);
        const auto K_HASH = scene_content_hash(**K_SCENE);
        require(K_HASH == K_VIEWER_FIXTURE_SHA256, "bundle differs from frozen portable fixture");
        report["scene_content_sha256"] = K_HASH;
        require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());

        struct SdlGuard {
            ~SdlGuard() {
                SDL_Quit();
            }
        } sdl;

        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Synthetic portable 250k fixture",
                             K_VIEWER_FIXTURE_WIDTH,
                             K_VIEWER_FIXTURE_HEIGHT,
                             SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN),
            SDL_DestroyWindow);
        require(window != nullptr, SDL_GetError());
        RendererOptions options;
        options.m_max_splats = K_VIEWER_FIXTURE_SPLATS;
        options.m_shader_directory = WE_SHADER_DIRECTORY;
        auto created = VulkanRenderer::create(window.get(), options);
        require(created.has_value(), created ? "" : created.error().m_message);
        auto renderer = std::move(*created);
        const auto& capabilities = renderer->capabilities();
        require(capabilities.m_validation_enabled, "Vulkan validation is required");
        report["device"] = capabilities.m_device_name;
        report["video_driver"] = SDL_GetCurrentVideoDriver();
        report["instance_version"] = capabilities.m_instance_version;
        report["device_version"] = capabilities.m_device_version;
        report["portability_subset"] = capabilities.m_portability_subset;
        auto batch = make_snapshot_batch(*K_SCENE, { 1 }).value();
        const auto K_ADMITTED = renderer->updates().enqueue(batch);
        require(K_ADMITTED.has_value(), K_ADMITTED ? "" : K_ADMITTED.error().m_message);
        const auto K_SCREENSHOT = output / "fixture.ppm";
        bool requested = false;
        for (int frame { 0 }; frame < 64 && !std::filesystem::exists(K_SCREENSHOT); ++frame) {
            SDL_PumpEvents();
            int width {}, height {};
            require(SDL_GetWindowSizeInPixels(window.get(), &width, &height), SDL_GetError());
            require(width == static_cast<int>(K_VIEWER_FIXTURE_WIDTH) &&
                        height == static_cast<int>(K_VIEWER_FIXTURE_HEIGHT),
                    "framebuffer extent changed");
            const auto K_STATUS = renderer->updates().query_progress(*K_ADMITTED);
            require(K_STATUS.has_value(), K_STATUS ? "" : K_STATUS.error().m_message);
            if (!requested && K_STATUS->m_state == UpdateState::RENDER_VISIBLE) {
                require(renderer->request_screenshot(K_SCREENSHOT).has_value(),
                        "readback request failed");
                requested = true;
            }
            const auto K_DRAWN = renderer->draw(portable_viewer_camera());
            require(K_DRAWN.has_value(), K_DRAWN ? "" : K_DRAWN.error().m_message);
        }
        require(requested && std::filesystem::exists(K_SCREENSHOT), "fixture never became visible");
        require(renderer->wait_for_idle().has_value(), "GPU completion failed");
        const auto K_STATS = renderer->diagnostics();
        report["resident_splats"] = K_STATS.m_resident_splat_count;
        report["submitted_frames"] = K_STATS.m_submitted_frames;
        report["validation_errors"] = K_STATS.m_validation_errors;
        report["allocated_bytes"] = K_STATS.m_allocated_bytes;
        report["width"] = K_STATS.m_framebuffer_width;
        report["height"] = K_STATS.m_framebuffer_height;
        require(K_STATS.m_resident_splat_count == K_VIEWER_FIXTURE_SPLATS &&
                    K_STATS.m_validation_errors == 0,
                "resident count or validation failure");
        const auto K_READBACK_HASH = sha256_file(K_SCREENSHOT, 4uz * 1024 * 1024);
        require(K_READBACK_HASH.has_value(), "readback checksum failed");
        std::ifstream image(K_SCREENSHOT, std::ios::binary);
        std::string magic;
        std::uint32_t width {}, height {}, maximum {};
        image >> magic >> width >> height >> maximum;
        require(magic == "P6" && width == K_VIEWER_FIXTURE_WIDTH &&
                    height == K_VIEWER_FIXTURE_HEIGHT && maximum == 255 && image.get() == '\n',
                "invalid full-size RGB8 readback");
        std::vector<unsigned char> pixels(std::size_t { K_VIEWER_FIXTURE_WIDTH } *
                                          K_VIEWER_FIXTURE_HEIGHT * 3);
        image.read(reinterpret_cast<char*>(pixels.data()),
                   static_cast<std::streamsize>(pixels.size()));
        require(image.gcount() == static_cast<std::streamsize>(pixels.size()) &&
                    image.peek() == EOF,
                "invalid readback byte count");
        constexpr std::array<int, 3> K_BACKGROUND { 11, 13, 16 };
        std::size_t foreground = 0;
        for (std::size_t pixel { 0uz }; pixel < pixels.size(); pixel += 3uz) {
            if (std::abs(static_cast<int>(pixels[pixel]) - K_BACKGROUND[0]) > 8 ||
                std::abs(static_cast<int>(pixels[pixel + 1]) - K_BACKGROUND[1]) > 8 ||
                std::abs(static_cast<int>(pixels[pixel + 2]) - K_BACKGROUND[2]) > 8) {
                ++foreground;
            }
        }
        // An off-screen/blank render cannot certify that the fixed scene opened.
        require(foreground > std::size_t { K_VIEWER_FIXTURE_WIDTH } * K_VIEWER_FIXTURE_HEIGHT / 100,
                "fixture readback lacks foreground");
        report["foreground_pixels"] = foreground;
        report["screenshot_sha256"] = *K_READBACK_HASH;
        report["passed"] = true;
    } catch (const std::exception& error) {
        report["error"] = error.what();
        std::cerr << "Synthetic portable fixture failed: " << error.what() << '\n';
    }
    if (owned) {
        std::ofstream stream(output / "native.json", std::ios::binary);
        stream << report.dump(2) << '\n';
        stream.close();
        if (!stream.good()) {
            return 2;
        }
    }
    std::cout << report.dump() << '\n';
    return report.at("passed").get<bool>() ? 0 : 2;
} catch (const std::exception& error) {
    std::cerr << "Portable fixture report failed: " << error.what() << '\n';
    return 2;
}
