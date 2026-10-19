#include "world_explorer/application/scenes.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/render_vulkan/renderer.hpp"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace world_explorer;

namespace {

using Json = nlohmann::json;
using SteadyClock = std::chrono::steady_clock;
using Seconds = std::chrono::seconds;

void require(bool valid, const std::string& message) {
    if (!valid) {
        throw std::runtime_error(message);
    }
}

std::optional<std::size_t> process_status_bytes(std::string_view field) {
#if defined(__linux__)
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.starts_with(field)) {
            std::istringstream entry(line);
            std::string key, unit;
            std::size_t value {};
            if (entry >> key >> value >> unit && unit == "kB" &&
                value <= std::numeric_limits<std::size_t>::max() / 1024) {
                return value * 1024;
            }
            return {};
        }
    }
#else
    static_cast<void>(field);
#endif
    return {};
}

void write_event(std::ofstream& stream, const Json& event) {
    stream << event.dump() << '\n';
    stream.flush();
    require(static_cast<bool>(stream), "stress event log write failed");
}

bool full_revision(std::uint64_t revision, bool resident_mode) {
    return resident_mode ? revision % 3 != 0 : revision % 2 != 0;
}

SceneSnapshot
revision_of(const SceneSnapshot& original, std::uint64_t revision, bool resident_mode) {
    auto copy = std::make_shared<SceneState>(*original);
    copy->m_revision = { revision };
    if (!full_revision(revision, resident_mode)) {
        copy->m_chunks.clear();
    }
    return copy;
}

CameraView source_camera(const SceneState& scene) {
    const auto K_VIEW = source_view(scene);
    require(K_VIEW.has_value(), K_VIEW ? "" : K_VIEW.error().m_message);
    CameraView camera;
    camera.m_position = K_VIEW->m_camera.m_position;
    camera.m_right = K_VIEW->m_camera.right();
    camera.m_down = K_VIEW->m_camera.down();
    camera.m_forward = K_VIEW->m_camera.forward();
    camera.m_vertical_fov_radians = K_VIEW->m_vertical_fov_radians;
    return camera;
}

void set_extent(SDL_Window* window, std::uint32_t width, std::uint32_t height) {
    require(SDL_SetWindowSize(window, static_cast<int>(width), static_cast<int>(height)),
            SDL_GetError());
    require(SDL_SyncWindow(window), SDL_GetError());
    int actual_width {}, actual_height {};
    require(SDL_GetWindowSizeInPixels(window, &actual_width, &actual_height), SDL_GetError());
    require(actual_width == static_cast<int>(width) && actual_height == static_cast<int>(height),
            "compositor did not apply requested stress extent");
}

struct StressContext {
    VulkanRenderer& m_renderer;
    SDL_Window* m_window;
    const CameraView& m_camera;
    std::size_t m_residency_bytes;
    std::size_t m_full_splats;
    std::size_t& m_peak_owned_bytes;
    std::uint64_t& m_frames;
    bool m_resident_mode {};
    std::uint64_t m_last_timing {};
    std::size_t m_host_copy_bytes {}, m_resident_copy_bytes {};
};

void check_frame(StressContext& context) {
    SDL_PumpEvents();
    const auto K_DRAWN = context.m_renderer.draw(context.m_camera);
    require(K_DRAWN.has_value(), K_DRAWN ? "" : K_DRAWN.error().m_message);
    const auto K_STATS = context.m_renderer.diagnostics();
    require(K_STATS.m_validation_errors == 0, "Vulkan validation error during stress");
    require(K_STATS.m_allocated_bytes <= context.m_residency_bytes,
            "renderer owned-byte budget exceeded");
    const auto K_VISIBLE = context.m_renderer.updates().visible_snapshot();
    if (K_VISIBLE) {
        require(K_STATS.m_resident_splat_count ==
                    (full_revision(K_VISIBLE->m_revision.m_value, context.m_resident_mode)
                         ? context.m_full_splats
                         : 0),
                "resident splat count differs from visible revision");
    }
    context.m_peak_owned_bytes = std::max(context.m_peak_owned_bytes, K_STATS.m_allocated_bytes);
    context.m_frames = K_STATS.m_submitted_frames;
    auto latest = context.m_last_timing;
    for (const auto& sample : K_STATS.m_completed_timings) {
        if (sample.m_sequence > context.m_last_timing) {
            context.m_host_copy_bytes += sample.m_host_copy_bytes;
            context.m_resident_copy_bytes += sample.m_resident_copy_bytes;
            latest = std::max(latest, sample.m_sequence);
        }
    }
    context.m_last_timing = latest;
}

void wait_for_revision(StressContext& context, UpdateTicket ticket) {
    const auto K_DEADLINE = SteadyClock::now() + Seconds(60);
    while (SteadyClock::now() < K_DEADLINE) {
        check_frame(context);
        const auto K_PROGRESS = context.m_renderer.updates().query_progress(ticket);
        require(K_PROGRESS.has_value(), K_PROGRESS ? "" : K_PROGRESS.error().m_message);
        if (K_PROGRESS->m_state == UpdateState::RENDER_VISIBLE) {
            return;
        }
        require(K_PROGRESS->m_state != UpdateState::FAILED &&
                    K_PROGRESS->m_state != UpdateState::CANCELLED,
                "stress scene update failed or was cancelled");
        SDL_Delay(20);
    }
    throw std::runtime_error("stress scene revision did not become visible within 60 seconds");
}

std::string capture(StressContext& context, const std::filesystem::path& path) {
    set_extent(context.m_window, 1280, 720);
    const auto K_REQUESTED = context.m_renderer.request_screenshot(path);
    require(K_REQUESTED.has_value(), K_REQUESTED ? "" : K_REQUESTED.error().m_message);
    check_frame(context);
    require(std::filesystem::is_regular_file(path), "stress screenshot missing");
    const auto K_HASH = sha256_file(path, 4uz * 1024 * 1024);
    require(K_HASH.has_value(), K_HASH ? "" : K_HASH.error().m_message);
    return *K_HASH;
}

}  // namespace

int main(int argc, char** argv) try {
    require(argc >= 4 && argc <= 6,
            "usage: we_gpu_lifetime_stress <retained-scene> <new-output-dir> <seconds> "
            "[--resident-reuse] [--fp16-sh]");
    const std::span<char*> K_ARGUMENTS(argv, static_cast<std::size_t>(argc));
    bool resident_mode = false, packed = false;
    for (const auto* argument : K_ARGUMENTS.subspan(4)) {
        const std::string_view K_OPTION(argument);
        if (K_OPTION == "--resident-reuse" && !resident_mode) {
            resident_mode = true;
        } else if (K_OPTION == "--fp16-sh" && !packed) {
            packed = true;
        } else {
            throw std::runtime_error("unknown or duplicate stress option");
        }
    }
    const std::string_view K_DURATION_TEXT(K_ARGUMENTS[3]);
    std::uint64_t duration {};
    const auto K_PARSED = std::from_chars(
        K_DURATION_TEXT.data(), K_DURATION_TEXT.data() + K_DURATION_TEXT.size(), duration);
    require(K_PARSED.ec == std::errc {} &&
                K_PARSED.ptr == K_DURATION_TEXT.data() + K_DURATION_TEXT.size(),
            "stress duration must be an integer");
    require(duration >= 10 && duration <= 7200, "stress duration must be 10–7200 seconds");
    const std::filesystem::path K_OUTPUT(argv[2]);
    require(!std::filesystem::exists(K_OUTPUT), "stress output directory already exists");
    require(std::filesystem::exists(K_OUTPUT.parent_path()),
            "stress output parent directory is missing");
    const auto K_SCENE = load_bundle(argv[1]);
    require(K_SCENE.has_value(), K_SCENE ? "" : K_SCENE.error().m_message);
    const auto& original = *K_SCENE;
    const auto K_CAMERA = source_camera(*original);
    const auto K_SCENE_HASH = scene_content_hash(*original);
    std::size_t full_splats {};
    for (const auto& [K_ID, K_CHUNK] : original->m_chunks) {
        static_cast<void>(K_ID);
        full_splats += K_CHUNK->m_splats.size();
    }
    require(full_splats > 0 && full_splats <= 1000000,
            "stress requires a nonempty scene of at most one million splats");
    require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());

    struct SdlOwner {
        ~SdlOwner() {
            SDL_Quit();
        }
    } sdl;

    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("World Explorer lifetime stress",
                         1280,
                         720,
                         SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN),
        SDL_DestroyWindow);
    require(window != nullptr, SDL_GetError());
    RendererOptions options;
    if (packed) {
        options.m_splat_storage = GpuSplatStorage::FP32_GEOMETRY_FP16_SH;
    }
    options.m_max_splats = 1000000;
    options.m_shader_directory = WE_SHADER_DIRECTORY;
    options.m_collect_timings = resident_mode;
    auto created = VulkanRenderer::create(window.get(), options);
    require(created.has_value(), created ? "" : created.error().m_message);
    auto renderer = std::move(*created);
    require(renderer->capabilities().m_validation_enabled, "stress requires Vulkan validation");
    const auto K_STRIDE = renderer->capabilities().m_splat_stride_bytes;
    require(K_STRIDE == (packed ? 160U : 256U), "stress storage stride differs from selection");
    const auto K_PAYLOAD_BYTES = full_splats * K_STRIDE;
    std::filesystem::create_directory(K_OUTPUT);
    std::ofstream events(K_OUTPUT / "events.jsonl", std::ios::binary);
    require(static_cast<bool>(events), "cannot open stress event log");
    std::size_t peak_owned_bytes {};
    std::uint64_t frames {}, revision = 1, updates {}, resizes {}, captures {};
    StressContext context { *renderer,
                           window.get(),
                           K_CAMERA,
                           options.m_residency_bytes,
                           full_splats,
                           peak_owned_bytes,
                           frames,
                           resident_mode };
    auto initial = make_snapshot_batch(revision_of(original, revision, resident_mode), { revision });
    require(initial.has_value(), initial ? "" : initial.error().m_message);
    const auto K_INITIAL_TICKET = renderer->updates().enqueue(*initial);
    require(K_INITIAL_TICKET.has_value(),
            K_INITIAL_TICKET ? "" : K_INITIAL_TICKET.error().m_message);
    wait_for_revision(context, *K_INITIAL_TICKET);
    const auto K_START = SteadyClock::now();
    const auto K_BASELINE = capture(context, K_OUTPUT / "capture-0.ppm");
    ++captures;
    write_event(events,
                { { "event", "start" },
                 { "scene_content_sha256", K_SCENE_HASH },
                 { "scene_splats", full_splats },
                 { "gpu_storage", packed ? "fp16_sh" : "fp32" },
                 { "splat_stride_bytes", K_STRIDE },
                 { "scene_payload_bytes", K_PAYLOAD_BYTES },
                 { "device", renderer->capabilities().m_device_name },
                 { "duration_seconds", duration },
                 { "update_mode", resident_mode ? "full_full_empty" : "full_empty" },
                 { "process_rss_bytes", process_status_bytes("VmRSS:") },
                 { "baseline_readback_sha256", K_BASELINE } });

    struct Extent {
        std::uint32_t m_width, m_height;
    };

    const std::array<Extent, 4> K_EXTENTS { { { 1024, 640 }, { 1600, 900 }, { 1920, 1080 }, { 1280, 720 } } };
    std::size_t extent_index {};
    auto next_update = K_START + Seconds(15);
    auto next_resize = K_START + Seconds(20);
    auto next_capture = K_START + Seconds(600);
    auto next_sample = K_START + Seconds(60);
    std::optional<UpdateTicket> pending;
    while (SteadyClock::now() - K_START < Seconds(duration)) {
        const auto K_NOW = SteadyClock::now();
        if (pending) {
            const auto K_PROGRESS = renderer->updates().query_progress(*pending);
            require(K_PROGRESS.has_value(), K_PROGRESS ? "" : K_PROGRESS.error().m_message);
            require(K_PROGRESS->m_state != UpdateState::FAILED &&
                        K_PROGRESS->m_state != UpdateState::CANCELLED,
                    "stress scene update failed or was cancelled");
            if (K_PROGRESS->m_state == UpdateState::RENDER_VISIBLE) {
                pending.reset();
                ++updates;
                write_event(
                    events,
                    { { "event", "revision" },
                     { "elapsed_seconds", std::chrono::duration<double>(K_NOW - K_START).count() },
                     { "revision", revision },
                     { "splats", full_revision(revision, resident_mode) ? full_splats : 0 } });
            }
        }
        if (!pending && K_NOW >= next_update) {
            const auto K_NEXT_REVISION = revision + 1;
            auto batch = make_snapshot_batch(revision_of(original, K_NEXT_REVISION, resident_mode),
                                             OperationId { K_NEXT_REVISION });
            require(batch.has_value(), batch ? "" : batch.error().m_message);
            batch->m_full_snapshot = false;
            batch->m_base_revision = { revision };
            auto admitted = renderer->updates().enqueue(*batch);
            if (admitted) {
                pending = *admitted;
                revision = K_NEXT_REVISION;
                next_update = K_NOW + Seconds(15);
            } else {
                require(admitted.error().m_code == ErrorCode::WOULD_BLOCK,
                        "stress update admission failed: " + admitted.error().m_message);
            }
        }
        if (K_NOW >= next_resize) {
            const auto K_EXTENT = K_EXTENTS[extent_index++ % K_EXTENTS.size()];
            set_extent(window.get(), K_EXTENT.m_width, K_EXTENT.m_height);
            ++resizes;
            next_resize = K_NOW + Seconds(20);
            write_event(
                events,
                { { "event", "resize" },
                 { "elapsed_seconds", std::chrono::duration<double>(K_NOW - K_START).count() },
                 { "width", K_EXTENT.m_width },
                 { "height", K_EXTENT.m_height } });
        }
        const bool K_TAKE_CAPTURE =
            K_NOW >= next_capture && !pending && full_revision(revision, resident_mode);
        if (K_TAKE_CAPTURE) {
            const auto K_PATH = K_OUTPUT / ("capture-" + std::to_string(captures) + ".ppm");
            const auto K_HASH = capture(context, K_PATH);
            require(K_HASH == K_BASELINE, "stress full-scene readback differs from baseline");
            ++captures;
            next_capture = K_NOW + Seconds(600);
            write_event(
                events,
                { { "event", "capture" },
                 { "elapsed_seconds", std::chrono::duration<double>(K_NOW - K_START).count() },
                 { "revision", revision },
                 { "sha256", K_HASH } });
        } else {
            check_frame(context);
        }
        if (K_NOW >= next_sample) {
            const auto K_STATS = renderer->diagnostics();
            write_event(
                events,
                { { "event", "sample" },
                 { "elapsed_seconds", std::chrono::duration<double>(K_NOW - K_START).count() },
                 { "revision", renderer->updates().visible_snapshot()->m_revision.m_value },
                 { "submitted_frames", K_STATS.m_submitted_frames },
                 { "completed_timeline", K_STATS.m_completed_timeline },
                 { "owned_bytes", K_STATS.m_allocated_bytes },
                 { "peak_owned_bytes", peak_owned_bytes },
                 { "host_copy_bytes", context.m_host_copy_bytes },
                 { "resident_copy_bytes", context.m_resident_copy_bytes },
                 { "process_rss_bytes", process_status_bytes("VmRSS:") },
                 { "process_peak_rss_bytes", process_status_bytes("VmHWM:") },
                 { "validation_errors", K_STATS.m_validation_errors } });
            next_sample = K_NOW + Seconds(60);
        }
        SDL_Delay(20);
    }
    if (pending) {
        wait_for_revision(context, *pending);
        ++updates;
        write_event(events,
                    { { "event", "revision" },
                     { "elapsed_seconds",
                      std::chrono::duration<double>(SteadyClock::now() - K_START).count() },
                     { "revision", revision },
                     { "splats", full_revision(revision, resident_mode) ? full_splats : 0 },
                     { "awaited_after_duration", true } });
    }
    if (!full_revision(revision, resident_mode)) {
        auto final = make_snapshot_batch(revision_of(original, revision + 1, resident_mode),
                                         OperationId { revision + 1 });
        require(final.has_value(), final ? "" : final.error().m_message);
        final->m_full_snapshot = false;
        final->m_base_revision = { revision };
        const auto K_ADMITTED = renderer->updates().enqueue(*final);
        require(K_ADMITTED.has_value(), K_ADMITTED ? "" : K_ADMITTED.error().m_message);
        ++revision;
        wait_for_revision(context, *K_ADMITTED);
        ++updates;
    }
    const auto K_FINAL_HASH = capture(context, K_OUTPUT / "capture-final.ppm");
    require(K_FINAL_HASH == K_BASELINE, "final full-scene readback differs from baseline");
    ++captures;
    const auto K_IDLE = renderer->wait_for_idle();
    require(K_IDLE.has_value(), K_IDLE ? "" : K_IDLE.error().m_message);
    const auto K_STATS = renderer->diagnostics();
    require(K_STATS.m_validation_errors == 0, "Vulkan validation error at stress completion");
    require(updates + 1 == revision, "stress visible revision/update counts disagree");
    require(!resident_mode || duration < 30 || context.m_resident_copy_bytes > 0,
            "resident stress did not observe a device-to-device copy");
    // Empty revisions still transfer the renderer's 16-byte zero sentinel.
    const auto K_EXPECTED_HOST_BYTES = ((revision + 2) / 3) * K_PAYLOAD_BYTES + (revision / 3) * 16;
    const auto K_EXPECTED_RESIDENT_BYTES = ((revision + 1) / 3) * K_PAYLOAD_BYTES;
    require(!resident_mode || (context.m_host_copy_bytes == K_EXPECTED_HOST_BYTES &&
                               context.m_resident_copy_bytes == K_EXPECTED_RESIDENT_BYTES),
            "stress transfer byte totals differ from the full/full/empty sequence");
    const auto K_ELAPSED = std::chrono::duration<double>(SteadyClock::now() - K_START).count();
    Json summary {
        { "passed", true },
        { "scope", "native read-only retained-scene update/resize stress" },
        { "elapsed_seconds", K_ELAPSED },
        { "requested_seconds", duration },
        { "update_mode", resident_mode ? "full_full_empty" : "full_empty" },
        { "scene_content_sha256", K_SCENE_HASH },
        { "scene_splats", full_splats },
        { "gpu_storage", packed ? "fp16_sh" : "fp32" },
        { "splat_stride_bytes", K_STRIDE },
        { "scene_payload_bytes", K_PAYLOAD_BYTES },
        { "device", renderer->capabilities().m_device_name },
        { "updates_visible", updates },
        { "last_visible_revision", renderer->updates().visible_snapshot()->m_revision.m_value },
        { "resizes", resizes },
        { "captures", captures },
        { "readback_sha256", K_BASELINE },
        { "submitted_frames", frames },
        { "peak_owned_bytes", peak_owned_bytes },
        { "host_copy_bytes", context.m_host_copy_bytes },
        { "resident_copy_bytes", context.m_resident_copy_bytes },
        { "process_rss_bytes", process_status_bytes("VmRSS:") },
        { "process_peak_rss_bytes", process_status_bytes("VmHWM:") },
        { "validation_errors", K_STATS.m_validation_errors } };
    std::ofstream result(K_OUTPUT / "summary.json", std::ios::binary);
    result << summary.dump(2) << '\n';
    require(static_cast<bool>(result), "stress summary write failed");
    write_event(events, { { "event", "complete" }, { "elapsed_seconds", K_ELAPSED } });
    std::cout << summary.dump() << '\n';
    return 0;
} catch (const std::exception& exception) {
    std::cerr << "GPU lifetime stress failed: " << exception.what() << '\n';
    return 2;
}
