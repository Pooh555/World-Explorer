#include "clipping_fixture.hpp"
#include "fit_view_selection.hpp"
#include "rebasing_fixture.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/render_vulkan/renderer.hpp"
#include "world_explorer/scene_store/bundle.hpp"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <bit>
#include <cmath>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>

using namespace world_explorer;

namespace {

using Json = nlohmann::json;
constexpr std::uint32_t K_WIDTH { 1280 };
constexpr std::uint32_t K_HEIGHT { 720 };

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

Json vector(Vec3d value) {
    return { value.m_x, value.m_y, value.m_z };
}

void write_json(const std::filesystem::path& path, const Json& value) {
    std::ofstream stream(path);
    stream << value.dump(2) << '\n';
    require(stream.good(), "cannot write reference fixture/result");
}

SceneSnapshot fixture() {
    auto scene = std::make_shared<SceneState>();
    scene->m_id = { 71 };
    scene->m_revision = { 1 };
    scene->m_provenance = { { "source_kind", "synthetic_diagnostic" },
                           { "fixture", "asymmetric_render_reference_v3" } };
    auto chunk = std::make_shared<SceneChunk>();
    chunk->m_id = { 1 };
    chunk->m_bounds = { { -10, -10, -60 }, { 10, 10, 6 } };
    for (int index { 0 }; index < 12; ++index) {
        Gaussian splat;
        const auto K_ROW = index / 4;
        splat.m_mean = { static_cast<float>((index % 4 - 1.5) * 0.7),
                        static_cast<float>((K_ROW - 1) * 0.65),
                        -0.1F * static_cast<float>(index) };
        splat.m_scale = { 0.25F, 0.08F + 0.015F * static_cast<float>(index % 3), 0.12F };
        const auto K_ANGLE = 0.17 * index;
        splat.m_rotation = {
            static_cast<float>(std::cos(K_ANGLE)), 0, 0, static_cast<float>(std::sin(K_ANGLE)) };
        splat.m_opacity = 0.35F + 0.045F * static_cast<float>(index);
        splat.m_sh_degree = 3;
        for (std::size_t channel { 0uz }; channel < 3uz; ++channel) {
            const auto K_COLOR =
                0.12F + 0.22F * static_cast<float>((static_cast<std::size_t>(index) + channel) % 4);
            splat.m_sh[channel] = (K_COLOR - 0.5F) / 0.2820947918F;
            for (std::size_t coefficient { 1uz }; coefficient < 16uz; ++coefficient) {
                splat.m_sh[coefficient * 3 + channel] = static_cast<float>(
                    0.025 * std::sin(index + static_cast<double>(coefficient) * 1.3 +
                                     static_cast<double>(channel)));
            }
        }
        chunk->m_splats.push_back(splat);
    }
    auto edge = chunk->m_splats.front();
    // An off-screen center with a broad, oblique footprint exercises Jacobian clipping.
    edge.m_mean = { 6, 0.2F, 2 };
    edge.m_scale = { 0.3F, 0.4F, 4.F };
    edge.m_rotation = { 1, 0, 0, 0 };
    edge.m_opacity = 0.9F;
    for (std::size_t channel { 0uz }; channel < 3uz; ++channel) {
        edge.m_sh[channel] = (0.9F - 0.5F) / 0.2820947918F;
    }
    chunk->m_splats.push_back(edge);
    auto clipped = edge;
    clipped.m_mean = { 0, 0, 5.995F };  // Behind the 0.01 near plane.
    chunk->m_splats.push_back(clipped);
    clipped.m_mean = { 0, 0, -50 };  // Beyond the 20-unit far plane.
    chunk->m_splats.push_back(clipped);
    scene->m_chunks.emplace(chunk->m_id, chunk);
    auto transformed = std::make_shared<SceneChunk>();
    transformed->m_id = { 2 };
    transformed->m_bounds = { { -1, -1, -1 }, { 1, 1, 1 } };
    transformed->m_world_from_chunk = { { std::cos(0.3), 0, 0, std::sin(0.3) }, { 0.3, 0.2, 1 }, 0.9 };
    for (std::size_t index { 0uz }; index < 3uz; ++index) {
        auto overlap = chunk->m_splats[index + 4];
        overlap.m_mean = { 0.1F * static_cast<float>(index),
                          0.1F * static_cast<float>(index),
                          -0.2F * static_cast<float>(index) };
        overlap.m_scale = { 0.45F, 0.16F, 0.09F };
        overlap.m_opacity = 0.65F;
        transformed->m_splats.push_back(overlap);
    }
    scene->m_chunks.emplace(transformed->m_id, transformed);
    return scene;
}

SceneSnapshot compositing_fixture(bool termination) {
    auto scene = std::make_shared<SceneState>();
    scene->m_id = { termination ? 73ULL : 72ULL };
    scene->m_revision = { 1 };
    scene->m_provenance = {
        { "source_kind", "synthetic_diagnostic" },
        { "fixture", termination ? "exclusive_termination_v1" : "compositing_stress_v1" } };
    auto chunk = std::make_shared<SceneChunk>();
    chunk->m_id = { 1 };
    chunk->m_bounds = { { -2, -2, -2 }, { 2, 2, 2 } };
    const auto K_ADD = [&](std::array<float, 3> mean, float opacity, std::array<float, 3> color) {
        Gaussian splat;
        splat.m_mean = mean;
        splat.m_scale = { 0.5F, 0.45F, 0.08F };
        splat.m_opacity = opacity;
        splat.m_sh_degree = 3;
        for (std::size_t channel { 0uz }; channel < 3uz; ++channel) {
            splat.m_sh[channel] = (color[channel] - 0.5F) / 0.2820947918F;
        }
        chunk->m_splats.push_back(splat);
    };
    if (termination) {
        // Normalized colors, deliberately near the pinned exclusive cutoff.
        // This must remain a separate case even if the raster path fails it.
        K_ADD({ 0, 0, 0.4F }, 0.95F, { 0, 0, 0 });
        K_ADD({ 0, 0, -0.4F }, 1.F, { 1, 1, 1 });
        scene->m_chunks.emplace(chunk->m_id, chunk);
        return scene;
    }
    // A bright rear contribution must retain its energy until the dark front
    // layer attenuates it. Per-fragment/per-layer UNORM clamping is incorrect.
    K_ADD({ -1.2F, 0, 0.4F }, 0.8F, { 0, 0, 0 });
    K_ADD({ -1.2F, 0, -0.4F }, 0.8F, { 6, 2, 0.5F });
    // Distinct depths avoid tie-order ambiguity. The pinned reference terminates
    // exclusively at T <= 1e-4; normalized colors bound the omitted tail energy.
    for (int index { 0 }; index < 24; ++index) {
        K_ADD({ 1.2F, 0, 1.F - 0.06F * static_cast<float>(index) },
              0.82F,
              { 0.12F, 0.35F, index % 2 ? 0.2F : 0.88F });
    }
    K_ADD({ 1.2F, 0, -1.F }, 0.8F, { 6, 2, 0.5F });
    scene->m_chunks.emplace(chunk->m_id, chunk);
    return scene;
}

SceneSnapshot batch_fixture() {
    auto scene = std::make_shared<SceneState>();
    scene->m_id = { 74 };
    scene->m_revision = { 1 };
    scene->m_provenance = { { "source_kind", "synthetic_diagnostic" }, { "fixture", "tile_batches_v1" } };
    auto chunk = std::make_shared<SceneChunk>();
    chunk->m_id = { 1 };
    chunk->m_bounds = { { -2, -2, -2 }, { 2, 2, 2 } };
    for (std::size_t index { 0uz }; index < 1027uz; ++index) {
        Gaussian splat;
        splat.m_mean = { 1.2F * (static_cast<float>(index % 3) - 1),
                        0,
                        0.5F - 0.001F * static_cast<float>(index) };
        splat.m_scale = { 0.5F, 0.45F, 0.08F };
        splat.m_opacity = 0.03F;
        splat.m_sh_degree = 3;
        for (std::size_t channel { 0uz }; channel < 3uz; ++channel) {
            const auto K_COLOR = static_cast<float>((index + channel) % 7) / 6;
            splat.m_sh[channel] = (K_COLOR - 0.5F) / 0.2820947918F;
        }
        chunk->m_splats.push_back(splat);
    }
    scene->m_chunks.emplace(chunk->m_id, chunk);
    return scene;
}

Json export_chunks(const SceneState& scene) {
    Json chunks = Json::array();
    for (const auto& [K_ID, K_CHUNK] : scene.m_chunks) {
        static_cast<void>(K_ID);
        const auto& transform = K_CHUNK->m_world_from_chunk;
        const auto K_Q = transform.m_rotation;
        Json splats = Json::array();
        for (const auto& splat : K_CHUNK->m_splats) {
            splats.push_back({ { "mean", splat.m_mean },
                              { "scale", splat.m_scale },
                              { "rotation", splat.m_rotation },
                              { "opacity", splat.m_opacity },
                              { "sh", splat.m_sh },
                              { "sh_degree", splat.m_sh_degree } });
        }
        chunks.push_back({ { "rotation", { K_Q.m_w, K_Q.m_x, K_Q.m_y, K_Q.m_z } },
                          { "translation", vector(transform.m_translation) },
                          { "scale", transform.m_scale },
                          { "splats", std::move(splats) } });
    }
    return chunks;
}

Json read_json(const std::filesystem::path& path) {
    require(std::filesystem::file_size(path) <= 4uz * 1024 * 1024,
            "fit metadata exceeds diagnostic budget");
    std::ifstream stream(path);
    return Json::parse(stream);
}

std::string file_hash(const std::filesystem::path& path, std::size_t budget) {
    auto hash = sha256_file(path, budget);
    require(hash.has_value(), hash ? "" : hash.error().m_message);
    return *hash;
}

std::uint32_t image_dimension(const Json& value) {
    require(value.is_number_unsigned() && value.get<std::uint64_t>() > 0 &&
                value.get<std::uint64_t>() <= 4096,
            "fit image dimension exceeds diagnostic budget");
    return value.get<std::uint32_t>();
}

Json export_binary_chunks(const SceneState& scene, const std::filesystem::path& output) {
    Json chunks = Json::array();
    for (const auto& [K_ID, K_CHUNK] : scene.m_chunks) {
        const auto K_NAME = "chunk-" + std::to_string(K_ID.m_value) + ".f32";
        std::ofstream stream(output / K_NAME, std::ios::binary);
        // Version 2 diagnostic rows: mean, scale, wxyz, opacity, 16 RGB SH,
        // degree; exactly 60 little-endian float32 values, no native padding.
        const auto K_PUT = [&](float value) {
            const auto K_BITS = std::bit_cast<std::uint32_t>(value);
            for (unsigned shift { 0 }; shift < 32; shift += 8) {
                stream.put(static_cast<char>((K_BITS >> shift) & 255U));
            }
        };
        for (const auto& splat : K_CHUNK->m_splats) {
            for (auto value : splat.m_mean) {
                K_PUT(value);
            }
            for (auto value : splat.m_scale) {
                K_PUT(value);
            }
            for (auto value : splat.m_rotation) {
                K_PUT(value);
            }
            K_PUT(splat.m_opacity);
            for (auto value : splat.m_sh) {
                K_PUT(value);
            }
            K_PUT(static_cast<float>(splat.m_sh_degree));
        }
        stream.close();
        require(stream.good(), "cannot write binary diagnostic splats");
        const auto& transform = K_CHUNK->m_world_from_chunk;
        const auto K_Q = transform.m_rotation;
        chunks.push_back({ { "rotation", { K_Q.m_w, K_Q.m_x, K_Q.m_y, K_Q.m_z } },
                          { "translation", vector(transform.m_translation) },
                          { "scale", transform.m_scale },
                          { "file", K_NAME },
                          { "count", K_CHUNK->m_splats.size() },
                          { "sha256", file_hash(output / K_NAME, 240uz * 1000000) } });
    }
    return chunks;
}

CameraView fit_camera(const Json& frame) {
    const auto& view = frame.at("view");
    const auto& intrinsic = frame.at("intrinsic");
    require(view.size() == 4 && intrinsic.size() == 3, "invalid fit camera matrices");
    for (const auto& row : view) {
        require(row.size() == 4, "invalid fit view row");
    }
    for (const auto& row : intrinsic) {
        require(row.size() == 3, "invalid fit intrinsic row");
    }
    require(view.at(3) == Json::array({ 0, 0, 0, 1 }) && intrinsic.at(2) == Json::array({ 0, 0, 1 }) &&
                intrinsic.at(0).at(1) == 0 && intrinsic.at(1).at(0) == 0,
            "unsupported fit camera matrix");
    // Fit uses native COLMAP world; canonical scene applies Rx(pi).
    const auto K_CANONICAL = [](Vec3d value) {
        return Vec3d { value.m_x, -value.m_y, -value.m_z };
    };
    CameraView camera;
    camera.m_right = K_CANONICAL({ view.at(0).at(0), view.at(0).at(1), view.at(0).at(2) });
    camera.m_down = K_CANONICAL({ view.at(1).at(0), view.at(1).at(1), view.at(1).at(2) });
    camera.m_forward = K_CANONICAL({ view.at(2).at(0), view.at(2).at(1), view.at(2).at(2) });
    // The stored float32 rotation is only approximately orthonormal. Invert
    // its actual rows, preserving the saved affine translation exactly.
    const auto K_DETERMINANT = dot(camera.m_right, cross(camera.m_down, camera.m_forward));
    require(std::isfinite(K_DETERMINANT) && std::abs(K_DETERMINANT - 1) < 1e-4,
            "invalid fit rotation determinant");
    camera.m_position = (cross(camera.m_down, camera.m_forward) * -view.at(0).at(3).get<double>() +
                         cross(camera.m_forward, camera.m_right) * -view.at(1).at(3).get<double>() +
                         cross(camera.m_right, camera.m_down) * -view.at(2).at(3).get<double>()) *
                        (1 / K_DETERMINANT);
    camera.m_calibration = CalibratedProjection { { intrinsic.at(0).at(0),
                                                  intrinsic.at(1).at(1),
                                                  intrinsic.at(0).at(2).get<double>() - 0.5,
                                                  intrinsic.at(1).at(2).get<double>() - 0.5 },
                                                 image_dimension(frame.at("width")),
                                                 image_dimension(frame.at("height")) };
    const auto K_VALID_CAMERA =
        raster_intrinsics(camera, camera.m_calibration->m_width, camera.m_calibration->m_height);
    require(K_VALID_CAMERA.has_value(), K_VALID_CAMERA ? "" : K_VALID_CAMERA.error().m_message);
    return camera;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const std::span<char*> K_ARGUMENTS(argv, static_cast<std::size_t>(argc));
        const bool K_TIMINGS =
            K_ARGUMENTS.size() > 2 && std::string_view(K_ARGUMENTS.back()) == "--timings";
        if (K_TIMINGS) {
            --argc;
        }
        const auto K_RENDER_ARGUMENTS =
            K_TIMINGS ? K_ARGUMENTS.first(K_ARGUMENTS.size() - 1) : K_ARGUMENTS;
        const bool K_PACKED = K_RENDER_ARGUMENTS.size() > 2 &&
                              std::string_view(K_RENDER_ARGUMENTS.back()) == "--fp16-sh";
        if (K_PACKED) {
            --argc;
        }
        const bool K_EXPLICIT_INDICES = argc == 7 && std::string_view(argv[5]) == "--fit-indices";
        const bool K_REAL =
            (argc == 5 || K_EXPLICIT_INDICES) && std::string_view(argv[2]) == "--real";
        require(!K_PACKED || K_REAL, "--fp16-sh requires a real calibrated fit");
        const bool K_REBASING = argc == 4 && std::string_view(argv[2]) == "--rebasing";
        auto offset = rebasing_offset(K_REBASING ? argv[3] : "zero");
        require(offset.has_value(), offset ? "" : offset.error().m_message);
        require(K_REAL || K_REBASING || argc == 2 ||
                    (argc == 3 && (std::string_view(argv[2]) == "--compositing" ||
                                   std::string_view(argv[2]) == "--termination" ||
                                   std::string_view(argv[2]) == "--batches" ||
                                   std::string_view(argv[2]) == "--clipping" ||
                                   std::string_view(argv[2]) == "--batches-bounded")),
                "usage: we_gpu_reference <new output directory> "
                "[--compositing|--termination|--batches|--batches-bounded|--clipping] or "
                "--rebasing zero|positive|negative or "
                "we_gpu_reference <new output directory> --real <scene bundle> <completed fit>; "
                "optionally --fit-indices 0,1,... for original fit cameras; "
                "append --fp16-sh for experimental SH storage; "
                "append --timings for completed GPU/CPU timing checks");
        const bool K_CLIPPING = argc == 3 && std::string_view(argv[2]) == "--clipping";
        const bool K_COMPOSITING = argc == 3 && !K_CLIPPING;
        const bool K_TERMINATION = K_COMPOSITING && std::string_view(argv[2]) == "--termination";
        const bool K_BOUNDED = K_COMPOSITING && std::string_view(argv[2]) == "--batches-bounded";
        const bool K_BATCHES =
            K_BOUNDED || (K_COMPOSITING && std::string_view(argv[2]) == "--batches");
        const std::filesystem::path K_OUTPUT(argv[1]);
        require(std::filesystem::create_directory(K_OUTPUT), "output directory must not exist");
        SceneSnapshot scene = K_REAL          ? SceneSnapshot {}
                              : K_CLIPPING    ? clipping_fixture()
                              : K_BATCHES     ? batch_fixture()
                              : K_COMPOSITING ? compositing_fixture(K_TERMINATION)
                                              : fixture();
        if (K_REBASING) {
            auto translated = rebasing_fixture(*offset);
            require(translated.has_value(), translated ? "" : translated.error().m_message);
            scene = *translated;
        }
        std::uint32_t image_width = K_WIDTH, image_height = K_HEIGHT;
        Json fit_frames, identity;
        std::vector<std::size_t> fit_indices;
        std::vector<CameraView> fit_cameras;
        if (K_REAL) {
            BundleLimits limits;
            limits.m_scene.m_max_chunks = 1024;
            limits.m_scene.m_max_splats = 1000000;
            limits.m_scene.m_max_payload_bytes = 512uz * 1024 * 1024;
            auto loaded = load_bundle(argv[3], limits);
            require(loaded.has_value(), loaded ? "" : loaded.error().m_message);
            scene = *loaded;
            const std::filesystem::path K_FIT(argv[4]);
            const auto K_RESULT = read_json(K_FIT / "result.json");
            const auto K_INPUT = read_json(K_FIT / "input_manifest.json");
            const auto& provenance = scene->m_provenance;
            require(
                provenance.at("source_kind") == "video_reconstruction" &&
                    provenance.at("fit_identity") == K_RESULT.at("identity").get<std::string>() &&
                    provenance.at("input_sha256") == K_RESULT.at("ply_sha256").get<std::string>() &&
                    provenance.at("capture_sha256") ==
                        K_RESULT.at("source_sha256").get<std::string>() &&
                    K_INPUT.at("source_sha256") == K_RESULT.at("source_sha256") &&
                    K_RESULT.at("schema_version") == 1 && K_RESULT.at("status") == "complete" &&
                    K_INPUT.at("color_domain") == "srgb" && K_INPUT.at("sh_degree") == 3 &&
                    K_INPUT.at("rasterize_mode") == "classic" &&
                    K_INPUT.at("dataset").at("world_transform") ==
                        "native COLMAP; canonical chunk applies Rx(pi)",
                "scene and completed fit identities/profile differ");
            const std::filesystem::path K_PLY_RELATIVE(K_RESULT.at("ply").get<std::string>());
            require(!K_PLY_RELATIVE.empty() && !K_PLY_RELATIVE.is_absolute(),
                    "invalid fit asset path");
            const auto K_FIT_ROOT = std::filesystem::canonical(K_FIT);
            const auto K_PLY_PATH = std::filesystem::canonical(K_FIT / K_PLY_RELATIVE);
            const auto K_WITHIN_FIT = K_PLY_PATH.lexically_relative(K_FIT_ROOT);
            require(!K_WITHIN_FIT.empty() && *K_WITHIN_FIT.begin() != "..",
                    "fit asset escaped directory");
            require(file_hash(K_PLY_PATH, 512uz * 1024 * 1024) ==
                        K_RESULT.at("ply_sha256").get<std::string>(),
                    "original fitted asset checksum mismatch");
            std::size_t count = 0;
            for (const auto& [K_ID, K_CHUNK] : scene->m_chunks) {
                static_cast<void>(K_ID);
                count += K_CHUNK->m_splats.size();
            }
            require(count > 0 && K_RESULT.at("splats").is_number_unsigned() &&
                        count == K_RESULT.at("splats").get<std::uint64_t>(),
                    "fit splats were trimmed or invalid");
            fit_frames = K_INPUT.at("dataset").at("frames");
            require(fit_frames.size() >= 3 && fit_frames.size() <= 120,
                    "fit frame budget exceeded");
            fit_indices = { 0, fit_frames.size() / 2, fit_frames.size() - 1 };
            if (K_EXPLICIT_INDICES) {
                auto selected = parse_fit_view_indices(argv[6], fit_frames.size());
                require(selected.has_value(), selected ? "" : selected.error().m_message);
                fit_indices = std::move(*selected);
            }
            image_width = image_dimension(fit_frames.at(0).at("width"));
            image_height = image_dimension(fit_frames.at(0).at("height"));
            for (const auto K_INDEX : fit_indices) {
                auto camera = fit_camera(fit_frames.at(K_INDEX));
                if (!camera.m_calibration) {
                    throw std::runtime_error("fit camera calibration is missing");
                }
                const auto& calibration = camera.m_calibration.value();
                require(calibration.m_width == image_width && calibration.m_height == image_height,
                        "fit cameras have different dimensions");
                fit_cameras.push_back(camera);
            }
            identity = {
                { "fit_identity", K_RESULT.at("identity") },
                { "scene_manifest_sha256",
                 file_hash(std::filesystem::path(argv[3]) / "manifest.json", 4uz * 1024 * 1024) },
                { "source_sha256", K_RESULT.at("source_sha256") },
                { "ply_sha256", K_RESULT.at("ply_sha256") },
                { "fit_input_sha256", file_hash(K_FIT / "input_manifest.json", 4uz * 1024 * 1024) },
                { "fit_result_sha256", file_hash(K_FIT / "result.json", 4uz * 1024 * 1024) } };
            // Preserve exact fit metadata for independent camera/identity checks.
            std::filesystem::copy_file(K_FIT / "input_manifest.json", K_OUTPUT / "fit-input.json");
            std::filesystem::copy_file(K_FIT / "result.json", K_OUTPUT / "fit-result.json");
        } else {
            auto saved = write_bundle(K_OUTPUT / "fixture.wexplore", scene);
            require(saved.has_value(), saved ? "" : saved.error().m_message);
        }
        const bool K_INITIALIZED = SDL_Init(SDL_INIT_VIDEO);
        require(K_INITIALIZED, SDL_GetError());

        struct SdlGuard {
            ~SdlGuard() {
                SDL_Quit();
            }
        } sdl;

        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow(K_REAL ? "Real calibrated comparison"
                                    : "Synthetic fixed-camera comparison",
                             static_cast<int>(image_width),
                             static_cast<int>(image_height),
                             SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN),
            SDL_DestroyWindow);
        require(window != nullptr, SDL_GetError());
        RendererOptions options;
        if (K_PACKED) {
            options.m_splat_storage = GpuSplatStorage::FP32_GEOMETRY_FP16_SH;
        }
        options.m_collect_timings = K_TIMINGS;
        options.m_max_splats = K_REAL ? 1000000 : 250000;
        if (K_BOUNDED) {
            options.m_max_tile_references = 2048;
        }
        options.m_shader_directory = WE_SHADER_DIRECTORY;
        auto created = VulkanRenderer::create(window.get(), options);
        require(created.has_value(), created ? "" : created.error().m_message);
        auto renderer = std::move(*created);
        require(renderer->capabilities().m_validation_enabled, "Vulkan validation is required");
        auto batch = make_snapshot_batch(scene, { 1 }).value();
        auto admitted = renderer->updates().enqueue(batch);
        require(admitted.has_value(), admitted ? "" : admitted.error().m_message);
        CameraView wrong_extent;
        wrong_extent.m_calibration = CalibratedProjection { { 300, 300, 100, 100 }, 640, 360 };
        require(!renderer->draw(wrong_extent), "mismatched calibration was admitted");
        const auto K_UNCHANGED = renderer->updates().query_progress(*admitted);
        require(K_UNCHANGED.has_value(), "invalid camera lost update ticket");
        require(renderer->diagnostics().m_submitted_frames == 0 &&
                    K_UNCHANGED->m_state == UpdateState::ACCEPTED,
                "invalid camera consumed the pending scene update");
        Json views = Json::array();
        Json images = Json::array();
        std::vector<std::string> names { "centered", "off_center", "rolled", "translated" };
        if (K_COMPOSITING) {
            names.insert(names.end(), { "orbit_left", "orbit_right", "close_up" });
        }
        if (K_CLIPPING) {
            names = { "clipping_boundaries" };
        }
        if (K_REAL) {
            names = { "fit_first", "fit_middle", "fit_last" };
            if (K_EXPLICIT_INDICES) {
                names.clear();
                for (const auto K_INDEX : fit_indices) {
                    names.push_back("fit_index_" + std::to_string(K_INDEX));
                }
            }
        }
        for (std::size_t view_index { 0uz }; view_index < names.size(); ++view_index) {
            const auto& name = names[view_index];
            CameraView camera;
            camera.m_position = { 0, 0, 6 };
            camera.m_far_plane = 20;
            camera.m_calibration =
                CalibratedProjection { { 900, 840, 639.5, 359.5 }, K_WIDTH, K_HEIGHT };
            if (name != "centered") {
                camera.m_calibration->m_intrinsics = { 930, 790, 481.25, 302.75 };
            }
            if (name == "rolled" || name == "translated") {
                camera.m_right = { std::cos(0.37), -std::sin(0.37), 0 };
                camera.m_down = { -std::sin(0.37), -std::cos(0.37), 0 };
            }
            if (name == "translated") {
                camera.m_position = { -0.3, 0.15, 6.4 };
            }
            if (name == "orbit_left" || name == "orbit_right") {
                const auto K_YAW = name == "orbit_left" ? -0.6 : 0.6;
                camera.m_position = { 6 * std::sin(K_YAW), 0, 6 * std::cos(K_YAW) };
                camera.m_right = { std::cos(K_YAW), 0, -std::sin(K_YAW) };
                camera.m_forward = { -std::sin(K_YAW), 0, -std::cos(K_YAW) };
            }
            if (name == "close_up") {
                camera.m_position = { 0.2, 0.1, 2.8 };
            }
            if (K_CLIPPING) {
                camera = clipping_camera();
            }
            if (K_REBASING) {
                if (name == "translated") {
                    camera.m_position = { -0.25, 0.125, 6.5 };
                }
                const auto K_TRANSLATED =
                    translated_diagnostic_position(camera.m_position, *offset);
                require(K_TRANSLATED.has_value(),
                        K_TRANSLATED ? "" : K_TRANSLATED.error().m_message);
                camera.m_position = *K_TRANSLATED;
                if (name == "translated") {
                    // Cross the renderer's 10k reupload threshold, then return.
                    auto excursion = camera;
                    excursion.m_position.m_x += 16384;
                    require(renderer->draw(excursion).has_value(), "rebase excursion failed");
                }
            }
            std::size_t fit_index = 0;
            if (K_REAL) {
                fit_index = fit_indices[view_index];
                camera = fit_cameras[view_index];
            }
            if (!camera.m_calibration) {
                throw std::runtime_error("diagnostic camera calibration is missing");
            }
            const auto K_K = camera.m_calibration.value().m_intrinsics;
            views.push_back({ { "name", name },
                             { "position", vector(camera.m_position) },
                             { "right", vector(camera.m_right) },
                             { "down", vector(camera.m_down) },
                             { "forward", vector(camera.m_forward) },
                             { "intrinsics", { K_K.m_fx, K_K.m_fy, K_K.m_cx, K_K.m_cy } },
                             { "near", camera.m_near_plane },
                             { "far", camera.m_far_plane } });
            if (K_REAL) {
                views.back()["fit_frame_index"] = fit_index;
                views.back()["fit_frame_name"] = fit_frames.at(fit_index).at("name");
            }
            bool captured = false;
            for (int frame { 0 }; frame < 120 && !captured; ++frame) {
                SDL_PumpEvents();
                int width {}, height {};
                SDL_GetWindowSizeInPixels(window.get(), &width, &height);
                require(width == static_cast<int>(image_width) &&
                            height == static_cast<int>(image_height),
                        "native framebuffer extent changed");
                auto status = renderer->updates().query_progress(*admitted);
                require(status.has_value(), status ? "" : status.error().m_message);
                if (status->m_state == UpdateState::RENDER_VISIBLE) {
                    auto capture = renderer->request_screenshot(K_OUTPUT / (name + ".ppm"));
                    require(capture.has_value(), capture ? "" : capture.error().m_message);
                    captured = true;
                }
                const auto K_DRAWN = renderer->draw(camera);
                require(K_DRAWN.has_value(), K_DRAWN ? "" : K_DRAWN.error().m_message);
            }
            require(captured && std::filesystem::exists(K_OUTPUT / (name + ".ppm")),
                    "scene never became visible/captured");
            images.push_back(
                { { "file", name + ".ppm" },
                 { "sha256", file_hash(K_OUTPUT / (name + ".ppm"), 64uz * 1024 * 1024) } });
        }
        require(renderer->wait_for_idle().has_value(), "GPU completion failed");
        const auto K_STATS = renderer->diagnostics();
        require(K_STATS.m_validation_errors == 0, "Vulkan validation errors");
        if (K_TIMINGS) {
            for (const auto& sample : K_STATS.m_completed_timings) {
                require(sample.m_sequence > 0 && sample.m_sequence <= K_STATS.m_submitted_frames &&
                            sample.m_gpu_valid ==
                                renderer->capabilities().m_gpu_timestamps_supported,
                        "completed timing sample missing or unsupported");
                require(sample.m_count_submissions > 0 && sample.m_tile_batches > 0,
                        "appearance timing did not retain count/batch work");
                if (K_BOUNDED) {
                    require(sample.m_tile_batches > 1,
                            "bounded timing did not exercise query reuse");
                }
            }
        }
        if (K_REAL) {
            std::size_t expected_count = 0;
            for (const auto& [K_ID, K_CHUNK] : scene->m_chunks) {
                static_cast<void>(K_ID);
                expected_count += K_CHUNK->m_splats.size();
            }
            require(K_STATS.m_resident_splat_count == expected_count,
                    "renderer trimmed the real scene");
        }
        Json diagnostic = {
            { "schema", K_REAL ? 2 : 1 },
            { "label",
             K_REAL          ? "real calibrated retained fit; no quality/performance certification"
             : K_REBASING    ? "synthetic floating origin translation v1; not a capture"
             : K_CLIPPING    ? "synthetic inclusive clipping float neighbors v1; not a capture"
             : K_BATCHES     ? "synthetic tile batches v1; not a capture"
             : K_TERMINATION ? "synthetic exclusive termination v1; not a capture"
             : K_COMPOSITING ? "synthetic compositing stress v1; not a capture"
                             : "synthetic asymmetric reference v3; not a capture" },
            { "width", image_width },
            { "height", image_height },
            { "background", { 0.045F, 0.051F, 0.063F } },
            { "chunks", K_REAL ? export_binary_chunks(*scene, K_OUTPUT) : export_chunks(*scene) },
            { "views", views },
            { "scene_content_sha256", scene_content_hash(*scene) } };
        if (K_REAL) {
            diagnostic["gpu_storage"] = K_PACKED ? "fp16_sh" : "fp32";
            diagnostic["splat_stride_bytes"] = renderer->capabilities().m_splat_stride_bytes;
            if (K_EXPLICIT_INDICES) {
                diagnostic["fit_frame_indices"] = fit_indices;
            }
            diagnostic["identity"] = identity;
            diagnostic["splat_count"] = K_STATS.m_resident_splat_count;
            diagnostic["render_origin"] = views.at(0).at("position");
            diagnostic["row_format"] =
                "little-endian float32[60]: mean3,scale3,wxyz4,opacity,sh48,degree";
        }
        if (K_REBASING) {
            diagnostic["rebasing_case"] = argv[3];
            diagnostic["world_offset"] = vector(*offset);
            diagnostic["render_origin"] = views.at(0).at("position");
            diagnostic["rebase_excursion"] = 16384;
        }
        write_json(K_OUTPUT / "fixture.json", diagnostic);
        write_json(
            K_OUTPUT / "native.json",
            { { "device", renderer->capabilities().m_device_name },
             { "fixture_sha256", file_hash(K_OUTPUT / "fixture.json", 4uz * 1024 * 1024) },
             { "images", images },
             { "timing_collection", K_TIMINGS },
             { "gpu_timestamps_supported", renderer->capabilities().m_gpu_timestamps_supported },
             { "timestamp_valid_bits", renderer->capabilities().m_timestamp_valid_bits },
             { "timestamp_period_ns", renderer->capabilities().m_timestamp_period_ns },
             { "fp32_compositing", renderer->capabilities().m_fp32_compositing },
             { "framebuffer_bytes", K_STATS.m_framebuffer_bytes },
             { "allocated_bytes", K_STATS.m_allocated_bytes },
             { "validation_errors", K_STATS.m_validation_errors },
             { "splat_count", K_STATS.m_resident_splat_count },
             { "tile_references", K_STATS.m_tile_reference_count },
             { "submitted_frames", K_STATS.m_submitted_frames } });
        std::cout << (K_REAL ? "Real calibrated retained fit: "
                             : "Synthetic fixed-camera fixture: ")
                  << K_STATS.m_resident_splat_count << " splats, " << names.size() << " views, "
                  << image_width << 'x' << image_height << ", validation_errors=0\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Fixed-camera rendering failed: " << error.what() << '\n';
        return 2;
    }
}
