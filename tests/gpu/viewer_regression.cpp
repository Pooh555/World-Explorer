#include "world_explorer/application/scenes.hpp"
#include "world_explorer/render_vulkan/renderer.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <span>

using namespace world_explorer;
namespace {
using json = nlohmann::json;
void require(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
double percentile(std::vector<double> values, double fraction) {
    require(!values.empty(), "cannot summarize absent timing samples");
    std::ranges::sort(values);
    return values[static_cast<std::size_t>(fraction * static_cast<double>(values.size() - 1))];
}
json timing_summary(const std::map<std::uint64_t, renderer_frame_timing>& samples) {
    json output{{"samples", json::array()}, {"metrics", json::object()}};
    std::map<std::string, std::vector<double>> metrics;
    constexpr std::array<const char*, GPU_TIMING_STAGE_COUNT> STAGES{
        "projection", "depth_sort", "count_scan", "tile_index", "composition", "presentation_draw"};
    for (const auto& [sequence, sample] : samples) {
        json row{{"sequence", sequence},
                 {"tile_sort_passes", sample.tile_sort_passes},
                 {"reused_tile_regions", sample.reused_tile_regions},
                 {"width", sample.width},
                 {"height", sample.height},
                 {"gpu_valid", sample.gpu_valid},
                 {"count_submissions", sample.count_submissions},
                 {"tile_batches", sample.tile_batches},
                 {"cpu_frame_wait_ms", sample.cpu_frame_wait_ms},
                 {"cpu_count_wait_ms", sample.cpu_count_wait_ms},
                 {"cpu_acquire_ms", sample.cpu_acquire_ms},
                 {"cpu_present_ms", sample.cpu_present_ms},
                 {"gpu_ms", nullptr}};
        for (const auto* metric :
             {"cpu_frame_wait_ms", "cpu_count_wait_ms", "cpu_acquire_ms", "cpu_present_ms"})
            metrics[metric].push_back(row.at(metric).get<double>());
        if (sample.gpu_valid) {
            row["gpu_ms"] = json::object();
            double total = 0;
            for (std::size_t index = 0; index < STAGES.size(); ++index) {
                row["gpu_ms"][STAGES[index]] = sample.gpu_ms[index];
                metrics["gpu_" + std::string(STAGES[index]) + "_ms"].push_back(
                    sample.gpu_ms[index]);
                total += sample.gpu_ms[index];
            }
            metrics["gpu_measured_stages_ms"].push_back(total);
        }
        metrics["count_submissions"].push_back(static_cast<double>(sample.count_submissions));
        metrics["reused_tile_regions"].push_back(static_cast<double>(sample.reused_tile_regions));
        metrics["tile_batches"].push_back(static_cast<double>(sample.tile_batches));
        output["samples"].push_back(std::move(row));
    }
    for (const auto& [name, values] : metrics)
        output["metrics"][name] = {{"p50", percentile(values, 0.5)},
                                   {"p95", percentile(values, 0.95)},
                                   {"p99", percentile(values, 0.99)}};
    return output;
}
} // namespace
int main(int argc, char** argv) try {
    if (argc < 3 || argc > 5) {
        std::cerr << "usage: we_gpu_viewer_regression <scene> <new report> [--timings] "
                     "[--fresh-tile-plan]\n";
        return 2;
    }
    bool timings = false, reuse_tile_plans = true;
    for (const auto* argument : std::span(argv + 3, static_cast<std::size_t>(argc - 3))) {
        if (std::string_view(argument) == "--timings")
            timings = true;
        else if (std::string_view(argument) == "--fresh-tile-plan")
            reuse_tile_plans = false;
        else
            throw std::runtime_error("unknown regression option");
    }
    require(!std::filesystem::exists(argv[2]), "report already exists");
    json report{{"scope", "local real-scene regression; not P2/P3 certification"},
                {"present_mode", "FIFO"},
                {"warmup_frames", 8},
                {"measured_frames", 24},
                {"timing_collection", timings},
                {"reuse_tile_plans", reuse_tile_plans},
                {"cases", json::array()}};
    try {
        require(!std::filesystem::exists(argv[2]), "report already exists");
        const auto scene = load_bundle(argv[1]);
        require(scene.has_value(), scene ? "" : scene.error().message);
        const auto inspection = source_view(**scene);
        require(inspection.has_value(), inspection ? "" : inspection.error().message);
        const bool initialized = SDL_Init(SDL_INIT_VIDEO);
        require(initialized, SDL_GetError());
        struct sdl_guard {
            ~sdl_guard() { SDL_Quit(); }
        } sdl;
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("World Explorer fullscreen regression", 1280, 720,
                             SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN),
            SDL_DestroyWindow);
        require(window != nullptr, SDL_GetError());
        renderer_options options;
        options.max_splats = 1000000;
        options.collect_timings = timings;
        options.reuse_tile_plans = reuse_tile_plans;
        options.shader_directory = WE_SHADER_DIRECTORY;
        auto created = vulkan_renderer::create(window.get(), options);
        require(created.has_value(), created ? "" : created.error().message);
        auto renderer = std::move(*created);
        require(renderer->capabilities().validation_enabled, "validation required");
        report["device"] = renderer->capabilities().device_name;
        report["gpu_timestamps_supported"] = renderer->capabilities().gpu_timestamps_supported;
        report["timestamp_valid_bits"] = renderer->capabilities().timestamp_valid_bits;
        report["timestamp_period_ns"] = renderer->capabilities().timestamp_period_ns;
        report["timestamp_queries"] = timings && renderer->capabilities().gpu_timestamps_supported
                                          ? GPU_TIMING_STAGE_COUNT * 4
                                          : 0;
        report["max_splats"] = options.max_splats;
        report["residency_bytes"] = options.residency_bytes;
        report["scene_content_sha256"] = scene_content_hash(**scene);
        auto batch = make_snapshot_batch(*scene, {1}).value();
        const auto ticket = renderer->updates().enqueue(batch);
        require(ticket.has_value(), ticket ? "" : ticket.error().message);
        auto spectator = inspection->camera;
        camera_view camera;
        camera.vertical_fov_radians = static_cast<float>(inspection->vertical_fov_radians);
        std::size_t expected = 0;
        for (const auto& [id, chunk] : (*scene)->chunks) {
            static_cast<void>(id);
            expected += chunk->splats.size();
        }
        report["scene_splats"] = expected;
        for (const auto* phase : {"windowed", "4k", "fullscreen", "restored"}) {
            auto record = json{{"phase", phase}};
            try {
                if (std::string_view(phase) == "4k")
                    require(SDL_SetWindowSize(window.get(), 3840, 2160), SDL_GetError());
                if (std::string_view(phase) == "fullscreen") {
                    require(SDL_ShowWindow(window.get()), SDL_GetError());
                    require(SDL_SyncWindow(window.get()), SDL_GetError());
                    SDL_PumpEvents();
                    // Wayland maps a shown surface only after a buffer is committed.
                    for (int frame = 0; frame < 2; ++frame) {
                        const auto visible = renderer->draw(camera);
                        require(visible.has_value(), visible ? "" : visible.error().message);
                        SDL_PumpEvents();
                    }
                    require(SDL_SetWindowFullscreen(window.get(), true), SDL_GetError());
                    for (int attempt = 0; attempt < 200 && (SDL_GetWindowFlags(window.get()) &
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
                int actual_width{}, actual_height{};
                require(SDL_GetWindowSizeInPixels(window.get(), &actual_width, &actual_height),
                        SDL_GetError());
                record["window_flags"] = SDL_GetWindowFlags(window.get());
                record["video_driver"] = SDL_GetCurrentVideoDriver();
                record["width"] = actual_width;
                record["height"] = actual_height;
                if (std::string_view(phase) == "windowed")
                    require(actual_width == 1280 && actual_height == 720,
                            "initial windowed extent changed");
                if (std::string_view(phase) == "restored")
                    require((SDL_GetWindowFlags(window.get()) & SDL_WINDOW_FULLSCREEN) == 0,
                            "windowed state was not restored");
                if (std::string_view(phase) == "4k")
                    require(actual_width == 3840 && actual_height == 2160,
                            "4K extent was not observed");
                if (std::string_view(phase) == "fullscreen")
                    require((SDL_GetWindowFlags(window.get()) & SDL_WINDOW_FULLSCREEN) != 0,
                            "native fullscreen flag absent");
                std::vector<double> times;
                std::map<std::uint64_t, renderer_frame_timing> timing_samples;
                std::size_t peak_owned = 0;
                std::uint64_t measured_start_frames = 0;
                auto measured_begin = std::chrono::steady_clock::now();
                const auto collect_samples = [&](const renderer_diagnostics& stats) {
                    if (!timings)
                        return;
                    for (const auto& sample : stats.completed_timings)
                        if (sample.sequence > measured_start_frames &&
                            sample.sequence <= measured_start_frames + 24) {
                            require(sample.width == static_cast<std::uint32_t>(actual_width) &&
                                        sample.height == static_cast<std::uint32_t>(actual_height),
                                    "timing sample belongs to another framebuffer extent");
                            require(
                                sample.gpu_valid ==
                                    renderer->capabilities().gpu_timestamps_supported,
                                "GPU timestamp availability differs from reported capabilities");
                            timing_samples.try_emplace(sample.sequence, sample);
                        }
                };
                for (int frame = 0; frame < 32; ++frame) {
                    SDL_PumpEvents();
                    camera.position = spectator.position;
                    camera.right = spectator.right();
                    camera.down = spectator.down();
                    camera.forward = spectator.forward();
                    if (frame == 8) {
                        measured_start_frames = renderer->diagnostics().submitted_frames;
                        measured_begin = std::chrono::steady_clock::now();
                    }
                    const auto begin = std::chrono::steady_clock::now();
                    const auto drawn = renderer->draw(camera);
                    require(drawn.has_value(), drawn ? "" : drawn.error().message);
                    if (frame >= 8)
                        times.push_back(std::chrono::duration<double, std::milli>(
                                            std::chrono::steady_clock::now() - begin)
                                            .count());
                    // Deterministic small yaw motion exercises rebuilding the index.
                    spectator.yaw += frame % 2 ? -0.004 : 0.004;
                    const auto stats = renderer->diagnostics();
                    if (frame >= 8)
                        collect_samples(stats);
                    peak_owned = std::max(peak_owned, stats.allocated_bytes);
                    require(stats.allocated_bytes <= options.residency_bytes,
                            "owned allocation budget exceeded");
                }
                require(renderer->wait_for_idle().has_value(), "GPU completion failed");
                const auto seconds =
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - measured_begin)
                        .count();
                const auto stats = renderer->diagnostics();
                collect_samples(stats);
                const auto measured_frames = stats.submitted_frames - measured_start_frames;
                require(measured_frames == 24, "measured draws did not all submit render frames");
                if (timings) {
                    require(timing_samples.size() == measured_frames,
                            "completed GPU/CPU timing samples were lost");
                    record["timings"] = timing_summary(timing_samples);
                }
                require(stats.validation_errors == 0, "Vulkan validation failure");
                require(SDL_GetWindowSizeInPixels(window.get(), &actual_width, &actual_height),
                        SDL_GetError());
                require(stats.framebuffer_width == static_cast<std::uint32_t>(actual_width) &&
                            stats.framebuffer_height == static_cast<std::uint32_t>(actual_height),
                        "renderer extent does not match actual window pixels");
                require(stats.resident_splat_count == expected, "real scene was trimmed");
                require(renderer->updates().visible_snapshot() != nullptr,
                        "scene never became visible");
                record.update({{"width", stats.framebuffer_width},
                               {"height", stats.framebuffer_height},
                               {"resident_splats", stats.resident_splat_count},
                               {"tile_references", stats.tile_reference_count},
                               {"measured_render_frames", measured_frames},
                               {"peak_reported_owned_bytes", peak_owned},
                               {"cpu_draw_ms_p50", percentile(times, 0.50)},
                               {"cpu_draw_ms_p95", percentile(times, 0.95)},
                               {"cpu_draw_ms_p99", percentile(times, 0.99)},
                               {"completed_render_frames_per_second", 24 / seconds},
                               {"validation_errors", stats.validation_errors},
                               {"passed", true}});
                if (std::string_view(phase) == "4k") {
                    const std::filesystem::path report_path(argv[2]);
                    const auto screenshot =
                        report_path.parent_path() / (report_path.stem().string() + "-4k.ppm");
                    require(renderer->request_screenshot(screenshot).has_value(),
                            "4K screenshot unsupported");
                    const auto captured = renderer->draw(camera);
                    require(captured.has_value(), captured ? "" : captured.error().message);
                    require(std::filesystem::exists(screenshot), "4K readback failed");
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
        if (!stream)
            return 3;
    }
    std::cout << report.dump() << '\n';
    return report["passed"].get<bool>() ? 0 : 2;
} catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 2;
}
