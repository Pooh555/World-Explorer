#include "world_explorer/render_vulkan/renderer.hpp"

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

SceneSnapshot fixture(std::size_t count, SceneRevisionId revision) {
    auto scene = std::make_shared<SceneState>();
    scene->m_id = { 71 };
    scene->m_revision = revision;
    scene->m_provenance = { { "source_kind", "synthetic_diagnostic" },
                           { "fixture", "tile_plan_density_v1" } };
    auto chunk = std::make_shared<SceneChunk>();
    chunk->m_id = { 1 };
    chunk->m_bounds = { { -1, -1, -1 }, { 1, 1, 1 } };
    for (std::size_t index { 0uz }; index < count; ++index) {
        Gaussian splat;
        splat.m_mean = { 0, 0, -static_cast<float>(index) / 10000 };
        splat.m_scale = { 10, 10, 10 };
        splat.m_opacity = 0.03F;
        splat.m_sh = {};
        splat.m_sh[0] = 1;
        chunk->m_splats.push_back(splat);
    }
    scene->m_chunks.emplace(chunk->m_id, chunk);
    return scene;
}

struct FrameExpectation {
    unsigned m_counts, m_batches, m_reused;
    std::size_t m_splats;
    bool m_capture = false, m_appearance = true;
};

std::string bytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    require(stream.good(), "cannot open capture");
    return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
}

}  // namespace
int main(int argc, char** argv) try {
    require(argc == 2, "usage: we_gpu_tile_plans <new output directory>");
    const std::filesystem::path K_OUTPUT(argv[1]);
    require(!std::filesystem::exists(K_OUTPUT), "output already exists");
    std::filesystem::create_directories(K_OUTPUT);
    require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());

    struct SdlGuard {
        ~SdlGuard() {
            SDL_Quit();
        }
    } sdl;

    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("World Explorer synthetic tile plan verification",
                         512,
                         512,
                         SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN),
        SDL_DestroyWindow);
    require(window != nullptr, SDL_GetError());
    nlohmann::json report { { "scope", "synthetic current-density admission and cached/fresh parity" },
                           { "runs", nlohmann::json::array() } };
    for (const bool K_REUSE : { false, true }) {
        require(SDL_SetWindowSize(window.get(), 512, 512), SDL_GetError());
        require(SDL_SyncWindow(window.get()), SDL_GetError());
        SDL_PumpEvents();
        RendererOptions options;
        options.m_shader_directory = WE_SHADER_DIRECTORY;
        options.m_collect_timings = true;
        options.m_reuse_tile_plans = K_REUSE;
        options.m_max_tile_references = 1024;
        options.m_max_splats = 4096;
        auto created = VulkanRenderer::create(window.get(), options);
        require(created.has_value(), created ? "" : created.error().m_message);
        auto renderer = std::move(*created);
        require(renderer->capabilities().m_validation_enabled, "native validation unavailable");
        require(renderer->capabilities().m_gpu_timestamps_supported, "native timing unavailable");
        report["device"] = renderer->capabilities().m_device_name;
        CameraView camera;
        camera.m_position = { 0, 0, 4 };
        std::uint64_t revision = 0;
        auto replace = [&](std::size_t count) {
            ++revision;
            auto batch = make_snapshot_batch(fixture(count, { revision }), { revision }).value();
            const auto K_ADMITTED = renderer->updates().enqueue(batch);
            require(K_ADMITTED.has_value(), K_ADMITTED ? "" : K_ADMITTED.error().m_message);
        };
        auto draw = [&](const char* phase, FrameExpectation expected) {
            SDL_PumpEvents();
            const auto K_PATH =
                K_OUTPUT / (std::string(K_REUSE ? "cached-" : "fresh-") + phase + ".ppm");
            if (expected.m_capture) {
                require(renderer->request_screenshot(K_PATH).has_value(), "capture unavailable");
            }
            const auto K_DRAWN = renderer->draw(camera, expected.m_appearance);
            require(K_DRAWN.has_value(), K_DRAWN ? "" : K_DRAWN.error().m_message);
            require(renderer->wait_for_idle().has_value(), "GPU completion failed");
            const auto K_STATS = renderer->diagnostics();
            require(K_STATS.m_validation_errors == 0, "Vulkan validation failed");
            require(K_STATS.m_allocated_bytes <= options.m_residency_bytes,
                    "owned residency exceeded");
            require(K_STATS.m_resident_splat_count == expected.m_splats,
                    "contributors were dropped");
            const auto K_TILES = static_cast<std::size_t>((K_STATS.m_framebuffer_width + 15) / 16) *
                                 ((K_STATS.m_framebuffer_height + 15) / 16);
            require(K_STATS.m_tile_reference_count ==
                        (expected.m_appearance ? expected.m_splats * K_TILES : 0),
                    "tile coverage/reference count changed");
            bool found = false;
            for (const auto& timing : K_STATS.m_completed_timings) {
                if (timing.m_sequence != K_STATS.m_submitted_frames) {
                    continue;
                }
                found = true;
                require(timing.m_count_submissions == expected.m_counts,
                        std::string(phase) + " count submissions " +
                            std::to_string(timing.m_count_submissions));
                require(timing.m_tile_batches == expected.m_batches,
                        std::string(phase) + " accepted batches " +
                            std::to_string(timing.m_tile_batches));
                require(timing.m_reused_tile_regions == expected.m_reused,
                        "stale/missing tile candidates");
                report["runs"].push_back({ { "reuse", K_REUSE },
                                          { "phase", phase },
                                          { "sequence", timing.m_sequence },
                                          { "counts", timing.m_count_submissions },
                                          { "batches", timing.m_tile_batches },
                                          { "reused", timing.m_reused_tile_regions },
                                          { "references", K_STATS.m_tile_reference_count },
                                          { "splats", expected.m_splats },
                                          { "width", K_STATS.m_framebuffer_width },
                                          { "height", K_STATS.m_framebuffer_height },
                                          { "allocated_bytes", K_STATS.m_allocated_bytes } });
            }
            require(found, "completed frame sample absent");
            if (expected.m_capture && K_REUSE) {
                require(bytes(K_PATH) == bytes(K_OUTPUT / (std::string("fresh-") + phase + ".ppm")),
                        "cached partition changed pixels");
            }
        };
        replace(1);
        draw("sparse-cold", { 1, 1, 0, 1 });
        draw("sparse-warm", { 1, 1, K_REUSE ? 1U : 0U, 1, true });
        replace(8);
        draw("dense-cold", { 15, 8, K_REUSE ? 1U : 0U, 8 });
        draw("dense-warm", { K_REUSE ? 8U : 15U, 8, K_REUSE ? 8U : 0U, 8, true });
        // A camera move changes current projection, while retained rectangles still need
        // recounting.
        camera.m_position.m_z = 3.5;
        replace(32);
        draw("denser-cold", { K_REUSE ? 56U : 63U, 32, K_REUSE ? 8U : 0U, 32 });
        draw("denser-warm", { K_REUSE ? 32U : 63U, 32, K_REUSE ? 32U : 0U, 32, true });
        draw("paused", { 0, 1, 0, 32, false, false });
        draw("resumed", { K_REUSE ? 32U : 63U, 32, K_REUSE ? 32U : 0U, 32 });
        replace(1);
        draw("sparse-again", { K_REUSE ? 32U : 1U, K_REUSE ? 32U : 1U, K_REUSE ? 32U : 0U, 1, true });
        replace(0);
        draw("empty", { 0, 1, 0, 0 });
        replace(1);
        require(SDL_SetWindowSize(window.get(), 640, 512), SDL_GetError());
        require(SDL_SyncWindow(window.get()), SDL_GetError());
        draw("resize-cold", { 3, 2, 0, 1 });
        draw("resize-warm", { K_REUSE ? 2U : 3U, 2, K_REUSE ? 2U : 0U, 1, true });
        replace(2048);
        const auto K_OVERFLOW = renderer->draw(camera);
        require(!K_OVERFLOW &&
                    K_OVERFLOW.error().m_message.find("one tile exceeds") != std::string::npos,
                "single-tile overflow was emitted or silently trimmed");
        require(renderer->wait_for_idle().has_value(), "overflow GPU completion failed");
        require(renderer->diagnostics().m_validation_errors == 0, "overflow validation failed");
    }
    report["passed"] = true;
    std::ofstream stream(K_OUTPUT / "report.json");
    stream << report.dump(2) << '\n';
    require(stream.good(), "report write failed");
    std::cout << report.dump() << '\n';
    return 0;
} catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 2;
}
