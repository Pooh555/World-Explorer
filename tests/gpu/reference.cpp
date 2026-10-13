#include "world_explorer/core/hash.hpp"
#include "world_explorer/render_vulkan/renderer.hpp"
#include "world_explorer/scene_store/bundle.hpp"
#include <SDL3/SDL.h>
#include <bit>
#include <cmath>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <span>
#include <stdexcept>

using namespace world_explorer;
namespace {
using json = nlohmann::json;
constexpr std::uint32_t WIDTH = 1280, HEIGHT = 720;
void require(bool condition, const std::string& message) {
    if (!condition)
        throw std::runtime_error(message);
}
json vector(vec3d value) { return {value.x, value.y, value.z}; }
void write_json(const std::filesystem::path& path, const json& value) {
    std::ofstream stream(path);
    stream << value.dump(2) << '\n';
    require(stream.good(), "cannot write reference fixture/result");
}
scene_snapshot fixture() {
    auto scene = std::make_shared<scene_state>();
    scene->id = {71};
    scene->revision = {1};
    scene->provenance = {{"source_kind", "synthetic_diagnostic"},
                         {"fixture", "asymmetric_render_reference_v3"}};
    auto chunk = std::make_shared<scene_chunk>();
    chunk->id = {1};
    chunk->bounds = {{-10, -10, -60}, {10, 10, 6}};
    for (int index = 0; index < 12; ++index) {
        gaussian splat;
        const auto row = index / 4;
        splat.mean = {static_cast<float>((index % 4 - 1.5) * 0.7),
                      static_cast<float>((row - 1) * 0.65), -0.1F * static_cast<float>(index)};
        splat.scale = {0.25F, 0.08F + 0.015F * static_cast<float>(index % 3), 0.12F};
        const auto angle = 0.17 * index;
        splat.rotation = {static_cast<float>(std::cos(angle)), 0, 0,
                          static_cast<float>(std::sin(angle))};
        splat.opacity = 0.35F + 0.045F * static_cast<float>(index);
        splat.sh_degree = 3;
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const auto color =
                0.12F + 0.22F * static_cast<float>((static_cast<std::size_t>(index) + channel) % 4);
            splat.sh[channel] = (color - 0.5F) / 0.2820947918F;
            for (std::size_t coefficient = 1; coefficient < 16; ++coefficient)
                splat.sh[coefficient * 3 + channel] = static_cast<float>(
                    0.025 * std::sin(index + static_cast<double>(coefficient) * 1.3 +
                                     static_cast<double>(channel)));
        }
        chunk->splats.push_back(splat);
    }
    auto edge = chunk->splats.front();
    // An off-screen center with a broad, oblique footprint exercises Jacobian clipping.
    edge.mean = {6, 0.2F, 2};
    edge.scale = {0.3F, 0.4F, 4.F};
    edge.rotation = {1, 0, 0, 0};
    edge.opacity = 0.9F;
    for (std::size_t channel = 0; channel < 3; ++channel)
        edge.sh[channel] = (0.9F - 0.5F) / 0.2820947918F;
    chunk->splats.push_back(edge);
    auto clipped = edge;
    clipped.mean = {0, 0, 5.995F}; // Behind the 0.01 near plane.
    chunk->splats.push_back(clipped);
    clipped.mean = {0, 0, -50}; // Beyond the 20-unit far plane.
    chunk->splats.push_back(clipped);
    scene->chunks.emplace(chunk->id, chunk);
    auto transformed = std::make_shared<scene_chunk>();
    transformed->id = {2};
    transformed->bounds = {{-1, -1, -1}, {1, 1, 1}};
    transformed->world_from_chunk = {{std::cos(0.3), 0, 0, std::sin(0.3)}, {0.3, 0.2, 1}, 0.9};
    for (std::size_t index = 0; index < 3; ++index) {
        auto overlap = chunk->splats[index + 4];
        overlap.mean = {0.1F * static_cast<float>(index), 0.1F * static_cast<float>(index),
                        -0.2F * static_cast<float>(index)};
        overlap.scale = {0.45F, 0.16F, 0.09F};
        overlap.opacity = 0.65F;
        transformed->splats.push_back(overlap);
    }
    scene->chunks.emplace(transformed->id, transformed);
    return scene;
}
scene_snapshot compositing_fixture(bool termination) {
    auto scene = std::make_shared<scene_state>();
    scene->id = {termination ? 73ULL : 72ULL};
    scene->revision = {1};
    scene->provenance = {
        {"source_kind", "synthetic_diagnostic"},
        {"fixture", termination ? "exclusive_termination_v1" : "compositing_stress_v1"}};
    auto chunk = std::make_shared<scene_chunk>();
    chunk->id = {1};
    chunk->bounds = {{-2, -2, -2}, {2, 2, 2}};
    const auto add = [&](std::array<float, 3> mean, float opacity, std::array<float, 3> color) {
        gaussian splat;
        splat.mean = mean;
        splat.scale = {0.5F, 0.45F, 0.08F};
        splat.opacity = opacity;
        splat.sh_degree = 3;
        for (std::size_t channel = 0; channel < 3; ++channel)
            splat.sh[channel] = (color[channel] - 0.5F) / 0.2820947918F;
        chunk->splats.push_back(splat);
    };
    if (termination) {
        // Normalized colors, deliberately near the pinned exclusive cutoff.
        // This must remain a separate case even if the raster path fails it.
        add({0, 0, 0.4F}, 0.95F, {0, 0, 0});
        add({0, 0, -0.4F}, 1.F, {1, 1, 1});
        scene->chunks.emplace(chunk->id, chunk);
        return scene;
    }
    // A bright rear contribution must retain its energy until the dark front
    // layer attenuates it. Per-fragment/per-layer UNORM clamping is incorrect.
    add({-1.2F, 0, 0.4F}, 0.8F, {0, 0, 0});
    add({-1.2F, 0, -0.4F}, 0.8F, {6, 2, 0.5F});
    // Distinct depths avoid tie-order ambiguity. The pinned reference terminates
    // exclusively at T <= 1e-4; normalized colors bound the omitted tail energy.
    for (int index = 0; index < 24; ++index)
        add({1.2F, 0, 1.F - 0.06F * static_cast<float>(index)}, 0.82F,
            {0.12F, 0.35F, index % 2 ? 0.2F : 0.88F});
    add({1.2F, 0, -1.F}, 0.8F, {6, 2, 0.5F});
    scene->chunks.emplace(chunk->id, chunk);
    return scene;
}
scene_snapshot batch_fixture() {
    auto scene = std::make_shared<scene_state>();
    scene->id = {74};
    scene->revision = {1};
    scene->provenance = {{"source_kind", "synthetic_diagnostic"}, {"fixture", "tile_batches_v1"}};
    auto chunk = std::make_shared<scene_chunk>();
    chunk->id = {1};
    chunk->bounds = {{-2, -2, -2}, {2, 2, 2}};
    for (std::size_t index = 0; index < 1027; ++index) {
        gaussian splat;
        splat.mean = {1.2F * (static_cast<float>(index % 3) - 1), 0,
                      0.5F - 0.001F * static_cast<float>(index)};
        splat.scale = {0.5F, 0.45F, 0.08F};
        splat.opacity = 0.03F;
        splat.sh_degree = 3;
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const auto color = static_cast<float>((index + channel) % 7) / 6;
            splat.sh[channel] = (color - 0.5F) / 0.2820947918F;
        }
        chunk->splats.push_back(splat);
    }
    scene->chunks.emplace(chunk->id, chunk);
    return scene;
}
json export_chunks(const scene_state& scene) {
    json chunks = json::array();
    for (const auto& [id, chunk] : scene.chunks) {
        static_cast<void>(id);
        const auto& transform = chunk->world_from_chunk;
        const auto q = transform.rotation;
        json splats = json::array();
        for (const auto& splat : chunk->splats)
            splats.push_back({{"mean", splat.mean},
                              {"scale", splat.scale},
                              {"rotation", splat.rotation},
                              {"opacity", splat.opacity},
                              {"sh", splat.sh},
                              {"sh_degree", splat.sh_degree}});
        chunks.push_back({{"rotation", {q.w, q.x, q.y, q.z}},
                          {"translation", vector(transform.translation)},
                          {"scale", transform.scale},
                          {"splats", std::move(splats)}});
    }
    return chunks;
}
json read_json(const std::filesystem::path& path) {
    require(std::filesystem::file_size(path) <= std::size_t{4} * 1024 * 1024,
            "fit metadata exceeds diagnostic budget");
    std::ifstream stream(path);
    return json::parse(stream);
}
std::string file_hash(const std::filesystem::path& path, std::size_t budget) {
    auto hash = sha256_file(path, budget);
    require(hash.has_value(), hash ? "" : hash.error().message);
    return *hash;
}
std::uint32_t image_dimension(const json& value) {
    require(value.is_number_unsigned() && value.get<std::uint64_t>() > 0 &&
                value.get<std::uint64_t>() <= 4096,
            "fit image dimension exceeds diagnostic budget");
    return value.get<std::uint32_t>();
}
json export_binary_chunks(const scene_state& scene, const std::filesystem::path& output) {
    json chunks = json::array();
    for (const auto& [id, chunk] : scene.chunks) {
        const auto name = "chunk-" + std::to_string(id.value) + ".f32";
        std::ofstream stream(output / name, std::ios::binary);
        // Version 2 diagnostic rows: mean, scale, wxyz, opacity, 16 RGB SH,
        // degree; exactly 60 little-endian float32 values, no native padding.
        const auto put = [&](float value) {
            const auto bits = std::bit_cast<std::uint32_t>(value);
            for (unsigned shift = 0; shift < 32; shift += 8)
                stream.put(static_cast<char>((bits >> shift) & 255U));
        };
        for (const auto& splat : chunk->splats) {
            for (auto value : splat.mean)
                put(value);
            for (auto value : splat.scale)
                put(value);
            for (auto value : splat.rotation)
                put(value);
            put(splat.opacity);
            for (auto value : splat.sh)
                put(value);
            put(static_cast<float>(splat.sh_degree));
        }
        stream.close();
        require(stream.good(), "cannot write binary diagnostic splats");
        const auto& transform = chunk->world_from_chunk;
        const auto q = transform.rotation;
        chunks.push_back({{"rotation", {q.w, q.x, q.y, q.z}},
                          {"translation", vector(transform.translation)},
                          {"scale", transform.scale},
                          {"file", name},
                          {"count", chunk->splats.size()},
                          {"sha256", file_hash(output / name, std::size_t{240} * 1000000)}});
    }
    return chunks;
}
camera_view fit_camera(const json& frame) {
    const auto& view = frame.at("view");
    const auto& intrinsic = frame.at("intrinsic");
    require(view.size() == 4 && intrinsic.size() == 3, "invalid fit camera matrices");
    for (const auto& row : view)
        require(row.size() == 4, "invalid fit view row");
    for (const auto& row : intrinsic)
        require(row.size() == 3, "invalid fit intrinsic row");
    require(view.at(3) == json::array({0, 0, 0, 1}) && intrinsic.at(2) == json::array({0, 0, 1}) &&
                intrinsic.at(0).at(1) == 0 && intrinsic.at(1).at(0) == 0,
            "unsupported fit camera matrix");
    // Fit uses native COLMAP world; canonical scene applies Rx(pi).
    const auto canonical = [](vec3d value) { return vec3d{value.x, -value.y, -value.z}; };
    camera_view camera;
    camera.right = canonical({view.at(0).at(0), view.at(0).at(1), view.at(0).at(2)});
    camera.down = canonical({view.at(1).at(0), view.at(1).at(1), view.at(1).at(2)});
    camera.forward = canonical({view.at(2).at(0), view.at(2).at(1), view.at(2).at(2)});
    // The stored float32 rotation is only approximately orthonormal. Invert
    // its actual rows, preserving the saved affine translation exactly.
    const auto determinant = dot(camera.right, cross(camera.down, camera.forward));
    require(std::isfinite(determinant) && std::abs(determinant - 1) < 1e-4,
            "invalid fit rotation determinant");
    camera.position = (cross(camera.down, camera.forward) * -view.at(0).at(3).get<double>() +
                       cross(camera.forward, camera.right) * -view.at(1).at(3).get<double>() +
                       cross(camera.right, camera.down) * -view.at(2).at(3).get<double>()) *
                      (1 / determinant);
    camera.calibration = calibrated_projection{{intrinsic.at(0).at(0), intrinsic.at(1).at(1),
                                                intrinsic.at(0).at(2).get<double>() - 0.5,
                                                intrinsic.at(1).at(2).get<double>() - 0.5},
                                               image_dimension(frame.at("width")),
                                               image_dimension(frame.at("height"))};
    const auto valid_camera =
        raster_intrinsics(camera, camera.calibration->width, camera.calibration->height);
    require(valid_camera.has_value(), valid_camera ? "" : valid_camera.error().message);
    return camera;
}
} // namespace
int main(int argc, char** argv) {
    try {
        const std::span<char*> arguments(argv, static_cast<std::size_t>(argc));
        const bool timings =
            arguments.size() > 2 && std::string_view(arguments.back()) == "--timings";
        if (timings)
            --argc;
        const bool real = argc == 5 && std::string_view(argv[2]) == "--real";
        require(real || argc == 2 ||
                    (argc == 3 && (std::string_view(argv[2]) == "--compositing" ||
                                   std::string_view(argv[2]) == "--termination" ||
                                   std::string_view(argv[2]) == "--batches" ||
                                   std::string_view(argv[2]) == "--batches-bounded")),
                "usage: we_gpu_reference <new output directory> "
                "[--compositing|--termination|--batches|--batches-bounded] or "
                "we_gpu_reference <new output directory> --real <scene bundle> <completed fit>; "
                "append --timings for completed GPU/CPU timing checks");
        const bool compositing = argc == 3;
        const bool termination = compositing && std::string_view(argv[2]) == "--termination";
        const bool bounded = compositing && std::string_view(argv[2]) == "--batches-bounded";
        const bool batches = bounded || (compositing && std::string_view(argv[2]) == "--batches");
        const std::filesystem::path output(argv[1]);
        require(std::filesystem::create_directory(output), "output directory must not exist");
        scene_snapshot scene = real          ? scene_snapshot{}
                               : batches     ? batch_fixture()
                               : compositing ? compositing_fixture(termination)
                                             : fixture();
        std::uint32_t image_width = WIDTH, image_height = HEIGHT;
        json fit_frames, identity;
        if (real) {
            bundle_limits limits;
            limits.scene.max_chunks = 1024;
            limits.scene.max_splats = 1000000;
            limits.scene.max_payload_bytes = std::size_t{512} * 1024 * 1024;
            auto loaded = load_bundle(argv[3], limits);
            require(loaded.has_value(), loaded ? "" : loaded.error().message);
            scene = *loaded;
            const std::filesystem::path fit(argv[4]);
            const auto result = read_json(fit / "result.json");
            const auto input = read_json(fit / "input_manifest.json");
            const auto& provenance = scene->provenance;
            require(provenance.at("source_kind") == "video_reconstruction" &&
                        provenance.at("fit_identity") == result.at("identity").get<std::string>() &&
                        provenance.at("input_sha256") ==
                            result.at("ply_sha256").get<std::string>() &&
                        provenance.at("capture_sha256") ==
                            result.at("source_sha256").get<std::string>() &&
                        input.at("source_sha256") == result.at("source_sha256") &&
                        result.at("schema_version") == 1 && result.at("status") == "complete" &&
                        input.at("color_domain") == "srgb" && input.at("sh_degree") == 3 &&
                        input.at("rasterize_mode") == "classic" &&
                        input.at("dataset").at("world_transform") ==
                            "native COLMAP; canonical chunk applies Rx(pi)",
                    "scene and completed fit identities/profile differ");
            const std::filesystem::path ply_relative(result.at("ply").get<std::string>());
            require(!ply_relative.empty() && !ply_relative.is_absolute(), "invalid fit asset path");
            const auto fit_root = std::filesystem::canonical(fit);
            const auto ply_path = std::filesystem::canonical(fit / ply_relative);
            const auto within_fit = ply_path.lexically_relative(fit_root);
            require(!within_fit.empty() && *within_fit.begin() != "..",
                    "fit asset escaped directory");
            require(file_hash(ply_path, std::size_t{512} * 1024 * 1024) ==
                        result.at("ply_sha256").get<std::string>(),
                    "original fitted asset checksum mismatch");
            std::size_t count = 0;
            for (const auto& [id, chunk] : scene->chunks) {
                static_cast<void>(id);
                count += chunk->splats.size();
            }
            require(count > 0 && result.at("splats").is_number_unsigned() &&
                        count == result.at("splats").get<std::uint64_t>(),
                    "fit splats were trimmed or invalid");
            fit_frames = input.at("dataset").at("frames");
            require(fit_frames.size() >= 3 && fit_frames.size() <= 120,
                    "fit frame budget exceeded");
            image_width = image_dimension(fit_frames.at(0).at("width"));
            image_height = image_dimension(fit_frames.at(0).at("height"));
            identity = {{"fit_identity", result.at("identity")},
                        {"source_sha256", result.at("source_sha256")},
                        {"ply_sha256", result.at("ply_sha256")},
                        {"fit_input_sha256",
                         file_hash(fit / "input_manifest.json", std::size_t{4} * 1024 * 1024)},
                        {"fit_result_sha256",
                         file_hash(fit / "result.json", std::size_t{4} * 1024 * 1024)}};
            // Preserve exact fit metadata for independent camera/identity checks.
            std::filesystem::copy_file(fit / "input_manifest.json", output / "fit-input.json");
            std::filesystem::copy_file(fit / "result.json", output / "fit-result.json");
        } else {
            auto saved = write_bundle(output / "fixture.wexplore", scene);
            require(saved.has_value(), saved ? "" : saved.error().message);
        }
        const bool initialized = SDL_Init(SDL_INIT_VIDEO);
        require(initialized, SDL_GetError());
        struct sdl_guard {
            ~sdl_guard() { SDL_Quit(); }
        } sdl;
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow(real ? "Real calibrated comparison"
                                  : "Synthetic fixed-camera comparison",
                             static_cast<int>(image_width), static_cast<int>(image_height),
                             SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN),
            SDL_DestroyWindow);
        require(window != nullptr, SDL_GetError());
        renderer_options options;
        options.collect_timings = timings;
        options.max_splats = real ? 1000000 : 250000;
        if (bounded)
            options.max_tile_references = 2048;
        options.shader_directory = WE_SHADER_DIRECTORY;
        auto created = vulkan_renderer::create(window.get(), options);
        require(created.has_value(), created ? "" : created.error().message);
        auto renderer = std::move(*created);
        require(renderer->capabilities().validation_enabled, "Vulkan validation is required");
        auto batch = make_snapshot_batch(scene, {1}).value();
        auto admitted = renderer->updates().enqueue(batch);
        require(admitted.has_value(), admitted ? "" : admitted.error().message);
        camera_view wrong_extent;
        wrong_extent.calibration = calibrated_projection{{300, 300, 100, 100}, 640, 360};
        require(!renderer->draw(wrong_extent), "mismatched calibration was admitted");
        const auto unchanged = renderer->updates().query_progress(*admitted);
        require(unchanged.has_value(), "invalid camera lost update ticket");
        require(renderer->diagnostics().submitted_frames == 0 &&
                    unchanged->state == update_state::ACCEPTED,
                "invalid camera consumed the pending scene update");
        json views = json::array();
        std::vector<std::string> names{"centered", "off_center", "rolled", "translated"};
        if (compositing)
            names.insert(names.end(), {"orbit_left", "orbit_right", "close_up"});
        if (real)
            names = {"fit_first", "fit_middle", "fit_last"};
        for (const auto& name : names) {
            camera_view camera;
            camera.position = {0, 0, 6};
            camera.far_plane = 20;
            camera.calibration = calibrated_projection{{900, 840, 639.5, 359.5}, WIDTH, HEIGHT};
            if (name != "centered")
                camera.calibration->intrinsics = {930, 790, 481.25, 302.75};
            if (name == "rolled" || name == "translated") {
                camera.right = {std::cos(0.37), -std::sin(0.37), 0};
                camera.down = {-std::sin(0.37), -std::cos(0.37), 0};
            }
            if (name == "translated")
                camera.position = {-0.3, 0.15, 6.4};
            if (name == "orbit_left" || name == "orbit_right") {
                const auto yaw = name == "orbit_left" ? -0.6 : 0.6;
                camera.position = {6 * std::sin(yaw), 0, 6 * std::cos(yaw)};
                camera.right = {std::cos(yaw), 0, -std::sin(yaw)};
                camera.forward = {-std::sin(yaw), 0, -std::cos(yaw)};
            }
            if (name == "close_up")
                camera.position = {0.2, 0.1, 2.8};
            std::size_t fit_index = 0;
            if (real) {
                fit_index = name == "fit_first"    ? 0
                            : name == "fit_middle" ? fit_frames.size() / 2
                                                   : fit_frames.size() - 1;
                camera = fit_camera(fit_frames.at(fit_index));
                require(camera.calibration->width == image_width &&
                            camera.calibration->height == image_height,
                        "fit cameras have different dimensions");
            }
            const auto k = camera.calibration->intrinsics;
            views.push_back({{"name", name},
                             {"position", vector(camera.position)},
                             {"right", vector(camera.right)},
                             {"down", vector(camera.down)},
                             {"forward", vector(camera.forward)},
                             {"intrinsics", {k.fx, k.fy, k.cx, k.cy}},
                             {"near", camera.near_plane},
                             {"far", camera.far_plane}});
            if (real) {
                views.back()["fit_frame_index"] = fit_index;
                views.back()["fit_frame_name"] = fit_frames.at(fit_index).at("name");
            }
            bool captured = false;
            for (int frame = 0; frame < 120 && !captured; ++frame) {
                SDL_PumpEvents();
                int width{}, height{};
                SDL_GetWindowSizeInPixels(window.get(), &width, &height);
                require(width == static_cast<int>(image_width) &&
                            height == static_cast<int>(image_height),
                        "native framebuffer extent changed");
                auto status = renderer->updates().query_progress(*admitted);
                require(status.has_value(), status ? "" : status.error().message);
                if (status->state == update_state::RENDER_VISIBLE) {
                    auto capture = renderer->request_screenshot(output / (name + ".ppm"));
                    require(capture.has_value(), capture ? "" : capture.error().message);
                    captured = true;
                }
                const auto drawn = renderer->draw(camera);
                require(drawn.has_value(), drawn ? "" : drawn.error().message);
            }
            require(captured && std::filesystem::exists(output / (name + ".ppm")),
                    "scene never became visible/captured");
        }
        require(renderer->wait_for_idle().has_value(), "GPU completion failed");
        const auto stats = renderer->diagnostics();
        require(stats.validation_errors == 0, "Vulkan validation errors");
        if (timings)
            for (const auto& sample : stats.completed_timings) {
                require(sample.sequence > 0 && sample.sequence <= stats.submitted_frames &&
                            sample.gpu_valid == renderer->capabilities().gpu_timestamps_supported,
                        "completed timing sample missing or unsupported");
                require(sample.count_submissions > 0 && sample.tile_batches > 0,
                        "appearance timing did not retain count/batch work");
                if (bounded)
                    require(sample.tile_batches > 1, "bounded timing did not exercise query reuse");
            }
        if (real) {
            std::size_t expected_count = 0;
            for (const auto& [id, chunk] : scene->chunks) {
                static_cast<void>(id);
                expected_count += chunk->splats.size();
            }
            require(stats.resident_splat_count == expected_count,
                    "renderer trimmed the real scene");
        }
        json diagnostic = {
            {"schema", real ? 2 : 1},
            {"label", real ? "real calibrated retained fit; no quality/performance certification"
                      : batches     ? "synthetic tile batches v1; not a capture"
                      : termination ? "synthetic exclusive termination v1; not a capture"
                      : compositing ? "synthetic compositing stress v1; not a capture"
                                    : "synthetic asymmetric reference v3; not a capture"},
            {"width", image_width},
            {"height", image_height},
            {"background", {0.045F, 0.051F, 0.063F}},
            {"chunks", real ? export_binary_chunks(*scene, output) : export_chunks(*scene)},
            {"views", views},
            {"scene_content_sha256", scene_content_hash(*scene)}};
        if (real) {
            diagnostic["identity"] = identity;
            diagnostic["splat_count"] = stats.resident_splat_count;
            diagnostic["render_origin"] = views.at(0).at("position");
            diagnostic["row_format"] =
                "little-endian float32[60]: mean3,scale3,wxyz4,opacity,sh48,degree";
        }
        write_json(output / "fixture.json", diagnostic);
        write_json(output / "native.json",
                   {{"device", renderer->capabilities().device_name},
                    {"timing_collection", timings},
                    {"gpu_timestamps_supported", renderer->capabilities().gpu_timestamps_supported},
                    {"timestamp_valid_bits", renderer->capabilities().timestamp_valid_bits},
                    {"timestamp_period_ns", renderer->capabilities().timestamp_period_ns},
                    {"fp32_compositing", renderer->capabilities().fp32_compositing},
                    {"framebuffer_bytes", stats.framebuffer_bytes},
                    {"allocated_bytes", stats.allocated_bytes},
                    {"validation_errors", stats.validation_errors},
                    {"splat_count", stats.resident_splat_count},
                    {"tile_references", stats.tile_reference_count},
                    {"submitted_frames", stats.submitted_frames}});
        std::cout << (real ? "Real calibrated retained fit: " : "Synthetic fixed-camera fixture: ")
                  << stats.resident_splat_count << " splats, " << names.size() << " views, "
                  << image_width << 'x' << image_height << ", validation_errors=0\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Fixed-camera rendering failed: " << error.what() << '\n';
        return 2;
    }
}
