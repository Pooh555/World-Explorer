#include "world_explorer/application/scenes.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/render_vulkan/renderer.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace world_explorer;
namespace {
using json = nlohmann::json;
using steady_clock = std::chrono::steady_clock;
using seconds = std::chrono::seconds;

void require(bool valid, const std::string& message) {
    if (!valid)
        throw std::runtime_error(message);
}

std::optional<std::size_t> process_status_bytes(std::string_view field) {
#if defined(__linux__)
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.starts_with(field)) {
            std::istringstream entry(line);
            std::string key, unit;
            std::size_t value{};
            if (entry >> key >> value >> unit && unit == "kB" &&
                value <= std::numeric_limits<std::size_t>::max() / 1024)
                return value * 1024;
            return {};
        }
    }
#else
    static_cast<void>(field);
#endif
    return {};
}

void write_event(std::ofstream& stream, const json& event) {
    stream << event.dump() << '\n';
    stream.flush();
    require(static_cast<bool>(stream), "stress event log write failed");
}

scene_snapshot revision_of(const scene_snapshot& original, std::uint64_t revision) {
    auto copy = std::make_shared<scene_state>(*original);
    copy->revision = {revision};
    if (revision % 2 == 0)
        copy->chunks.clear();
    return copy;
}

camera_view source_camera(const scene_state& scene) {
    const auto view = source_view(scene);
    require(view.has_value(), view ? "" : view.error().message);
    camera_view camera;
    camera.position = view->camera.position;
    camera.right = view->camera.right();
    camera.down = view->camera.down();
    camera.forward = view->camera.forward();
    camera.vertical_fov_radians = view->vertical_fov_radians;
    return camera;
}

void set_extent(SDL_Window* window, std::uint32_t width, std::uint32_t height) {
    require(SDL_SetWindowSize(window, static_cast<int>(width), static_cast<int>(height)),
            SDL_GetError());
    require(SDL_SyncWindow(window), SDL_GetError());
    int actual_width{}, actual_height{};
    require(SDL_GetWindowSizeInPixels(window, &actual_width, &actual_height), SDL_GetError());
    require(actual_width == static_cast<int>(width) && actual_height == static_cast<int>(height),
            "compositor did not apply requested stress extent");
}

struct stress_context {
    vulkan_renderer& renderer;
    SDL_Window* window;
    const camera_view& camera;
    std::size_t residency_bytes;
    std::size_t full_splats;
    std::size_t& peak_owned_bytes;
    std::uint64_t& frames;
};

void check_frame(stress_context& context) {
    SDL_PumpEvents();
    const auto drawn = context.renderer.draw(context.camera);
    require(drawn.has_value(), drawn ? "" : drawn.error().message);
    const auto stats = context.renderer.diagnostics();
    require(stats.validation_errors == 0, "Vulkan validation error during stress");
    require(stats.allocated_bytes <= context.residency_bytes,
            "renderer owned-byte budget exceeded");
    const auto visible = context.renderer.updates().visible_snapshot();
    if (visible)
        require(stats.resident_splat_count ==
                    (visible->revision.value % 2 ? context.full_splats : 0),
                "resident splat count differs from visible revision");
    context.peak_owned_bytes = std::max(context.peak_owned_bytes, stats.allocated_bytes);
    context.frames = stats.submitted_frames;
}

void wait_for_revision(stress_context& context, update_ticket ticket) {
    const auto deadline = steady_clock::now() + seconds(60);
    while (steady_clock::now() < deadline) {
        check_frame(context);
        const auto progress = context.renderer.updates().query_progress(ticket);
        require(progress.has_value(), progress ? "" : progress.error().message);
        if (progress->state == update_state::RENDER_VISIBLE)
            return;
        require(progress->state != update_state::FAILED &&
                    progress->state != update_state::CANCELLED,
                "stress scene update failed or was cancelled");
        SDL_Delay(20);
    }
    throw std::runtime_error("stress scene revision did not become visible within 60 seconds");
}

std::string capture(stress_context& context, const std::filesystem::path& path) {
    set_extent(context.window, 1280, 720);
    const auto requested = context.renderer.request_screenshot(path);
    require(requested.has_value(), requested ? "" : requested.error().message);
    check_frame(context);
    require(std::filesystem::is_regular_file(path), "stress screenshot missing");
    const auto hash = sha256_file(path, std::size_t{4} * 1024 * 1024);
    require(hash.has_value(), hash ? "" : hash.error().message);
    return *hash;
}
} // namespace

int main(int argc, char** argv) try {
    require(argc == 4, "usage: we_gpu_lifetime_stress <retained-scene> <new-output-dir> <seconds>");
    const auto duration = std::stoull(argv[3]);
    require(duration >= 10 && duration <= 7200, "stress duration must be 10–7200 seconds");
    const std::filesystem::path output(argv[2]);
    require(!std::filesystem::exists(output), "stress output directory already exists");
    require(std::filesystem::exists(output.parent_path()),
            "stress output parent directory is missing");
    const auto scene = load_bundle(argv[1]);
    require(scene.has_value(), scene ? "" : scene.error().message);
    const auto& original = *scene;
    const auto camera = source_camera(*original);
    const auto scene_hash = scene_content_hash(*original);
    std::size_t full_splats{};
    for (const auto& [id, chunk] : original->chunks) {
        static_cast<void>(id);
        full_splats += chunk->splats.size();
    }
    require(full_splats > 0 && full_splats <= 1000000,
            "stress requires a nonempty scene of at most one million splats");
    require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
    struct sdl_owner {
        ~sdl_owner() { SDL_Quit(); }
    } sdl;
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("World Explorer lifetime stress", 1280, 720,
                         SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN),
        SDL_DestroyWindow);
    require(window != nullptr, SDL_GetError());
    renderer_options options;
    options.max_splats = 1000000;
    options.shader_directory = WE_SHADER_DIRECTORY;
    auto created = vulkan_renderer::create(window.get(), options);
    require(created.has_value(), created ? "" : created.error().message);
    auto renderer = std::move(*created);
    require(renderer->capabilities().validation_enabled, "stress requires Vulkan validation");
    std::filesystem::create_directory(output);
    std::ofstream events(output / "events.jsonl", std::ios::binary);
    require(static_cast<bool>(events), "cannot open stress event log");
    std::size_t peak_owned_bytes{};
    std::uint64_t frames{}, revision = 1, updates{}, resizes{}, captures{};
    stress_context context{*renderer,   window.get(),     camera, options.residency_bytes,
                           full_splats, peak_owned_bytes, frames};
    auto initial = make_snapshot_batch(revision_of(original, revision), {revision});
    require(initial.has_value(), initial ? "" : initial.error().message);
    const auto initial_ticket = renderer->updates().enqueue(*initial);
    require(initial_ticket.has_value(), initial_ticket ? "" : initial_ticket.error().message);
    wait_for_revision(context, *initial_ticket);
    const auto start = steady_clock::now();
    const auto baseline = capture(context, output / "capture-0.ppm");
    ++captures;
    write_event(events, {{"event", "start"},
                         {"scene_content_sha256", scene_hash},
                         {"scene_splats", full_splats},
                         {"device", renderer->capabilities().device_name},
                         {"duration_seconds", duration},
                         {"process_rss_bytes", process_status_bytes("VmRSS:")},
                         {"baseline_readback_sha256", baseline}});
    struct extent {
        std::uint32_t width, height;
    };
    const std::array<extent, 4> extents{{{1024, 640}, {1600, 900}, {1920, 1080}, {1280, 720}}};
    std::size_t extent_index{};
    auto next_update = start + seconds(15);
    auto next_resize = start + seconds(20);
    auto next_capture = start + seconds(600);
    auto next_sample = start + seconds(60);
    std::optional<update_ticket> pending;
    while (steady_clock::now() - start < seconds(duration)) {
        const auto now = steady_clock::now();
        if (pending) {
            const auto progress = renderer->updates().query_progress(*pending);
            require(progress.has_value(), progress ? "" : progress.error().message);
            require(progress->state != update_state::FAILED &&
                        progress->state != update_state::CANCELLED,
                    "stress scene update failed or was cancelled");
            if (progress->state == update_state::RENDER_VISIBLE) {
                pending.reset();
                ++updates;
                write_event(events, {{"event", "revision"},
                                     {"elapsed_seconds",
                                      std::chrono::duration<double>(now - start).count()},
                                     {"revision", revision},
                                     {"splats", revision % 2 ? full_splats : 0}});
            }
        }
        if (!pending && now >= next_update) {
            const auto next_revision = revision + 1;
            auto batch = make_snapshot_batch(revision_of(original, next_revision),
                                             operation_id{next_revision});
            require(batch.has_value(), batch ? "" : batch.error().message);
            batch->full_snapshot = false;
            batch->base_revision = {revision};
            auto admitted = renderer->updates().enqueue(*batch);
            if (admitted) {
                pending = *admitted;
                revision = next_revision;
                next_update = now + seconds(15);
            } else
                require(admitted.error().code == error_code::WOULD_BLOCK,
                        "stress update admission failed: " + admitted.error().message);
        }
        if (now >= next_resize) {
            const auto extent = extents[extent_index++ % extents.size()];
            set_extent(window.get(), extent.width, extent.height);
            ++resizes;
            next_resize = now + seconds(20);
            write_event(events,
                        {{"event", "resize"},
                         {"elapsed_seconds", std::chrono::duration<double>(now - start).count()},
                         {"width", extent.width},
                         {"height", extent.height}});
        }
        const bool take_capture = now >= next_capture && !pending && revision % 2 == 1;
        if (take_capture) {
            const auto path = output / ("capture-" + std::to_string(captures) + ".ppm");
            const auto hash = capture(context, path);
            require(hash == baseline, "stress full-scene readback differs from baseline");
            ++captures;
            next_capture = now + seconds(600);
            write_event(events,
                        {{"event", "capture"},
                         {"elapsed_seconds", std::chrono::duration<double>(now - start).count()},
                         {"revision", revision},
                         {"sha256", hash}});
        } else
            check_frame(context);
        if (now >= next_sample) {
            const auto stats = renderer->diagnostics();
            write_event(events,
                        {{"event", "sample"},
                         {"elapsed_seconds", std::chrono::duration<double>(now - start).count()},
                         {"revision", renderer->updates().visible_snapshot()->revision.value},
                         {"submitted_frames", stats.submitted_frames},
                         {"completed_timeline", stats.completed_timeline},
                         {"owned_bytes", stats.allocated_bytes},
                         {"peak_owned_bytes", peak_owned_bytes},
                         {"process_rss_bytes", process_status_bytes("VmRSS:")},
                         {"process_peak_rss_bytes", process_status_bytes("VmHWM:")},
                         {"validation_errors", stats.validation_errors}});
            next_sample = now + seconds(60);
        }
        SDL_Delay(20);
    }
    if (pending)
        wait_for_revision(context, *pending);
    if (revision % 2 == 0) {
        auto final =
            make_snapshot_batch(revision_of(original, revision + 1), operation_id{revision + 1});
        require(final.has_value(), final ? "" : final.error().message);
        final->full_snapshot = false;
        final->base_revision = {revision};
        const auto admitted = renderer->updates().enqueue(*final);
        require(admitted.has_value(), admitted ? "" : admitted.error().message);
        ++revision;
        wait_for_revision(context, *admitted);
        ++updates;
    }
    const auto final_hash = capture(context, output / "capture-final.ppm");
    require(final_hash == baseline, "final full-scene readback differs from baseline");
    ++captures;
    const auto idle = renderer->wait_for_idle();
    require(idle.has_value(), idle ? "" : idle.error().message);
    const auto stats = renderer->diagnostics();
    require(stats.validation_errors == 0, "Vulkan validation error at stress completion");
    const auto elapsed = std::chrono::duration<double>(steady_clock::now() - start).count();
    json summary{{"passed", true},
                 {"scope", "native read-only retained-scene update/resize stress"},
                 {"elapsed_seconds", elapsed},
                 {"requested_seconds", duration},
                 {"scene_content_sha256", scene_hash},
                 {"scene_splats", full_splats},
                 {"device", renderer->capabilities().device_name},
                 {"updates_visible", updates},
                 {"last_visible_revision", renderer->updates().visible_snapshot()->revision.value},
                 {"resizes", resizes},
                 {"captures", captures},
                 {"readback_sha256", baseline},
                 {"submitted_frames", frames},
                 {"peak_owned_bytes", peak_owned_bytes},
                 {"process_rss_bytes", process_status_bytes("VmRSS:")},
                 {"process_peak_rss_bytes", process_status_bytes("VmHWM:")},
                 {"validation_errors", stats.validation_errors}};
    std::ofstream result(output / "summary.json", std::ios::binary);
    result << summary.dump(2) << '\n';
    require(static_cast<bool>(result), "stress summary write failed");
    write_event(events, {{"event", "complete"}, {"elapsed_seconds", elapsed}});
    std::cout << summary.dump() << '\n';
    return 0;
} catch (const std::exception& exception) {
    std::cerr << "GPU lifetime stress failed: " << exception.what() << '\n';
    return 2;
}
