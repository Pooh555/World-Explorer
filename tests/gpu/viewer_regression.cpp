#include "world_explorer/application/scenes.hpp"
#include "world_explorer/render_vulkan/renderer.hpp"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <span>

using namespace world_explorer;

namespace {

using Json = nlohmann::json;

void require(bool value, const std::string& message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

double percentile(std::vector<double> values, double fraction) {
    require(!values.empty(), "cannot summarize absent timing samples");
    std::ranges::sort(values);
    return values[static_cast<std::size_t>(fraction * static_cast<double>(values.size() - 1))];
}

Json timing_summary(const std::map<std::uint64_t, RendererFrameTiming>& samples) {
    Json output { { "samples", Json::array() }, { "metrics", Json::object() } };
    std::map<std::string, std::vector<double>> metrics;
    constexpr std::array<const char*, K_GPU_TIMING_STAGE_COUNT> K_STAGES { "upload",
                                                                          "projection",
                                                                          "depth_sort",
                                                                          "count_scan",
                                                                          "tile_index",
                                                                          "composition",
                                                                          "presentation_draw" };
    for (const auto& [K_SEQUENCE, K_SAMPLE] : samples) {
        Json row { { "sequence", K_SEQUENCE },
                  { "tile_sort_passes", K_SAMPLE.m_tile_sort_passes },
                  { "reused_tile_regions", K_SAMPLE.m_reused_tile_regions },
                  { "width", K_SAMPLE.m_width },
                  { "height", K_SAMPLE.m_height },
                  { "gpu_valid", K_SAMPLE.m_gpu_valid },
                  { "count_submissions", K_SAMPLE.m_count_submissions },
                  { "tile_batches", K_SAMPLE.m_tile_batches },
                  { "cpu_frame_wait_ms", K_SAMPLE.m_cpu_frame_wait_ms },
                  { "cpu_count_wait_ms", K_SAMPLE.m_cpu_count_wait_ms },
                  { "cpu_acquire_ms", K_SAMPLE.m_cpu_acquire_ms },
                  { "cpu_present_ms", K_SAMPLE.m_cpu_present_ms },
                  { "gpu_ms", nullptr } };
        for (const auto* metric :
             { "cpu_frame_wait_ms", "cpu_count_wait_ms", "cpu_acquire_ms", "cpu_present_ms" }) {
            metrics[metric].push_back(row.at(metric).get<double>());
        }
        if (K_SAMPLE.m_gpu_valid) {
            row["gpu_ms"] = Json::object();
            double total = 0;
            for (std::size_t index { 0uz }; index < K_STAGES.size(); ++index) {
                row["gpu_ms"][K_STAGES[index]] = K_SAMPLE.m_gpu_ms[index];
                metrics["gpu_" + std::string(K_STAGES[index]) + "_ms"].push_back(
                    K_SAMPLE.m_gpu_ms[index]);
                total += K_SAMPLE.m_gpu_ms[index];
            }
            metrics["gpu_measured_stages_ms"].push_back(total);
        }
        metrics["count_submissions"].push_back(static_cast<double>(K_SAMPLE.m_count_submissions));
        metrics["reused_tile_regions"].push_back(
            static_cast<double>(K_SAMPLE.m_reused_tile_regions));
        metrics["tile_batches"].push_back(static_cast<double>(K_SAMPLE.m_tile_batches));
        output["samples"].push_back(std::move(row));
    }
    for (const auto& [K_NAME, K_VALUES] : metrics) {
        output["metrics"][K_NAME] = { { "p50", percentile(K_VALUES, 0.5) },
                                     { "p95", percentile(K_VALUES, 0.95) },
                                     { "p99", percentile(K_VALUES, 0.99) } };
    }
    return output;
}

}  // namespace
int main(int argc, char** argv) try {
    if (argc < 3 || argc > 5) {
        std::cerr << "usage: we_gpu_viewer_regression <scene> <new report> [--timings] "
                     "[--fresh-tile-plan]\n";
        return 2;
    }
    bool timings = false, reuse_tile_plans = true;
    for (const auto* argument : std::span(argv + 3, static_cast<std::size_t>(argc - 3))) {
        if (std::string_view(argument) == "--timings") {
            timings = true;
        } else if (std::string_view(argument) == "--fresh-tile-plan") {
            reuse_tile_plans = false;
        } else {
            throw std::runtime_error("unknown regression option");
        }
    }
    require(!std::filesystem::exists(argv[2]), "report already exists");
    Json report { { "scope", "local real-scene regression; not P2/P3 certification" },
                 { "present_mode", "FIFO" },
                 { "warmup_frames", 8 },
                 { "measured_frames", 24 },
                 { "timing_collection", timings },
                 { "reuse_tile_plans", reuse_tile_plans },
                 { "cases", Json::array() } };
    try {
        require(!std::filesystem::exists(argv[2]), "report already exists");
        const auto K_SCENE = load_bundle(argv[1]);
        require(K_SCENE.has_value(), K_SCENE ? "" : K_SCENE.error().m_message);
        const auto K_INSPECTION = source_view(**K_SCENE);
        require(K_INSPECTION.has_value(), K_INSPECTION ? "" : K_INSPECTION.error().m_message);
        const bool K_INITIALIZED = SDL_Init(SDL_INIT_VIDEO);
        require(K_INITIALIZED, SDL_GetError());

        struct SdlGuard {
            ~SdlGuard() {
                SDL_Quit();
            }
        } sdl;

        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("World Explorer fullscreen regression",
                             1280,
                             720,
                             SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN),
            SDL_DestroyWindow);
        require(window != nullptr, SDL_GetError());
        RendererOptions options;
        options.m_max_splats = 1000000;
        options.m_collect_timings = timings;
        options.m_reuse_tile_plans = reuse_tile_plans;
        options.m_shader_directory = WE_SHADER_DIRECTORY;
        auto created = VulkanRenderer::create(window.get(), options);
        require(created.has_value(), created ? "" : created.error().m_message);
        auto renderer = std::move(*created);
        require(renderer->capabilities().m_validation_enabled, "validation required");
        report["device"] = renderer->capabilities().m_device_name;
        report["gpu_timestamps_supported"] = renderer->capabilities().m_gpu_timestamps_supported;
        report["timestamp_valid_bits"] = renderer->capabilities().m_timestamp_valid_bits;
        report["timestamp_period_ns"] = renderer->capabilities().m_timestamp_period_ns;
        report["timestamp_queries"] = timings && renderer->capabilities().m_gpu_timestamps_supported
                                          ? K_GPU_TIMING_STAGE_COUNT * 4
                                          : 0;
        report["max_splats"] = options.m_max_splats;
        report["residency_bytes"] = options.m_residency_bytes;
        report["scene_content_sha256"] = scene_content_hash(**K_SCENE);
        auto batch = make_snapshot_batch(*K_SCENE, { 1 }).value();
        const auto K_TICKET = renderer->updates().enqueue(batch);
        require(K_TICKET.has_value(), K_TICKET ? "" : K_TICKET.error().m_message);
        auto spectator = K_INSPECTION->m_camera;
        CameraView camera;
        camera.m_vertical_fov_radians = static_cast<float>(K_INSPECTION->m_vertical_fov_radians);
        std::size_t expected = 0;
        for (const auto& [K_ID, K_CHUNK] : (*K_SCENE)->m_chunks) {
            static_cast<void>(K_ID);
            expected += K_CHUNK->m_splats.size();
        }
        report["scene_splats"] = expected;
        for (const auto* phase : { "windowed", "4k", "fullscreen", "restored" }) {
            auto record = Json { { "phase", phase } };
            try {
                if (std::string_view(phase) == "4k") {
                    require(SDL_SetWindowSize(window.get(), 3840, 2160), SDL_GetError());
                }
                if (std::string_view(phase) == "fullscreen") {
                    require(SDL_ShowWindow(window.get()), SDL_GetError());
                    require(SDL_SyncWindow(window.get()), SDL_GetError());
                    SDL_PumpEvents();
                    // Wayland maps a shown surface only after a buffer is committed.
                    for (int frame { 0 }; frame < 2; ++frame) {
                        const auto K_VISIBLE = renderer->draw(camera);
                        require(K_VISIBLE.has_value(),
                                K_VISIBLE ? "" : K_VISIBLE.error().m_message);
                        SDL_PumpEvents();
                    }
                    require(SDL_SetWindowFullscreen(window.get(), true), SDL_GetError());
                    for (int attempt { 0 }; attempt < 200 && (SDL_GetWindowFlags(window.get()) &
                                                            SDL_WINDOW_FULLSCREEN) == 0;
                         ++attempt) {
                        SDL_PumpEvents();
                        SDL_Delay(5);
                    }
                }
                if (std::string_view(phase) == "restored") {
                    require(SDL_SetWindowFullscreen(window.get(), false), SDL_GetError());
                    require(SDL_SyncWindow(window.get()), SDL_GetError());
                    require(SDL_HideWindow(window.get()), SDL_GetError());
                    require(SDL_SetWindowSize(window.get(), 1280, 720), SDL_GetError());
                }
                require(SDL_SyncWindow(window.get()), SDL_GetError());
                int actual_width {}, actual_height {};
                require(SDL_GetWindowSizeInPixels(window.get(), &actual_width, &actual_height),
                        SDL_GetError());
                record["window_flags"] = SDL_GetWindowFlags(window.get());
                record["video_driver"] = SDL_GetCurrentVideoDriver();
                record["width"] = actual_width;
                record["height"] = actual_height;
                if (std::string_view(phase) == "windowed") {
                    require(actual_width == 1280 && actual_height == 720,
                            "initial windowed extent changed");
                }
                if (std::string_view(phase) == "restored") {
                    require((SDL_GetWindowFlags(window.get()) & SDL_WINDOW_FULLSCREEN) == 0,
                            "windowed state was not restored");
                }
                if (std::string_view(phase) == "4k") {
                    require(actual_width == 3840 && actual_height == 2160,
                            "4K extent was not observed");
                }
                if (std::string_view(phase) == "fullscreen") {
                    require((SDL_GetWindowFlags(window.get()) & SDL_WINDOW_FULLSCREEN) != 0,
                            "native fullscreen flag absent");
                }
                std::vector<double> times;
                std::map<std::uint64_t, RendererFrameTiming> timing_samples;
                std::size_t peak_owned = 0;
                std::uint64_t measured_start_frames = 0;
                auto measured_begin = std::chrono::steady_clock::now();
                const auto K_COLLECT_SAMPLES = [&](const RendererDiagnostics& stats) {
                    if (!timings) {
                        return;
                    }
                    for (const auto& sample : stats.m_completed_timings) {
                        if (sample.m_sequence > measured_start_frames &&
                            sample.m_sequence <= measured_start_frames + 24) {
                            require(sample.m_width == static_cast<std::uint32_t>(actual_width) &&
                                        sample.m_height ==
                                            static_cast<std::uint32_t>(actual_height),
                                    "timing sample belongs to another framebuffer extent");
                            require(
                                sample.m_gpu_valid ==
                                    renderer->capabilities().m_gpu_timestamps_supported,
                                "GPU timestamp availability differs from reported capabilities");
                            timing_samples.try_emplace(sample.m_sequence, sample);
                        }
                    }
                };
                for (int frame { 0 }; frame < 32; ++frame) {
                    SDL_PumpEvents();
                    camera.m_position = spectator.m_position;
                    camera.m_right = spectator.right();
                    camera.m_down = spectator.down();
                    camera.m_forward = spectator.forward();
                    if (frame == 8) {
                        measured_start_frames = renderer->diagnostics().m_submitted_frames;
                        measured_begin = std::chrono::steady_clock::now();
                    }
                    const auto K_BEGIN = std::chrono::steady_clock::now();
                    const auto K_DRAWN = renderer->draw(camera);
                    require(K_DRAWN.has_value(), K_DRAWN ? "" : K_DRAWN.error().m_message);
                    if (frame >= 8) {
                        times.push_back(std::chrono::duration<double, std::milli>(
                                            std::chrono::steady_clock::now() - K_BEGIN)
                                            .count());
                    }
                    // Deterministic small yaw motion exercises rebuilding the index.
                    spectator.m_yaw += frame % 2 ? -0.004 : 0.004;
                    const auto K_STATS = renderer->diagnostics();
                    if (frame >= 8) {
                        K_COLLECT_SAMPLES(K_STATS);
                    }
                    peak_owned = std::max(peak_owned, K_STATS.m_allocated_bytes);
                    require(K_STATS.m_allocated_bytes <= options.m_residency_bytes,
                            "owned allocation budget exceeded");
                }
                require(renderer->wait_for_idle().has_value(), "GPU completion failed");
                const auto K_SECONDS =
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - measured_begin)
                        .count();
                const auto K_STATS = renderer->diagnostics();
                K_COLLECT_SAMPLES(K_STATS);
                const auto K_MEASURED_FRAMES = K_STATS.m_submitted_frames - measured_start_frames;
                require(K_MEASURED_FRAMES == 24, "measured draws did not all submit render frames");
                if (timings) {
                    require(timing_samples.size() == K_MEASURED_FRAMES,
                            "completed GPU/CPU timing samples were lost");
                    record["timings"] = timing_summary(timing_samples);
                }
                require(K_STATS.m_validation_errors == 0, "Vulkan validation failure");
                require(SDL_GetWindowSizeInPixels(window.get(), &actual_width, &actual_height),
                        SDL_GetError());
                require(K_STATS.m_framebuffer_width == static_cast<std::uint32_t>(actual_width) &&
                            K_STATS.m_framebuffer_height ==
                                static_cast<std::uint32_t>(actual_height),
                        "renderer extent does not match actual window pixels");
                require(K_STATS.m_resident_splat_count == expected, "real scene was trimmed");
                require(renderer->updates().visible_snapshot() != nullptr,
                        "scene never became visible");
                record.update({ { "width", K_STATS.m_framebuffer_width },
                               { "height", K_STATS.m_framebuffer_height },
                               { "resident_splats", K_STATS.m_resident_splat_count },
                               { "tile_references", K_STATS.m_tile_reference_count },
                               { "measured_render_frames", K_MEASURED_FRAMES },
                               { "peak_reported_owned_bytes", peak_owned },
                               { "cpu_draw_ms_p50", percentile(times, 0.50) },
                               { "cpu_draw_ms_p95", percentile(times, 0.95) },
                               { "cpu_draw_ms_p99", percentile(times, 0.99) },
                               { "completed_render_frames_per_second", 24 / K_SECONDS },
                               { "validation_errors", K_STATS.m_validation_errors },
                               { "passed", true } });
                if (std::string_view(phase) == "4k") {
                    const std::filesystem::path K_REPORT_PATH(argv[2]);
                    const auto K_SCREENSHOT =
                        K_REPORT_PATH.parent_path() / (K_REPORT_PATH.stem().string() + "-4k.ppm");
                    require(renderer->request_screenshot(K_SCREENSHOT).has_value(),
                            "4K screenshot unsupported");
                    const auto K_CAPTURED = renderer->draw(camera);
                    require(K_CAPTURED.has_value(), K_CAPTURED ? "" : K_CAPTURED.error().m_message);
                    require(std::filesystem::exists(K_SCREENSHOT), "4K readback failed");
                }
            } catch (const std::exception& exception) {
                record["passed"] = false;
                record["error"] = exception.what();
                report["cases"].push_back(record);
                throw;
            }
            report["cases"].push_back(record);
            std::cout << record.dump() << '\n';
        }
        report["passed"] = true;
    } catch (const std::exception& exception) {
        report["passed"] = false;
        report["error"] = exception.what();
    }
    {
        std::ofstream stream(argv[2]);
        stream << report.dump(2) << '\n';
        if (!stream) {
            return 3;
        }
    }
    std::cout << report.dump() << '\n';
    return report["passed"].get<bool>() ? 0 : 2;
} catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 2;
}
