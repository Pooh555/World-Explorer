#include "../fixtures/scale_fixture.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/render_vulkan/renderer.hpp"
#include "world_explorer/scene_store/bundle.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <stdexcept>

using namespace world_explorer;
namespace {
using json = nlohmann::json;
void require(bool condition, const std::string& message) {
    if (!condition)
        throw std::runtime_error(message);
}
std::optional<std::size_t> process_bytes(std::string_view field, bool rollup = false) {
#if defined(__linux__)
    std::ifstream status(rollup ? "/proc/self/smaps_rollup" : "/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (!line.starts_with(field))
            continue;
        std::istringstream stream(line);
        std::string key, unit;
        std::size_t value{};
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
json optional_bytes(std::optional<std::size_t> value) {
    return value ? json(*value) : json(nullptr);
}
std::string digest(const std::filesystem::path& path) {
    const auto hash = sha256_file(path, 16ULL * 1024 * 1024);
    require(hash.has_value(), "cannot hash diagnostic capture");
    return *hash;
}
std::size_t foreground(const std::filesystem::path& path) {
    std::ifstream image(path, std::ios::binary);
    std::string magic;
    unsigned width{}, height{}, maximum{};
    image >> magic >> width >> height >> maximum;
    require(magic == "P6" && width == SCALE_FIXTURE_WIDTH && height == SCALE_FIXTURE_HEIGHT &&
                maximum == 255 && image.get() == '\n',
            "invalid full-size RGB8 readback");
    std::vector<unsigned char> pixels(std::size_t{width} * height * 3);
    image.read(reinterpret_cast<char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    require(image.gcount() == static_cast<std::streamsize>(pixels.size()) && image.peek() == EOF,
            "invalid readback byte count");
    constexpr std::array<int, 3> BACKGROUND{11, 13, 16};
    std::size_t count{};
    for (std::size_t pixel = 0; pixel < pixels.size(); pixel += 3)
        if (std::abs(static_cast<int>(pixels[pixel]) - BACKGROUND[0]) > 8 ||
            std::abs(static_cast<int>(pixels[pixel + 1]) - BACKGROUND[1]) > 8 ||
            std::abs(static_cast<int>(pixels[pixel + 2]) - BACKGROUND[2]) > 8)
            ++count;
    return count;
}
} // namespace
int main(int argc, char** argv) try {
    require(argc >= 3, "usage: we_gpu_scale_fixture FIXED_BUNDLE NEW_DIRECTORY "
                       "[--fp16-sh] [--budget-mib N]");
    bool packed = false, budget_set = false;
    std::size_t budget_mib = 2048;
    for (int argument = 3; argument < argc; ++argument) {
        const std::string_view option(argv[argument]);
        if (option == "--fp16-sh" && !packed)
            packed = true;
        else if (option == "--budget-mib" && !budget_set && argument + 1 < argc) {
            const std::string_view text(argv[++argument]);
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), budget_mib);
            require(parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() &&
                        budget_mib >= 512 && budget_mib <= 4096,
                    "budget must be an integer from 512 through 4096 MiB");
            budget_set = true;
        } else
            throw std::runtime_error("unknown, duplicate or missing diagnostic option");
    }
    const std::filesystem::path output(argv[2]);
    require(std::filesystem::create_directory(output), "output must be a new directory");
    json report{{"scope", "synthetic 2M resident admission/memory; no FPS or capture gate"},
                {"passed", false},
                {"storage", packed ? "FP32_GEOMETRY_FP16_SH" : "FP32"},
                {"residency_budget_bytes", budget_mib * 1024 * 1024},
                {"frames", json::array()},
                {"views", json::array()}};
    try {
        bundle_limits limits;
        limits.scene.max_splats = SCALE_FIXTURE_SPLATS;
        limits.scene.max_chunks = 8;
        limits.scene.max_payload_bytes = 512ULL * 1024 * 1024;
        const auto scene = load_bundle(argv[1], limits);
        require(scene.has_value(), scene ? "" : scene.error().message);
        const auto content = scene_content_hash(**scene);
        require(content == SCALE_FIXTURE_SHA256, "bundle differs from frozen scale fixture");
        report["scene_content_sha256"] = content;
        const auto payload = validate_scene(**scene, limits.scene);
        require(payload.has_value(), "invalid full fixture payload");
        report["cpu_payload_bytes"] = *payload;
        require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        struct sdl_owner {
            ~sdl_owner() { SDL_Quit(); }
        } sdl;
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Synthetic World Explorer 2M scale check", SCALE_FIXTURE_WIDTH,
                             SCALE_FIXTURE_HEIGHT, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN),
            SDL_DestroyWindow);
        require(window != nullptr, SDL_GetError());
        renderer_options options;
        options.max_splats = SCALE_FIXTURE_SPLATS;
        options.residency_bytes = budget_mib * 1024 * 1024;
        options.collect_timings = true;
        options.splat_storage =
            packed ? gpu_splat_storage::FP32_GEOMETRY_FP16_SH : gpu_splat_storage::FP32;
        options.shader_directory = WE_SHADER_DIRECTORY;
        auto created = vulkan_renderer::create(window.get(), options);
        require(created.has_value(), created ? "" : created.error().message);
        auto renderer = std::move(*created);
        require(renderer->capabilities().validation_enabled, "native Vulkan validation required");
        report["device"] = renderer->capabilities().device_name;
        report["video_driver"] = SDL_GetCurrentVideoDriver();
        report["splat_stride_bytes"] = renderer->capabilities().splat_stride_bytes;
        report["gpu_timestamps_supported"] = renderer->capabilities().gpu_timestamps_supported;
        std::size_t peak_owned{}, peak_staging{}, peak_scratch{}, peak_framebuffer{};
        std::optional<std::size_t> peak_hwm, peak_rss;
        std::map<std::uint64_t, renderer_frame_timing> samples;
        const auto sample = [&] {
            const auto stats = renderer->diagnostics();
            require(stats.validation_errors == 0 &&
                        stats.allocated_bytes <= options.residency_bytes,
                    "validation or residency failure");
            require(stats.framebuffer_width == SCALE_FIXTURE_WIDTH &&
                        stats.framebuffer_height == SCALE_FIXTURE_HEIGHT,
                    "framebuffer differs from fixed 1080p calibration");
            peak_owned = std::max(peak_owned, stats.allocated_bytes);
            peak_staging = std::max(peak_staging, stats.staging_bytes);
            peak_scratch = std::max(peak_scratch, stats.scratch_bytes);
            peak_framebuffer = std::max(peak_framebuffer, stats.framebuffer_bytes);
            for (const auto& timing : stats.completed_timings)
                if (timing.sequence)
                    samples.try_emplace(timing.sequence, timing);
            // status RSS/HWM counters are approximate and can fluctuate. Retain
            // every raw sample and its maximum; rollup provides a separate RSS
            // observation with explicit sampling scope.
            const auto hwm = process_bytes("VmHWM:");
            const auto rss = process_bytes("Rss:", true);
            if (hwm)
                peak_hwm = std::max(peak_hwm.value_or(0), *hwm);
            if (rss)
                peak_rss = std::max(peak_rss.value_or(0), *rss);
            report["frames"].push_back({{"sequence", stats.submitted_frames},
                                        {"resident_splats", stats.resident_splat_count},
                                        {"references", stats.tile_reference_count},
                                        {"owned_bytes", stats.allocated_bytes},
                                        {"staging_bytes", stats.staging_bytes},
                                        {"scratch_bytes", stats.scratch_bytes},
                                        {"framebuffer_bytes", stats.framebuffer_bytes},
                                        {"rss_bytes", optional_bytes(process_bytes("VmRSS:"))},
                                        {"process_hwm_bytes", optional_bytes(hwm)},
                                        {"smaps_rss_bytes", optional_bytes(rss)}});
            report["peak_sampled_owned_bytes"] = peak_owned;
            report["peak_sampled_staging_bytes"] = peak_staging;
            report["peak_sampled_scratch_bytes"] = peak_scratch;
            report["framebuffer_bytes"] = peak_framebuffer;
            report["process_hwm_bytes"] = optional_bytes(peak_hwm);
            report["peak_sampled_rss_bytes"] = optional_bytes(peak_rss);
            report["validation_errors"] = stats.validation_errors;
            if (peak_hwm)
                require(*peak_hwm <= 4ULL * 1024 * 1024 * 1024,
                        "sampled process high-water exceeds the unchanged 4 GiB bound");
            if (peak_rss)
                require(*peak_rss <= 4ULL * 1024 * 1024 * 1024,
                        "sampled process RSS exceeds the unchanged 4 GiB bound");
        };
        const auto draw = [&](const camera_view& camera) {
            SDL_PumpEvents();
            const auto drawn = renderer->draw(camera);
            require(drawn.has_value(), drawn ? "" : drawn.error().message);
            sample();
        };
        const auto capture = [&](const char* name, const camera_view& camera) {
            const auto path = output / (std::string(name) + ".ppm");
            require(renderer->request_screenshot(path).has_value(), "readback request failed");
            for (int frame = 0; frame < 8 && !std::filesystem::exists(path); ++frame)
                draw(camera);
            require(std::filesystem::exists(path), "readback did not complete");
            require(renderer->wait_for_idle().has_value(), "GPU drain failed");
            sample();
            return digest(path);
        };
        const auto admit = [&](const scene_snapshot& snapshot, operation_id operation) {
            auto batch = make_snapshot_batch(snapshot, operation);
            require(batch.has_value(), "fixture snapshot is invalid");
            const auto ticket = renderer->updates().enqueue(*batch);
            require(ticket.has_value(), ticket ? "" : ticket.error().message);
            return *ticket;
        };
        const auto await = [&](update_ticket ticket) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
            for (int frame = 0; frame < 256 && std::chrono::steady_clock::now() < deadline;
                 ++frame) {
                draw(scale_fixture_camera());
                const auto progress = renderer->updates().query_progress(ticket);
                require(progress.has_value(), "upload progress missing");
                if (progress->state == update_state::FAILED ||
                    progress->state == update_state::RENDER_VISIBLE)
                    return *progress;
            }
            throw std::runtime_error("bounded fixture upload did not complete");
        };
        auto empty = std::make_shared<scene_state>(**scene);
        empty->chunks.clear();
        require(await(admit(empty, {1})).state == update_state::RENDER_VISIBLE,
                "empty baseline failed");
        const auto baseline_hash = capture("baseline", scale_fixture_camera());
        auto full = std::make_shared<scene_state>(**scene);
        full->revision = {2};
        const auto full_hash = scene_content_hash(*full);
        report["upload_scene_content_sha256"] = full_hash;
        const auto progress = await(admit(full, {2}));
        if (progress.state == update_state::FAILED) {
            report["admission_error"] = progress.failure ? progress.failure->message : "unknown";
            report["rejected_admission_preserved"] =
                renderer->updates().visible_snapshot() == empty &&
                renderer->diagnostics().resident_splat_count == 0 &&
                capture("rejected", scale_fixture_camera()) == baseline_hash &&
                scene_content_hash(*full) == full_hash;
            require(report.at("rejected_admission_preserved").get<bool>(),
                    "rejected admission changed prior scene or caller data");
            throw std::runtime_error("full scale fixture was rejected: " +
                                     report.at("admission_error").get<std::string>());
        }
        std::array<std::string, 3> hashes;
        constexpr std::array NAMES{"front", "reverse", "front-return"};
        for (std::size_t view = 0; view < NAMES.size(); ++view) {
            const auto camera = scale_fixture_camera(view == 1);
            for (int warmup = 0; warmup < 4; ++warmup)
                draw(camera);
            hashes[view] = capture(NAMES[view], camera);
            const auto stats = renderer->diagnostics();
            require(stats.resident_splat_count == SCALE_FIXTURE_SPLATS &&
                        stats.tile_reference_count == SCALE_FIXTURE_REFERENCES,
                    "full resident count or analytic tile coverage differs");
            const auto pixels = foreground(output / (std::string(NAMES[view]) + ".ppm"));
            require(pixels > std::size_t{SCALE_FIXTURE_WIDTH} * SCALE_FIXTURE_HEIGHT / 4,
                    "capture lacks the full bank's foreground");
            report["views"].push_back({{"view", NAMES[view]},
                                       {"sha256", hashes[view]},
                                       {"resident_splats", stats.resident_splat_count},
                                       {"tile_references", stats.tile_reference_count},
                                       {"foreground_pixels", pixels}});
        }
        require(hashes[0] == hashes[2] && hashes[0] != hashes[1],
                "bank switch or return pixels differ from fixture invariants");
        std::size_t host{}, resident{};
        for (const auto& [sequence, timing] : samples) {
            static_cast<void>(sequence);
            require(timing.host_copy_bytes + timing.resident_copy_bytes <= 15ULL * 1024 * 1024,
                    "per-frame transfer budget exceeded");
            host += timing.host_copy_bytes;
            resident += timing.resident_copy_bytes;
        }
        require(host == 16 + SCALE_FIXTURE_SPLATS * renderer->capabilities().splat_stride_bytes &&
                    resident == 0,
                "upload bytes differ from the complete resident payload");
        report["host_copy_bytes"] = host;
        report["resident_copy_bytes"] = resident;
        report["canonical_scene_unchanged"] = scene_content_hash(*full) == full_hash;
        require(report.at("canonical_scene_unchanged").get<bool>(), "caller scene changed");
        report["passed"] = true;
    } catch (const std::exception& error) {
        report["error"] = error.what();
    }
    std::ofstream stream(output / "native.json", std::ios::binary | std::ios::noreplace);
    stream << report.dump(2) << '\n';
    stream.close();
    require(stream.good(), "cannot save scale diagnostic report");
    std::cout << report.at("passed") << " " << report.value("error", std::string{}) << '\n';
    return report.at("passed").get<bool>() ? 0 : 2;
} catch (const std::exception& error) {
    std::cerr << "Scale diagnostic failed: " << error.what() << '\n';
    return 2;
}
