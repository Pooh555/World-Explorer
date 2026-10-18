#include "world_explorer/application/scenes.hpp"
#include "world_explorer/render_vulkan/renderer.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

using namespace world_explorer;

namespace {

using Clock = std::chrono::steady_clock;
using Json = nlohmann::json;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

double milliseconds(Clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
}

double percentile(std::vector<double> samples, double fraction) {
    require(!samples.empty(), "missing performance samples");
    std::ranges::sort(samples);
    return samples[static_cast<std::size_t>(fraction * static_cast<double>(samples.size() - 1))];
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

struct EnqueueSample {
    std::uint64_t m_revision {};
    double m_prepare_ms {}, m_enqueue_ms {}, m_schedule_lag_ms {};
    std::uint64_t m_would_block_retries {};
};

struct ProducerState {
    std::mutex m_mutex;
    std::vector<EnqueueSample> m_samples;
    std::string m_failure;
    std::atomic<bool> m_done {};
};

SceneDeltaBatch make_update(const SceneSnapshot& original,
                            const SceneChunk& marker_source,
                            ChunkId marker_id,
                            std::uint64_t revision) {
    auto scene = std::make_shared<SceneState>(*original);
    scene->m_revision = { revision };
    if (revision % 3 != 1) {
        auto marker = std::make_shared<SceneChunk>(marker_source);
        marker->m_id = marker_id;
        marker->m_generation = revision;
        marker->m_world_from_chunk.m_translation.m_x += revision % 3 == 2 ? 0.005 : -0.005;
        scene->m_chunks.emplace(marker_id, std::move(marker));
    }
    return {scene->m_id,
            scene->m_epoch,
            {revision - 1},
            {revision},
            {revision},
            std::move(scene),
            false,
            0};
}

void produce(const std::stop_token& stop,
             RendererUpdateSink& sink,
             const SceneSnapshot& original,
             const SceneChunk& marker_source,
             ChunkId marker_id,
             std::uint64_t count,
             Clock::time_point start,
             ProducerState& state) {
    try {
        for (std::uint64_t index { 0 }; index < count && !stop.stop_requested(); ++index) {
            const auto K_SCHEDULED = start + std::chrono::milliseconds(100 * index);
            const auto K_REVISION = index + 2;
            auto batch = make_update(original, marker_source, marker_id, K_REVISION);
            const auto K_PREPARATION_STARTED = Clock::now();
            auto prepared = sink.prepare(batch);
            require(prepared.has_value(), prepared ? "" : prepared.error().m_message);
            const auto K_PREPARE_MS = milliseconds(Clock::now() - K_PREPARATION_STARTED);
            std::this_thread::sleep_until(K_SCHEDULED);
            std::uint64_t retries {};
            while (!stop.stop_requested()) {
                const auto K_BEGAN = Clock::now();
                const auto K_TICKET = sink.enqueue(*prepared);
                const auto K_ENDED = Clock::now();
                if (K_TICKET) {
                    std::lock_guard lock(state.m_mutex);
                    state.m_samples.push_back({K_REVISION,
                                               K_PREPARE_MS,
                                               milliseconds(K_ENDED - K_BEGAN),
                                               milliseconds(K_BEGAN - K_SCHEDULED),
                                               retries});
                    break;
                }
                require(K_TICKET.error().m_code == ErrorCode::WOULD_BLOCK,
                        "update enqueue failed: " + K_TICKET.error().m_message);
                ++retries;
                require(K_ENDED - K_SCHEDULED < std::chrono::seconds(30),
                        "update enqueue stayed blocked for 30 seconds at revision " +
                            std::to_string(K_REVISION) + ": " + K_TICKET.error().m_message +
                            "; visible revision " +
                            std::to_string(sink.visible_snapshot()
                                               ? sink.visible_snapshot()->m_revision.m_value
                                               : 0));
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    } catch (const std::exception& exception) {
        std::lock_guard lock(state.m_mutex);
        state.m_failure = exception.what();
    }
    state.m_done = true;
}

}  // namespace

int main(int argc, char** argv) try {
    require(argc == 4 || (argc == 5 && std::string_view(argv[4]) == "--no-resident-reuse"),
            "usage: we_gpu_update_stream <retained-scene> <new-output-dir> <seconds> "
            "[--no-resident-reuse]");
    const auto K_SECONDS = std::stoull(argv[3]);
    require(K_SECONDS >= 10 && K_SECONDS <= 120, "duration must be 10–120 seconds");
    const auto K_OUTPUT = std::filesystem::path(argv[2]);
    require(!std::filesystem::exists(K_OUTPUT), "output directory already exists");
    require(std::filesystem::exists(K_OUTPUT.parent_path()), "output parent is missing");
    const auto K_LOADED = load_bundle(argv[1]);
    require(K_LOADED.has_value(), K_LOADED ? "" : K_LOADED.error().m_message);
    const SceneSnapshot& original = *K_LOADED;
    require(!original->m_chunks.empty(), "P6 diagnostic requires a nonempty retained scene");
    const auto K_SCENE_HASH = scene_content_hash(*original);
    const auto K_CAMERA = source_camera(*original);
    auto marker = *original->m_chunks.begin()->second;
    marker.m_splats.resize(std::min<std::size_t>(marker.m_splats.size(), 4096));
    require(!marker.m_splats.empty(), "first retained chunk is empty");
    const auto K_MARKER_ID = ChunkId { original->m_chunks.rbegin()->first.m_value + 1 };
    require(K_MARKER_ID.m_value != 0, "marker chunk ID overflow");
    std::size_t real_splats {};
    for (const auto& [K_ID, K_CHUNK] : original->m_chunks) {
        static_cast<void>(K_ID);
        real_splats += K_CHUNK->m_splats.size();
    }
    require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());

    struct SdlOwner {
        ~SdlOwner() { SDL_Quit(); }
    } sdl;

    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow(
            "World Explorer P6 diagnostic", 1280, 720, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN),
        SDL_DestroyWindow);
    require(window != nullptr, SDL_GetError());
    RendererOptions options;
    options.m_max_splats = std::max<std::size_t>(real_splats + marker.m_splats.size(), 1000000);
    options.m_shader_directory = WE_SHADER_DIRECTORY;
    options.m_collect_timings = true;
    options.m_reuse_resident_chunks = argc == 4;
    auto created = VulkanRenderer::create(window.get(), options);
    require(created.has_value(), created ? "" : created.error().m_message);
    auto renderer = std::move(*created);
    require(renderer->capabilities().m_validation_enabled, "Vulkan validation is required");
    auto initial = make_snapshot_batch(original, {1});
    require(initial.has_value(), initial ? "" : initial.error().m_message);
    const auto K_INITIAL_TICKET = renderer->updates().enqueue(*initial);
    require(K_INITIAL_TICKET.has_value(),
            K_INITIAL_TICKET ? "" : K_INITIAL_TICKET.error().m_message);
    const auto K_WARMUP_DEADLINE = Clock::now() + std::chrono::seconds(60);
    while (Clock::now() < K_WARMUP_DEADLINE) {
        SDL_PumpEvents();
        const auto K_DRAWN = renderer->draw(K_CAMERA);
        require(K_DRAWN.has_value(), K_DRAWN ? "" : K_DRAWN.error().m_message);
        const auto K_PROGRESS = renderer->updates().query_progress(*K_INITIAL_TICKET);
        require(K_PROGRESS.has_value(), K_PROGRESS ? "" : K_PROGRESS.error().m_message);
        if (K_PROGRESS->m_state == UpdateState::RENDER_VISIBLE) break;
        require(K_PROGRESS->m_state != UpdateState::FAILED, "initial GPU upload failed");
    }
    const auto K_INITIAL_VISIBLE = renderer->updates().visible_snapshot();
    require(K_INITIAL_VISIBLE && K_INITIAL_VISIBLE->m_revision.m_value == 1,
            "initial retained scene was not visible");
    for (int index { 0 }; index < 8; ++index) {
        SDL_PumpEvents();
        const auto K_DRAWN = renderer->draw(K_CAMERA);
        require(K_DRAWN.has_value(), K_DRAWN ? "" : K_DRAWN.error().m_message);
    }
    const auto K_BASELINE = renderer->diagnostics();
    const auto K_TARGET_COUNT = K_SECONDS * 10;
    const auto K_TARGET_REVISION = K_TARGET_COUNT + 1;
    ProducerState producer_state;
    std::map<std::uint64_t, RendererFrameTiming> timing_samples;
    std::vector<double> draw_ms;
    std::size_t peak_owned_bytes = K_BASELINE.m_allocated_bytes;
    const auto K_START = Clock::now();
    std::jthread producer([&](const std::stop_token& stop) {
        produce(stop,
                renderer->updates(),
                original,
                marker,
                K_MARKER_ID,
                K_TARGET_COUNT,
                K_START,
                producer_state);
    });
    const auto K_DEADLINE = K_START + std::chrono::seconds(K_SECONDS * 4 + 30);
    while (Clock::now() < K_DEADLINE) {
        SDL_PumpEvents();
        const auto K_DRAW_BEGAN = Clock::now();
        const auto K_DRAWN = renderer->draw(K_CAMERA);
        draw_ms.push_back(milliseconds(Clock::now() - K_DRAW_BEGAN));
        require(K_DRAWN.has_value(), K_DRAWN ? "" : K_DRAWN.error().m_message);
        const auto K_STATS = renderer->diagnostics();
        require(K_STATS.m_validation_errors == 0, "Vulkan validation error during P6 replay");
        require(K_STATS.m_allocated_bytes <= options.m_residency_bytes,
                "renderer owned-byte budget exceeded");
        peak_owned_bytes = std::max(peak_owned_bytes, K_STATS.m_allocated_bytes);
        for (const auto& sample : K_STATS.m_completed_timings)
            if (sample.m_sequence > K_BASELINE.m_submitted_frames)
                timing_samples.try_emplace(sample.m_sequence, sample);
        const auto K_VISIBLE = renderer->updates().visible_snapshot();
        require(K_VISIBLE &&
                    K_STATS.m_resident_splat_count ==
                        real_splats +
                            (K_VISIBLE->m_revision.m_value % 3 == 1 ? 0 : marker.m_splats.size()),
                "resident splat count differs from visible revision");
        if (producer_state.m_done) {
            std::lock_guard lock(producer_state.m_mutex);
            const auto K_ADMITTED_REVISION =
                producer_state.m_samples.empty() ? 1 : producer_state.m_samples.back().m_revision;
            if (K_VISIBLE->m_revision.m_value == K_ADMITTED_REVISION) break;
        }
    }
    producer.join();
    const auto K_ADMITTED_REVISION =
        producer_state.m_samples.empty() ? 1 : producer_state.m_samples.back().m_revision;
    require(renderer->updates().visible_snapshot()->m_revision.m_value == K_ADMITTED_REVISION,
            "replay did not make its last admitted revision visible");
    const bool K_COMPLETE =
        producer_state.m_failure.empty() && K_ADMITTED_REVISION == K_TARGET_REVISION;
    const auto K_REPLAY_SECONDS = std::chrono::duration<double>(Clock::now() - K_START).count();
    for (int index { 0 }; index < 3; ++index) {
        SDL_PumpEvents();
        const auto K_DRAWN = renderer->draw(K_CAMERA);
        require(K_DRAWN.has_value(), K_DRAWN ? "" : K_DRAWN.error().m_message);
        for (const auto& sample : renderer->diagnostics().m_completed_timings)
            if (sample.m_sequence > K_BASELINE.m_submitted_frames)
                timing_samples.try_emplace(sample.m_sequence, sample);
    }
    const auto K_IDLE = renderer->wait_for_idle();
    require(K_IDLE.has_value(), K_IDLE ? "" : K_IDLE.error().m_message);
    const auto K_FINAL = renderer->diagnostics();
    for (const auto& sample : K_FINAL.m_completed_timings)
        if (sample.m_sequence > K_BASELINE.m_submitted_frames)
            timing_samples.try_emplace(sample.m_sequence, sample);
    std::vector<double> prepare_ms, enqueue_ms, schedule_lag_ms, upload_ms;
    std::uint64_t retries {};
    Json updates = Json::array();
    for (const auto& sample : producer_state.m_samples) {
        prepare_ms.push_back(sample.m_prepare_ms);
        enqueue_ms.push_back(sample.m_enqueue_ms);
        schedule_lag_ms.push_back(sample.m_schedule_lag_ms);
        retries += sample.m_would_block_retries;
        updates.push_back({{"revision", sample.m_revision},
                           {"prepare_ms", sample.m_prepare_ms},
                           {"enqueue_ms", sample.m_enqueue_ms},
                           {"schedule_lag_ms", sample.m_schedule_lag_ms},
                           {"would_block_retries", sample.m_would_block_retries}});
    }
    std::size_t host_copy_bytes {}, resident_copy_bytes {};
    for (const auto& [K_SEQUENCE, K_SAMPLE] : timing_samples) {
        static_cast<void>(K_SEQUENCE);
        host_copy_bytes += K_SAMPLE.m_host_copy_bytes;
        resident_copy_bytes += K_SAMPLE.m_resident_copy_bytes;
        if (K_SAMPLE.m_gpu_valid &&
            K_SAMPLE.m_gpu_ms[static_cast<std::size_t>(GpuTimingStage::UPLOAD)] > 0)
            upload_ms.push_back(
                K_SAMPLE.m_gpu_ms[static_cast<std::size_t>(GpuTimingStage::UPLOAD)]);
    }
    if (renderer->capabilities().m_gpu_timestamps_supported)
        require(upload_ms.size() >= producer_state.m_samples.size(),
                "completed GPU upload timing samples were lost");
    const auto K_IDLE_WAITS =
        K_FINAL.m_interactive_device_idle_waits - K_BASELINE.m_interactive_device_idle_waits;
    Json summary {{"scope", "retained real scene with synthetic marker replacements and evictions"},
                  {"device", renderer->capabilities().m_device_name},
                  {"width", K_FINAL.m_framebuffer_width},
                  {"height", K_FINAL.m_framebuffer_height},
                  {"real_scene_splats", real_splats},
                  {"scene_content_sha256", K_SCENE_HASH},
                  {"synthetic_marker_splats", marker.m_splats.size()},
                  {"scheduled_updates", K_TARGET_COUNT},
                  {"admitted_updates", producer_state.m_samples.size()},
                  {"visible_revision", K_ADMITTED_REVISION},
                  {"failure", producer_state.m_failure},
                  {"requested_seconds", K_SECONDS},
                  {"replay_seconds", K_REPLAY_SECONDS},
                  {"visible_updates_per_second",
                   static_cast<double>(producer_state.m_samples.size()) / K_REPLAY_SECONDS},
                  {"submitted_frames", K_FINAL.m_submitted_frames - K_BASELINE.m_submitted_frames},
                  {"peak_owned_bytes", peak_owned_bytes},
                  {"validation_errors", K_FINAL.m_validation_errors},
                  {"interactive_device_idle_waits", K_IDLE_WAITS},
                  {"resident_reuse_enabled", options.m_reuse_resident_chunks},
                  {"host_copy_bytes", host_copy_bytes},
                  {"resident_copy_bytes", resident_copy_bytes},
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
                  {"gpu_timestamp_supported", renderer->capabilities().m_gpu_timestamps_supported},
                  {"updates", std::move(updates)}};
    if (!upload_ms.empty())
        summary["gpu_upload_ms"] = { { "p50", percentile(upload_ms, 0.5) },
                                    { "p95", percentile(upload_ms, 0.95) },
                                    { "p99", percentile(upload_ms, 0.99) },
                                    { "max", *std::ranges::max_element(upload_ms) } };
    summary["local_p6_thresholds_met"] =
        K_COMPLETE && summary["enqueue_ms"]["p95"].get<double>() <= 0.5 && !upload_ms.empty() &&
        *std::ranges::max_element(upload_ms) <= 2.0 && K_IDLE_WAITS == 0 &&
        summary["visible_updates_per_second"].get<double>() >= 10.0;
    std::filesystem::create_directory(K_OUTPUT);
    std::ofstream report(K_OUTPUT / "summary.json", std::ios::binary);
    report << summary.dump(2) << '\n';
    require(static_cast<bool>(report), "cannot write P6 summary");
    std::cout << summary.dump() << '\n';
    return K_COMPLETE ? 0 : 2;
} catch (const std::exception& exception) {
    std::cerr << "P6 update stream failed: " << exception.what() << '\n';
    return 2;
}
