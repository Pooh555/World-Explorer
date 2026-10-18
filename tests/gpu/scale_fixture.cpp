#include "../fixtures/scale_fixture.hpp"

#include "world_explorer/core/hash.hpp"
#include "world_explorer/render_vulkan/renderer.hpp"
#include "world_explorer/scene_store/bundle.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

using namespace world_explorer;

namespace {

using Json = nlohmann::json;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::optional<std::size_t> process_bytes(std::string_view field, bool rollup = false) {
#if defined(__linux__)
    std::ifstream status(rollup ? "/proc/self/smaps_rollup" : "/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (!line.starts_with(field)) continue;
        std::istringstream stream(line);
        std::string key, unit;
        std::size_t value {};
        if (stream >> key >> value >> unit && unit == "kB" &&
            value <= std::numeric_limits<std::size_t>::max() / 1024)
            return value * 1024;
        break;
    }
#else
    static_cast<void>(field);
    static_cast<void>(rollup);
#endif
    return {};
}

Json optional_bytes(std::optional<std::size_t> value) {
    return value ? Json(*value) : Json(nullptr);
}

std::string digest(const std::filesystem::path& path) {
    const auto K_HASH = sha256_file(path, 16ULL * 1024 * 1024);
    require(K_HASH.has_value(), "cannot hash diagnostic capture");
    return *K_HASH;
}

std::size_t foreground(const std::filesystem::path& path) {
    std::ifstream image(path, std::ios::binary);
    std::string magic;
    unsigned width {}, height {}, maximum {};
    image >> magic >> width >> height >> maximum;
    require(magic == "P6" && width == K_SCALE_FIXTURE_WIDTH && height == K_SCALE_FIXTURE_HEIGHT &&
                maximum == 255 && image.get() == '\n',
            "invalid full-size RGB8 readback");
    std::vector<unsigned char> pixels(std::size_t { width } * height * 3);
    image.read(reinterpret_cast<char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    require(image.gcount() == static_cast<std::streamsize>(pixels.size()) && image.peek() == EOF,
            "invalid readback byte count");
    constexpr std::array<int, 3> K_BACKGROUND { 11, 13, 16 };
    std::size_t count {};
    for (std::size_t pixel { 0uz }; pixel < pixels.size(); pixel += 3uz)
        if (std::abs(static_cast<int>(pixels[pixel]) - K_BACKGROUND[0]) > 8 ||
            std::abs(static_cast<int>(pixels[pixel + 1]) - K_BACKGROUND[1]) > 8 ||
            std::abs(static_cast<int>(pixels[pixel + 2]) - K_BACKGROUND[2]) > 8)
            ++count;
    return count;
}

}  // namespace
int main(int argc, char** argv) try {
    require(argc >= 3,
            "usage: we_gpu_scale_fixture FIXED_BUNDLE NEW_DIRECTORY "
            "[--fp16-sh] [--budget-mib N]");
    bool packed = false, budget_set = false;
    std::size_t budget_mib = 2048;
    for (int argument { 3 }; argument < argc; ++argument) {
        const std::string_view K_OPTION(argv[argument]);
        if (K_OPTION == "--fp16-sh" && !packed) packed = true;
        else if (K_OPTION == "--budget-mib" && !budget_set && argument + 1 < argc) {
            const std::string_view K_TEXT(argv[++argument]);
            const auto K_PARSED =
                std::from_chars(K_TEXT.data(), K_TEXT.data() + K_TEXT.size(), budget_mib);
            require(K_PARSED.ec == std::errc {} && K_PARSED.ptr == K_TEXT.data() + K_TEXT.size() &&
                        budget_mib >= 512 && budget_mib <= 4096,
                    "budget must be an integer from 512 through 4096 MiB");
            budget_set = true;
        } else throw std::runtime_error("unknown, duplicate or missing diagnostic option");
    }
    const std::filesystem::path K_OUTPUT(argv[2]);
    require(std::filesystem::create_directory(K_OUTPUT), "output must be a new directory");
    Json report {{"scope", "synthetic 2M resident admission/memory; no FPS or capture gate"},
                 {"passed", false},
                 {"storage", packed ? "FP32_GEOMETRY_FP16_SH" : "FP32"},
                 {"residency_budget_bytes", budget_mib * 1024 * 1024},
                 {"frames", Json::array()},
                 {"views", Json::array()}};
    try {
        BundleLimits limits;
        limits.m_scene.m_max_splats = K_SCALE_FIXTURE_SPLATS;
        limits.m_scene.m_max_chunks = 8;
        limits.m_scene.m_max_payload_bytes = 512ULL * 1024 * 1024;
        const auto K_SCENE = load_bundle(argv[1], limits);
        require(K_SCENE.has_value(), K_SCENE ? "" : K_SCENE.error().m_message);
        const auto K_CONTENT = scene_content_hash(**K_SCENE);
        require(K_CONTENT == K_SCALE_FIXTURE_SHA256, "bundle differs from frozen scale fixture");
        report["scene_content_sha256"] = K_CONTENT;
        const auto K_PAYLOAD = validate_scene(**K_SCENE, limits.m_scene);
        require(K_PAYLOAD.has_value(), "invalid full fixture payload");
        report["cpu_payload_bytes"] = *K_PAYLOAD;
        require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());

        struct SdlOwner {
            ~SdlOwner() { SDL_Quit(); }
        } sdl;

        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Synthetic World Explorer 2M scale check",
                             K_SCALE_FIXTURE_WIDTH,
                             K_SCALE_FIXTURE_HEIGHT,
                             SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN),
            SDL_DestroyWindow);
        require(window != nullptr, SDL_GetError());
        RendererOptions options;
        options.m_max_splats = K_SCALE_FIXTURE_SPLATS;
        options.m_residency_bytes = budget_mib * 1024 * 1024;
        options.m_collect_timings = true;
        options.m_splat_storage =
            packed ? GpuSplatStorage::FP32_GEOMETRY_FP16_SH : GpuSplatStorage::FP32;
        options.m_shader_directory = WE_SHADER_DIRECTORY;
        auto created = VulkanRenderer::create(window.get(), options);
        require(created.has_value(), created ? "" : created.error().m_message);
        auto renderer = std::move(*created);
        require(renderer->capabilities().m_validation_enabled, "native Vulkan validation required");
        report["device"] = renderer->capabilities().m_device_name;
        report["video_driver"] = SDL_GetCurrentVideoDriver();
        report["splat_stride_bytes"] = renderer->capabilities().m_splat_stride_bytes;
        report["gpu_timestamps_supported"] = renderer->capabilities().m_gpu_timestamps_supported;
        std::size_t peak_owned {}, peak_staging {}, peak_scratch {}, peak_framebuffer {};
        std::optional<std::size_t> peak_hwm, peak_rss;
        std::map<std::uint64_t, RendererFrameTiming> samples;
        const auto K_SAMPLE = [&] {
            const auto K_STATS = renderer->diagnostics();
            require(K_STATS.m_validation_errors == 0 &&
                        K_STATS.m_allocated_bytes <= options.m_residency_bytes,
                    "validation or residency failure");
            require(K_STATS.m_framebuffer_width == K_SCALE_FIXTURE_WIDTH &&
                        K_STATS.m_framebuffer_height == K_SCALE_FIXTURE_HEIGHT,
                    "framebuffer differs from fixed 1080p calibration");
            peak_owned = std::max(peak_owned, K_STATS.m_allocated_bytes);
            peak_staging = std::max(peak_staging, K_STATS.m_staging_bytes);
            peak_scratch = std::max(peak_scratch, K_STATS.m_scratch_bytes);
            peak_framebuffer = std::max(peak_framebuffer, K_STATS.m_framebuffer_bytes);
            for (const auto& timing : K_STATS.m_completed_timings)
                if (timing.m_sequence) samples.try_emplace(timing.m_sequence, timing);
            // status RSS/HWM counters are approximate and can fluctuate. Retain
            // every raw sample and its maximum; rollup provides a separate RSS
            // observation with explicit sampling scope.
            const auto K_HWM = process_bytes("VmHWM:");
            const auto K_RSS = process_bytes("Rss:", true);
            if (K_HWM) peak_hwm = std::max(peak_hwm.value_or(0), *K_HWM);
            if (K_RSS) peak_rss = std::max(peak_rss.value_or(0), *K_RSS);
            report["frames"].push_back({{"sequence", K_STATS.m_submitted_frames},
                                        {"resident_splats", K_STATS.m_resident_splat_count},
                                        {"references", K_STATS.m_tile_reference_count},
                                        {"owned_bytes", K_STATS.m_allocated_bytes},
                                        {"staging_bytes", K_STATS.m_staging_bytes},
                                        {"scratch_bytes", K_STATS.m_scratch_bytes},
                                        {"framebuffer_bytes", K_STATS.m_framebuffer_bytes},
                                        {"rss_bytes", optional_bytes(process_bytes("VmRSS:"))},
                                        {"process_hwm_bytes", optional_bytes(K_HWM)},
                                        {"smaps_rss_bytes", optional_bytes(K_RSS)}});
            report["peak_sampled_owned_bytes"] = peak_owned;
            report["peak_sampled_staging_bytes"] = peak_staging;
            report["peak_sampled_scratch_bytes"] = peak_scratch;
            report["framebuffer_bytes"] = peak_framebuffer;
            report["process_hwm_bytes"] = optional_bytes(peak_hwm);
            report["peak_sampled_rss_bytes"] = optional_bytes(peak_rss);
            report["validation_errors"] = K_STATS.m_validation_errors;
            if (peak_hwm)
                require(*peak_hwm <= 4ULL * 1024 * 1024 * 1024,
                        "sampled process high-water exceeds the unchanged 4 GiB bound");
            if (peak_rss)
                require(*peak_rss <= 4ULL * 1024 * 1024 * 1024,
                        "sampled process RSS exceeds the unchanged 4 GiB bound");
        };
        const auto K_DRAW = [&](const CameraView& camera) {
            SDL_PumpEvents();
            const auto K_DRAWN = renderer->draw(camera);
            require(K_DRAWN.has_value(), K_DRAWN ? "" : K_DRAWN.error().m_message);
            K_SAMPLE();
        };
        const auto K_CAPTURE = [&](const char* name, const CameraView& camera) {
            const auto K_PATH = K_OUTPUT / (std::string(name) + ".ppm");
            require(renderer->request_screenshot(K_PATH).has_value(), "readback request failed");
            for (int frame { 0 }; frame < 8 && !std::filesystem::exists(K_PATH); ++frame)
                K_DRAW(camera);
            require(std::filesystem::exists(K_PATH), "readback did not complete");
            require(renderer->wait_for_idle().has_value(), "GPU drain failed");
            K_SAMPLE();
            return digest(K_PATH);
        };
        const auto K_ADMIT = [&](const SceneSnapshot& snapshot, OperationId operation) {
            auto batch = make_snapshot_batch(snapshot, operation);
            require(batch.has_value(), "fixture snapshot is invalid");
            const auto K_TICKET = renderer->updates().enqueue(*batch);
            require(K_TICKET.has_value(), K_TICKET ? "" : K_TICKET.error().m_message);
            return *K_TICKET;
        };
        const auto K_AWAIT = [&](UpdateTicket ticket) {
            const auto K_DEADLINE = std::chrono::steady_clock::now() + std::chrono::seconds(60);
            for (int frame { 0 }; frame < 256 && std::chrono::steady_clock::now() < K_DEADLINE;
                 ++frame) {
                K_DRAW(scale_fixture_camera());
                const auto K_PROGRESS = renderer->updates().query_progress(ticket);
                require(K_PROGRESS.has_value(), "upload progress missing");
                if (K_PROGRESS->m_state == UpdateState::FAILED ||
                    K_PROGRESS->m_state == UpdateState::RENDER_VISIBLE)
                    return *K_PROGRESS;
            }
            throw std::runtime_error("bounded fixture upload did not complete");
        };
        auto empty = std::make_shared<SceneState>(**K_SCENE);
        empty->m_chunks.clear();
        require(K_AWAIT(K_ADMIT(empty, {1})).m_state == UpdateState::RENDER_VISIBLE,
                "empty baseline failed");
        const auto K_BASELINE_HASH = K_CAPTURE("baseline", scale_fixture_camera());
        auto full = std::make_shared<SceneState>(**K_SCENE);
        full->m_revision = { 2 };
        const auto K_FULL_HASH = scene_content_hash(*full);
        report["upload_scene_content_sha256"] = K_FULL_HASH;
        const auto K_PROGRESS = K_AWAIT(K_ADMIT(full, {2}));
        if (K_PROGRESS.m_state == UpdateState::FAILED) {
            report["admission_error"] =
                K_PROGRESS.m_failure ? K_PROGRESS.m_failure->m_message : "unknown";
            report["rejected_admission_preserved"] =
                renderer->updates().visible_snapshot() == empty &&
                renderer->diagnostics().m_resident_splat_count == 0 &&
                K_CAPTURE("rejected", scale_fixture_camera()) == K_BASELINE_HASH &&
                scene_content_hash(*full) == K_FULL_HASH;
            require(report.at("rejected_admission_preserved").get<bool>(),
                    "rejected admission changed prior scene or caller data");
            throw std::runtime_error("full scale fixture was rejected: " +
                                     report.at("admission_error").get<std::string>());
        }
        std::array<std::string, 3> hashes;
        constexpr std::array K_NAMES { "front", "reverse", "front-return" };
        for (std::size_t view { 0uz }; view < K_NAMES.size(); ++view) {
            const auto K_CAMERA = scale_fixture_camera(view == 1);
            for (int warmup { 0 }; warmup < 4; ++warmup) K_DRAW(K_CAMERA);
            hashes[view] = K_CAPTURE(K_NAMES[view], K_CAMERA);
            const auto K_STATS = renderer->diagnostics();
            require(K_STATS.m_resident_splat_count == K_SCALE_FIXTURE_SPLATS &&
                        K_STATS.m_tile_reference_count == K_SCALE_FIXTURE_REFERENCES,
                    "full resident count or analytic tile coverage differs");
            const auto K_PIXELS = foreground(K_OUTPUT / (std::string(K_NAMES[view]) + ".ppm"));
            require(K_PIXELS > std::size_t { K_SCALE_FIXTURE_WIDTH } * K_SCALE_FIXTURE_HEIGHT / 4,
                    "capture lacks the full bank's foreground");
            report["views"].push_back({{"view", K_NAMES[view]},
                                       {"sha256", hashes[view]},
                                       {"resident_splats", K_STATS.m_resident_splat_count},
                                       {"tile_references", K_STATS.m_tile_reference_count},
                                       {"foreground_pixels", K_PIXELS}});
        }
        require(hashes[0] == hashes[2] && hashes[0] != hashes[1],
                "bank switch or return pixels differ from fixture invariants");
        std::size_t host {}, resident {};
        for (const auto& [K_SEQUENCE, K_TIMING] : samples) {
            static_cast<void>(K_SEQUENCE);
            require(K_TIMING.m_host_copy_bytes + K_TIMING.m_resident_copy_bytes <=
                        15ULL * 1024 * 1024,
                    "per-frame transfer budget exceeded");
            host += K_TIMING.m_host_copy_bytes;
            resident += K_TIMING.m_resident_copy_bytes;
        }
        require(host == 16 + K_SCALE_FIXTURE_SPLATS *
                                 renderer->capabilities().m_splat_stride_bytes &&
                    resident == 0,
                "upload bytes differ from the complete resident payload");
        report["host_copy_bytes"] = host;
        report["resident_copy_bytes"] = resident;
        report["canonical_scene_unchanged"] = scene_content_hash(*full) == K_FULL_HASH;
        require(report.at("canonical_scene_unchanged").get<bool>(), "caller scene changed");
        report["passed"] = true;
    } catch (const std::exception& error) {
        report["error"] = error.what();
    }
    std::ofstream stream(K_OUTPUT / "native.json", std::ios::binary | std::ios::noreplace);
    stream << report.dump(2) << '\n';
    stream.close();
    require(stream.good(), "cannot save scale diagnostic report");
    std::cout << report.at("passed") << " " << report.value("error", std::string {}) << '\n';
    return report.at("passed").get<bool>() ? 0 : 2;
} catch (const std::exception& error) {
    std::cerr << "Scale diagnostic failed: " << error.what() << '\n';
    return 2;
}
