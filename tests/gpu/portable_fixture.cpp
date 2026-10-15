#include "../fixtures/viewer_fixture.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/render_vulkan/renderer.hpp"
#include "world_explorer/scene_store/bundle.hpp"
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
} // namespace
int main(int argc, char** argv) try {
    std::filesystem::path output;
    bool owned = false;
    nlohmann::json report{{"scope", "synthetic 250k native load/readback only; no performance or "
                                    "reconstruction certification"},
                          {"passed", false}};
    try {
        require(argc == 3, "usage: we_gpu_portable_fixture FIXED_BUNDLE NEW_DIRECTORY");
        output = argv[2];
        owned = std::filesystem::create_directory(output);
        require(owned, "output directory must not exist");
        bundle_limits limits;
        limits.scene.max_chunks = 4;
        limits.scene.max_splats = VIEWER_FIXTURE_SPLATS;
        limits.scene.max_payload_bytes = std::size_t{128} * 1024 * 1024;
        const auto scene = load_bundle(argv[1], limits);
        require(scene.has_value(), scene ? "" : scene.error().message);
        const auto hash = scene_content_hash(**scene);
        require(hash == VIEWER_FIXTURE_SHA256, "bundle differs from frozen portable fixture");
        report["scene_content_sha256"] = hash;
        require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        struct sdl_guard {
            ~sdl_guard() { SDL_Quit(); }
        } sdl;
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Synthetic portable 250k fixture", VIEWER_FIXTURE_WIDTH,
                             VIEWER_FIXTURE_HEIGHT, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN),
            SDL_DestroyWindow);
        require(window != nullptr, SDL_GetError());
        renderer_options options;
        options.max_splats = VIEWER_FIXTURE_SPLATS;
        options.shader_directory = WE_SHADER_DIRECTORY;
        auto created = vulkan_renderer::create(window.get(), options);
        require(created.has_value(), created ? "" : created.error().message);
        auto renderer = std::move(*created);
        const auto& capabilities = renderer->capabilities();
        require(capabilities.validation_enabled, "Vulkan validation is required");
        report["device"] = capabilities.device_name;
        report["video_driver"] = SDL_GetCurrentVideoDriver();
        report["instance_version"] = capabilities.instance_version;
        report["device_version"] = capabilities.device_version;
        report["portability_subset"] = capabilities.portability_subset;
        auto batch = make_snapshot_batch(*scene, {1}).value();
        const auto admitted = renderer->updates().enqueue(batch);
        require(admitted.has_value(), admitted ? "" : admitted.error().message);
        const auto screenshot = output / "fixture.ppm";
        bool requested = false;
        for (int frame = 0; frame < 64 && !std::filesystem::exists(screenshot); ++frame) {
            SDL_PumpEvents();
            int width{}, height{};
            require(SDL_GetWindowSizeInPixels(window.get(), &width, &height), SDL_GetError());
            require(width == static_cast<int>(VIEWER_FIXTURE_WIDTH) &&
                        height == static_cast<int>(VIEWER_FIXTURE_HEIGHT),
                    "framebuffer extent changed");
            const auto status = renderer->updates().query_progress(*admitted);
            require(status.has_value(), status ? "" : status.error().message);
            if (!requested && status->state == update_state::RENDER_VISIBLE) {
                require(renderer->request_screenshot(screenshot).has_value(),
                        "readback request failed");
                requested = true;
            }
            const auto drawn = renderer->draw(portable_viewer_camera());
            require(drawn.has_value(), drawn ? "" : drawn.error().message);
        }
        require(requested && std::filesystem::exists(screenshot), "fixture never became visible");
        require(renderer->wait_for_idle().has_value(), "GPU completion failed");
        const auto stats = renderer->diagnostics();
        report["resident_splats"] = stats.resident_splat_count;
        report["submitted_frames"] = stats.submitted_frames;
        report["validation_errors"] = stats.validation_errors;
        report["allocated_bytes"] = stats.allocated_bytes;
        report["width"] = stats.framebuffer_width;
        report["height"] = stats.framebuffer_height;
        require(stats.resident_splat_count == VIEWER_FIXTURE_SPLATS && stats.validation_errors == 0,
                "resident count or validation failure");
        const auto readback_hash = sha256_file(screenshot, std::size_t{4} * 1024 * 1024);
        require(readback_hash.has_value(), "readback checksum failed");
        std::ifstream image(screenshot, std::ios::binary);
        std::string magic;
        std::uint32_t width{}, height{}, maximum{};
        image >> magic >> width >> height >> maximum;
        require(magic == "P6" && width == VIEWER_FIXTURE_WIDTH && height == VIEWER_FIXTURE_HEIGHT &&
                    maximum == 255 && image.get() == '\n',
                "invalid full-size RGB8 readback");
        std::vector<unsigned char> pixels(std::size_t{VIEWER_FIXTURE_WIDTH} *
                                          VIEWER_FIXTURE_HEIGHT * 3);
        image.read(reinterpret_cast<char*>(pixels.data()),
                   static_cast<std::streamsize>(pixels.size()));
        require(image.gcount() == static_cast<std::streamsize>(pixels.size()) &&
                    image.peek() == EOF,
                "invalid readback byte count");
        constexpr std::array<int, 3> BACKGROUND{11, 13, 16};
        std::size_t foreground = 0;
        for (std::size_t pixel = 0; pixel < pixels.size(); pixel += 3)
            if (std::abs(static_cast<int>(pixels[pixel]) - BACKGROUND[0]) > 8 ||
                std::abs(static_cast<int>(pixels[pixel + 1]) - BACKGROUND[1]) > 8 ||
                std::abs(static_cast<int>(pixels[pixel + 2]) - BACKGROUND[2]) > 8)
                ++foreground;
        // An off-screen/blank render cannot certify that the fixed scene opened.
        require(foreground > std::size_t{VIEWER_FIXTURE_WIDTH} * VIEWER_FIXTURE_HEIGHT / 100,
                "fixture readback lacks foreground");
        report["foreground_pixels"] = foreground;
        report["screenshot_sha256"] = *readback_hash;
        report["passed"] = true;
    } catch (const std::exception& error) {
        report["error"] = error.what();
        std::cerr << "Synthetic portable fixture failed: " << error.what() << '\n';
    }
    if (owned) {
        std::ofstream stream(output / "native.json", std::ios::binary);
        stream << report.dump(2) << '\n';
        stream.close();
        if (!stream.good())
            return 2;
    }
    std::cout << report.dump() << '\n';
    return report.at("passed").get<bool>() ? 0 : 2;
} catch (const std::exception& error) {
    std::cerr << "Portable fixture report failed: " << error.what() << '\n';
    return 2;
}
