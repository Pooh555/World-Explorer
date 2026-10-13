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
scene_snapshot fixture(std::size_t count, scene_revision_id revision) {
    auto scene = std::make_shared<scene_state>();
    scene->id = {71};
    scene->revision = revision;
    scene->provenance = {{"source_kind", "synthetic_diagnostic"},
                         {"fixture", "tile_plan_density_v1"}};
    auto chunk = std::make_shared<scene_chunk>();
    chunk->id = {1};
    chunk->bounds = {{-1, -1, -1}, {1, 1, 1}};
    for (std::size_t index = 0; index < count; ++index) {
        gaussian splat;
        splat.mean = {0, 0, -static_cast<float>(index) / 10000};
        splat.scale = {10, 10, 10};
        splat.opacity = 0.03F;
        splat.sh = {};
        splat.sh[0] = 1;
        chunk->splats.push_back(splat);
    }
    scene->chunks.emplace(chunk->id, chunk);
    return scene;
}
struct frame_expectation {
    unsigned counts, batches, reused;
    std::size_t splats;
    bool capture = false, appearance = true;
};
std::string bytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    require(stream.good(), "cannot open capture");
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
} // namespace
int main(int argc, char** argv) try {
    require(argc == 2, "usage: we_gpu_tile_plans <new output directory>");
    const std::filesystem::path output(argv[1]);
    require(!std::filesystem::exists(output), "output already exists");
    std::filesystem::create_directories(output);
    require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
    struct sdl_guard {
        ~sdl_guard() { SDL_Quit(); }
    } sdl;
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("World Explorer synthetic tile plan verification", 512, 512,
                         SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN),
        SDL_DestroyWindow);
    require(window != nullptr, SDL_GetError());
    nlohmann::json report{{"scope", "synthetic current-density admission and cached/fresh parity"},
                          {"runs", nlohmann::json::array()}};
    for (const bool reuse : {false, true}) {
        require(SDL_SetWindowSize(window.get(), 512, 512), SDL_GetError());
        require(SDL_SyncWindow(window.get()), SDL_GetError());
        SDL_PumpEvents();
        renderer_options options;
        options.shader_directory = WE_SHADER_DIRECTORY;
        options.collect_timings = true;
        options.reuse_tile_plans = reuse;
        options.max_tile_references = 1024;
        options.max_splats = 4096;
        auto created = vulkan_renderer::create(window.get(), options);
        require(created.has_value(), created ? "" : created.error().message);
        auto renderer = std::move(*created);
        require(renderer->capabilities().validation_enabled, "native validation unavailable");
        require(renderer->capabilities().gpu_timestamps_supported, "native timing unavailable");
        report["device"] = renderer->capabilities().device_name;
        camera_view camera;
        camera.position = {0, 0, 4};
        std::uint64_t revision = 0;
        auto replace = [&](std::size_t count) {
            ++revision;
            auto batch = make_snapshot_batch(fixture(count, {revision}), {revision}).value();
            const auto admitted = renderer->updates().enqueue(batch);
            require(admitted.has_value(), admitted ? "" : admitted.error().message);
        };
        auto draw = [&](const char* phase, frame_expectation expected) {
            SDL_PumpEvents();
            const auto path = output / (std::string(reuse ? "cached-" : "fresh-") + phase + ".ppm");
            if (expected.capture)
                require(renderer->request_screenshot(path).has_value(), "capture unavailable");
            const auto drawn = renderer->draw(camera, expected.appearance);
            require(drawn.has_value(), drawn ? "" : drawn.error().message);
            require(renderer->wait_for_idle().has_value(), "GPU completion failed");
            const auto stats = renderer->diagnostics();
            require(stats.validation_errors == 0, "Vulkan validation failed");
            require(stats.allocated_bytes <= options.residency_bytes, "owned residency exceeded");
            require(stats.resident_splat_count == expected.splats, "contributors were dropped");
            const auto tiles = static_cast<std::size_t>((stats.framebuffer_width + 15) / 16) *
                               ((stats.framebuffer_height + 15) / 16);
            require(stats.tile_reference_count ==
                        (expected.appearance ? expected.splats * tiles : 0),
                    "tile coverage/reference count changed");
            bool found = false;
            for (const auto& timing : stats.completed_timings) {
                if (timing.sequence != stats.submitted_frames)
                    continue;
                found = true;
                require(timing.count_submissions == expected.counts,
                        std::string(phase) + " count submissions " +
                            std::to_string(timing.count_submissions));
                require(timing.tile_batches == expected.batches,
                        std::string(phase) + " accepted batches " +
                            std::to_string(timing.tile_batches));
                require(timing.reused_tile_regions == expected.reused,
                        "stale/missing tile candidates");
                report["runs"].push_back({{"reuse", reuse},
                                          {"phase", phase},
                                          {"sequence", timing.sequence},
                                          {"counts", timing.count_submissions},
                                          {"batches", timing.tile_batches},
                                          {"reused", timing.reused_tile_regions},
                                          {"references", stats.tile_reference_count},
                                          {"splats", expected.splats},
                                          {"width", stats.framebuffer_width},
                                          {"height", stats.framebuffer_height},
                                          {"allocated_bytes", stats.allocated_bytes}});
            }
            require(found, "completed frame sample absent");
            if (expected.capture && reuse)
                require(bytes(path) == bytes(output / (std::string("fresh-") + phase + ".ppm")),
                        "cached partition changed pixels");
        };
        replace(1);
        draw("sparse-cold", {1, 1, 0, 1});
        draw("sparse-warm", {1, 1, reuse ? 1U : 0U, 1, true});
        replace(8);
        draw("dense-cold", {15, 8, reuse ? 1U : 0U, 8});
        draw("dense-warm", {reuse ? 8U : 15U, 8, reuse ? 8U : 0U, 8, true});
        // A camera move changes current projection, while retained rectangles still need
        // recounting.
        camera.position.z = 3.5;
        replace(32);
        draw("denser-cold", {reuse ? 56U : 63U, 32, reuse ? 8U : 0U, 32});
        draw("denser-warm", {reuse ? 32U : 63U, 32, reuse ? 32U : 0U, 32, true});
        draw("paused", {0, 1, 0, 32, false, false});
        draw("resumed", {reuse ? 32U : 63U, 32, reuse ? 32U : 0U, 32});
        replace(1);
        draw("sparse-again", {reuse ? 32U : 1U, reuse ? 32U : 1U, reuse ? 32U : 0U, 1, true});
        replace(0);
        draw("empty", {0, 1, 0, 0});
        replace(1);
        require(SDL_SetWindowSize(window.get(), 640, 512), SDL_GetError());
        require(SDL_SyncWindow(window.get()), SDL_GetError());
        draw("resize-cold", {3, 2, 0, 1});
        draw("resize-warm", {reuse ? 2U : 3U, 2, reuse ? 2U : 0U, 1, true});
        replace(2048);
        const auto overflow = renderer->draw(camera);
        require(!overflow && overflow.error().message.find("one tile exceeds") != std::string::npos,
                "single-tile overflow was emitted or silently trimmed");
        require(renderer->wait_for_idle().has_value(), "overflow GPU completion failed");
        require(renderer->diagnostics().validation_errors == 0, "overflow validation failed");
    }
    report["passed"] = true;
    std::ofstream stream(output / "report.json");
    stream << report.dump(2) << '\n';
    require(stream.good(), "report write failed");
    std::cout << report.dump() << '\n';
    return 0;
} catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 2;
}
