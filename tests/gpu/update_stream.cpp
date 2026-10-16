#include "world_explorer/application/scenes.hpp"
#include "world_explorer/render_vulkan/renderer.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace world_explorer;
namespace {
using clock = std::chrono::steady_clock;
using json = nlohmann::json;

void require(bool condition, const std::string& message) {
    if (!condition)
        throw std::runtime_error(message);
}

double milliseconds(clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
}

double percentile(std::vector<double> samples, double fraction) {
    require(!samples.empty(), "missing performance samples");
    std::ranges::sort(samples);
    return samples[static_cast<std::size_t>(fraction * static_cast<double>(samples.size() - 1))];
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

struct enqueue_sample {
    std::uint64_t revision{};
    double prepare_ms{}, enqueue_ms{}, schedule_lag_ms{};
    std::uint64_t would_block_retries{};
};

struct producer_state {
    std::mutex mutex;
    std::vector<enqueue_sample> samples;
    std::string failure;
    std::atomic<bool> done{};
};

scene_delta_batch make_update(const scene_snapshot& original, const scene_chunk& marker_source,
                              chunk_id marker_id, std::uint64_t revision) {
    auto scene = std::make_shared<scene_state>(*original);
    scene->revision = {revision};
    if (revision % 3 != 1) {
        auto marker = std::make_shared<scene_chunk>(marker_source);
        marker->id = marker_id;
        marker->generation = revision;
        marker->world_from_chunk.translation.x += revision % 3 == 2 ? 0.005 : -0.005;
        scene->chunks.emplace(marker_id, std::move(marker));
    }
    return {scene->id,  scene->epoch,     {revision - 1}, {revision},
            {revision}, std::move(scene), false,          0};
}

void produce(const std::stop_token& stop, renderer_update_sink& sink,
             const scene_snapshot& original, const scene_chunk& marker_source, chunk_id marker_id,
             std::uint64_t count, clock::time_point start, producer_state& state) {
    try {
        for (std::uint64_t index = 0; index < count && !stop.stop_requested(); ++index) {
            const auto scheduled = start + std::chrono::milliseconds(100 * index);
            const auto revision = index + 2;
            auto batch = make_update(original, marker_source, marker_id, revision);
            const auto preparation_started = clock::now();
            auto prepared = sink.prepare(batch);
            require(prepared.has_value(), prepared ? "" : prepared.error().message);
            const auto prepare_ms = milliseconds(clock::now() - preparation_started);
            std::this_thread::sleep_until(scheduled);
            std::uint64_t retries{};
            while (!stop.stop_requested()) {
                const auto began = clock::now();
                const auto ticket = sink.enqueue(*prepared);
                const auto ended = clock::now();
                if (ticket) {
                    std::lock_guard lock(state.mutex);
                    state.samples.push_back({revision, prepare_ms, milliseconds(ended - began),
                                             milliseconds(began - scheduled), retries});
                    break;
                }
                require(ticket.error().code == error_code::WOULD_BLOCK,
                        "update enqueue failed: " + ticket.error().message);
                ++retries;
                require(ended - scheduled < std::chrono::seconds(30),
                        "update enqueue stayed blocked for 30 seconds at revision " +
                            std::to_string(revision) + ": " + ticket.error().message +
                            "; visible revision " +
                            std::to_string(sink.visible_snapshot()
                                               ? sink.visible_snapshot()->revision.value
                                               : 0));
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    } catch (const std::exception& exception) {
        std::lock_guard lock(state.mutex);
        state.failure = exception.what();
    }
    state.done = true;
}
} // namespace

int main(int argc, char** argv) try {
    require(argc == 4, "usage: we_gpu_update_stream <retained-scene> <new-output-dir> <seconds>");
    const auto seconds = std::stoull(argv[3]);
    require(seconds >= 10 && seconds <= 120, "duration must be 10–120 seconds");
    const auto output = std::filesystem::path(argv[2]);
    require(!std::filesystem::exists(output), "output directory already exists");
    require(std::filesystem::exists(output.parent_path()), "output parent is missing");
    const auto loaded = load_bundle(argv[1]);
    require(loaded.has_value(), loaded ? "" : loaded.error().message);
    const scene_snapshot& original = *loaded;
    require(!original->chunks.empty(), "P6 diagnostic requires a nonempty retained scene");
    const auto scene_hash = scene_content_hash(*original);
    const auto camera = source_camera(*original);
    auto marker = *original->chunks.begin()->second;
    marker.splats.resize(std::min<std::size_t>(marker.splats.size(), 4096));
    require(!marker.splats.empty(), "first retained chunk is empty");
    const auto marker_id = chunk_id{original->chunks.rbegin()->first.value + 1};
    require(marker_id.value != 0, "marker chunk ID overflow");
    std::size_t real_splats{};
    for (const auto& [id, chunk] : original->chunks) {
        static_cast<void>(id);
        real_splats += chunk->splats.size();
    }
    require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
    struct sdl_owner {
        ~sdl_owner() { SDL_Quit(); }
    } sdl;
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("World Explorer P6 diagnostic", 1280, 720,
                         SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN),
        SDL_DestroyWindow);
    require(window != nullptr, SDL_GetError());
    renderer_options options;
    options.max_splats = std::max<std::size_t>(real_splats + marker.splats.size(), 1000000);
    options.shader_directory = WE_SHADER_DIRECTORY;
    options.collect_timings = true;
    auto created = vulkan_renderer::create(window.get(), options);
    require(created.has_value(), created ? "" : created.error().message);
    auto renderer = std::move(*created);
    require(renderer->capabilities().validation_enabled, "Vulkan validation is required");
    auto initial = make_snapshot_batch(original, {1});
    require(initial.has_value(), initial ? "" : initial.error().message);
    const auto initial_ticket = renderer->updates().enqueue(*initial);
    require(initial_ticket.has_value(), initial_ticket ? "" : initial_ticket.error().message);
    const auto warmup_deadline = clock::now() + std::chrono::seconds(60);
    while (clock::now() < warmup_deadline) {
        SDL_PumpEvents();
        const auto drawn = renderer->draw(camera);
        require(drawn.has_value(), drawn ? "" : drawn.error().message);
        const auto progress = renderer->updates().query_progress(*initial_ticket);
        require(progress.has_value(), progress ? "" : progress.error().message);
        if (progress->state == update_state::RENDER_VISIBLE)
            break;
        require(progress->state != update_state::FAILED, "initial GPU upload failed");
    }
    const auto initial_visible = renderer->updates().visible_snapshot();
    require(initial_visible && initial_visible->revision.value == 1,
            "initial retained scene was not visible");
    for (int index = 0; index < 8; ++index) {
        SDL_PumpEvents();
        const auto drawn = renderer->draw(camera);
        require(drawn.has_value(), drawn ? "" : drawn.error().message);
    }
    const auto baseline = renderer->diagnostics();
    const auto target_count = seconds * 10;
    const auto target_revision = target_count + 1;
    producer_state producer_state;
    std::map<std::uint64_t, renderer_frame_timing> timing_samples;
    std::vector<double> draw_ms;
    std::size_t peak_owned_bytes = baseline.allocated_bytes;
    const auto start = clock::now();
    std::jthread producer([&](const std::stop_token& stop) {
        produce(stop, renderer->updates(), original, marker, marker_id, target_count, start,
                producer_state);
    });
    const auto deadline = start + std::chrono::seconds(seconds * 4 + 30);
    while (clock::now() < deadline) {
        SDL_PumpEvents();
        const auto draw_began = clock::now();
        const auto drawn = renderer->draw(camera);
        draw_ms.push_back(milliseconds(clock::now() - draw_began));
        require(drawn.has_value(), drawn ? "" : drawn.error().message);
        const auto stats = renderer->diagnostics();
        require(stats.validation_errors == 0, "Vulkan validation error during P6 replay");
        require(stats.allocated_bytes <= options.residency_bytes,
                "renderer owned-byte budget exceeded");
        peak_owned_bytes = std::max(peak_owned_bytes, stats.allocated_bytes);
        for (const auto& sample : stats.completed_timings)
            if (sample.sequence > baseline.submitted_frames)
                timing_samples.try_emplace(sample.sequence, sample);
        const auto visible = renderer->updates().visible_snapshot();
        require(visible &&
                    stats.resident_splat_count ==
                        real_splats + (visible->revision.value % 3 == 1 ? 0 : marker.splats.size()),
                "resident splat count differs from visible revision");
        if (producer_state.done) {
            std::lock_guard lock(producer_state.mutex);
            const auto admitted_revision =
                producer_state.samples.empty() ? 1 : producer_state.samples.back().revision;
            if (visible->revision.value == admitted_revision)
                break;
        }
    }
    producer.join();
    const auto admitted_revision =
        producer_state.samples.empty() ? 1 : producer_state.samples.back().revision;
    require(renderer->updates().visible_snapshot()->revision.value == admitted_revision,
            "replay did not make its last admitted revision visible");
    const bool complete = producer_state.failure.empty() && admitted_revision == target_revision;
    const auto replay_seconds = std::chrono::duration<double>(clock::now() - start).count();
    for (int index = 0; index < 3; ++index) {
        SDL_PumpEvents();
        const auto drawn = renderer->draw(camera);
        require(drawn.has_value(), drawn ? "" : drawn.error().message);
        for (const auto& sample : renderer->diagnostics().completed_timings)
            if (sample.sequence > baseline.submitted_frames)
                timing_samples.try_emplace(sample.sequence, sample);
    }
    const auto idle = renderer->wait_for_idle();
    require(idle.has_value(), idle ? "" : idle.error().message);
    const auto final = renderer->diagnostics();
    for (const auto& sample : final.completed_timings)
        if (sample.sequence > baseline.submitted_frames)
            timing_samples.try_emplace(sample.sequence, sample);
    std::vector<double> prepare_ms, enqueue_ms, schedule_lag_ms, upload_ms;
    std::uint64_t retries{};
    json updates = json::array();
    for (const auto& sample : producer_state.samples) {
        prepare_ms.push_back(sample.prepare_ms);
        enqueue_ms.push_back(sample.enqueue_ms);
        schedule_lag_ms.push_back(sample.schedule_lag_ms);
        retries += sample.would_block_retries;
        updates.push_back({{"revision", sample.revision},
                           {"prepare_ms", sample.prepare_ms},
                           {"enqueue_ms", sample.enqueue_ms},
                           {"schedule_lag_ms", sample.schedule_lag_ms},
                           {"would_block_retries", sample.would_block_retries}});
    }
    for (const auto& [sequence, sample] : timing_samples) {
        static_cast<void>(sequence);
        if (sample.gpu_valid &&
            sample.gpu_ms[static_cast<std::size_t>(gpu_timing_stage::UPLOAD)] > 0)
            upload_ms.push_back(sample.gpu_ms[static_cast<std::size_t>(gpu_timing_stage::UPLOAD)]);
    }
    if (renderer->capabilities().gpu_timestamps_supported)
        require(upload_ms.size() >= producer_state.samples.size(),
                "completed GPU upload timing samples were lost");
    const auto idle_waits =
        final.interactive_device_idle_waits - baseline.interactive_device_idle_waits;
    json summary{{"scope", "retained real scene with synthetic marker replacements and evictions"},
                 {"device", renderer->capabilities().device_name},
                 {"width", final.framebuffer_width},
                 {"height", final.framebuffer_height},
                 {"real_scene_splats", real_splats},
                 {"scene_content_sha256", scene_hash},
                 {"synthetic_marker_splats", marker.splats.size()},
                 {"scheduled_updates", target_count},
                 {"admitted_updates", producer_state.samples.size()},
                 {"visible_revision", admitted_revision},
                 {"failure", producer_state.failure},
                 {"requested_seconds", seconds},
                 {"replay_seconds", replay_seconds},
                 {"visible_updates_per_second",
                  static_cast<double>(producer_state.samples.size()) / replay_seconds},
                 {"submitted_frames", final.submitted_frames - baseline.submitted_frames},
                 {"peak_owned_bytes", peak_owned_bytes},
                 {"validation_errors", final.validation_errors},
                 {"interactive_device_idle_waits", idle_waits},
                 {"would_block_retries", retries},
                 {"draw_ms",
                  {{"p50", percentile(draw_ms, 0.5)},
                   {"p95", percentile(draw_ms, 0.95)},
                   {"p99", percentile(draw_ms, 0.99)}}},
                 {"enqueue_ms",
                  {{"p50", percentile(enqueue_ms, 0.5)},
                   {"p95", percentile(enqueue_ms, 0.95)},
                   {"p99", percentile(enqueue_ms, 0.99)}}},
                 {"prepare_ms",
                  {{"p50", percentile(prepare_ms, 0.5)},
                   {"p95", percentile(prepare_ms, 0.95)},
                   {"p99", percentile(prepare_ms, 0.99)}}},
                 {"schedule_lag_ms",
                  {{"p50", percentile(schedule_lag_ms, 0.5)},
                   {"p95", percentile(schedule_lag_ms, 0.95)},
                   {"p99", percentile(schedule_lag_ms, 0.99)}}},
                 {"gpu_upload_samples", upload_ms.size()},
                 {"gpu_upload_ms_samples", upload_ms},
                 {"gpu_timestamp_supported", renderer->capabilities().gpu_timestamps_supported},
                 {"updates", std::move(updates)}};
    if (!upload_ms.empty())
        summary["gpu_upload_ms"] = {{"p50", percentile(upload_ms, 0.5)},
                                    {"p95", percentile(upload_ms, 0.95)},
                                    {"p99", percentile(upload_ms, 0.99)},
                                    {"max", *std::ranges::max_element(upload_ms)}};
    summary["local_p6_thresholds_met"] =
        complete && summary["enqueue_ms"]["p95"].get<double>() <= 0.5 && !upload_ms.empty() &&
        *std::ranges::max_element(upload_ms) <= 2.0 && idle_waits == 0 &&
        summary["visible_updates_per_second"].get<double>() >= 10.0;
    std::filesystem::create_directory(output);
    std::ofstream report(output / "summary.json", std::ios::binary);
    report << summary.dump(2) << '\n';
    require(static_cast<bool>(report), "cannot write P6 summary");
    std::cout << summary.dump() << '\n';
    return complete ? 0 : 2;
} catch (const std::exception& exception) {
    std::cerr << "P6 update stream failed: " << exception.what() << '\n';
    return 2;
}
