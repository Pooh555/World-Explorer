#include "world_explorer/render_vulkan/renderer.hpp"
#include "world_explorer/scene_store/bundle.hpp"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <type_traits>

using namespace world_explorer;

namespace {

void require(bool ok, const std::string& message) {
    if (!ok) {
        throw std::runtime_error(message);
    }
}

template <class T> T take(Result<T> result) {
    if (!result) {
        throw std::runtime_error(result.error().m_message);
    }
    if constexpr (std::is_void_v<T>) {
        return;
    } else {
        return std::move(*result);
    }
}

template <class T> const T& selected(const std::optional<T>& value) {
    if (!value) {
        throw std::runtime_error("PBR fixture payload missing");
    }
    return *value;
}

void save(const std::filesystem::path& path, const nlohmann::json& doc) {
    std::ofstream out(path);
    out << doc.dump(2) << '\n';
    require(static_cast<bool>(out), "PBR report write failed");
}

}  // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 3, "Usage: we_gpu_pbr NEW_DIRECTORY SYNTHETIC_PBR_BUNDLE");
        const std::filesystem::path K_OUTPUT { argv[1] };
        require(std::filesystem::create_directory(K_OUTPUT), "PBR output must be new");
        const auto K_SCENE = take(load_bundle(argv[2]));
        require(K_SCENE->m_provenance.at("source_kind") == "synthetic_diagnostic",
                "GPU PBR probe requires a labeled synthetic fixture");
        require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());

        struct SdlOwner {
            ~SdlOwner() {
                SDL_Quit();
            }
        } sdl;

        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("World Explorer - synthetic PBR validation",
                             640,
                             480,
                             SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN),
            SDL_DestroyWindow);
        require(window != nullptr, SDL_GetError());
        RendererOptions options;
        options.m_shader_directory = WE_SHADER_DIRECTORY;
        options.m_residency_bytes = 256uz * 1024uz * 1024uz;
        auto renderer = take(VulkanRenderer::create(window.get(), options));
        auto sources = nlohmann::json::array();
        const auto K_TRANSFORM = [](const SimilarityTransform& value) {
            return nlohmann::json {
                { "rotation",
                 { value.m_rotation.m_w,
                  value.m_rotation.m_x,
                  value.m_rotation.m_y,
                  value.m_rotation.m_z } },
                { "translation",
                 { value.m_translation.m_x, value.m_translation.m_y, value.m_translation.m_z } },
                { "scale", value.m_scale } };
        };
        for (const auto& [K_ID, K_CHUNK] : K_SCENE->m_chunks) {
            require(K_CHUNK->m_mesh.has_value(), "PBR geometry absent");
            const auto& mesh = selected(K_CHUNK->m_mesh);
            require(mesh.m_pbr.has_value(), "PBR material absent");
            const auto& pbr = selected(mesh.m_pbr);
            auto vertices = nlohmann::json::array(), materials = nlohmann::json::array(),
                 instances = nlohmann::json::array();
            for (std::size_t i { 0uz }; i < mesh.m_vertices.size(); ++i) {
                vertices.push_back({ { "position", mesh.m_vertices[i].m_position },
                                    { "normal", mesh.m_vertices[i].m_normal },
                                    { "tangent", pbr.m_tangents[i] },
                                    { "uv", pbr.m_uv[i] } });
            }
            for (const auto& material : pbr.m_materials) {
                auto images = nlohmann::json::array();
                for (const auto& image : material.m_images) {
                    auto pixels = nlohmann::json::array();
                    for (auto byte : image) {
                        pixels.push_back(std::to_integer<unsigned>(byte));
                    }
                    images.push_back(std::move(pixels));
                }
                materials.push_back({ { "width", material.m_width },
                                     { "height", material.m_height },
                                     { "linear", material.m_linear_filter },
                                     { "repeat", material.m_repeat },
                                     { "images", images } });
            }
            for (const auto& instance : pbr.m_instances) {
                instances.push_back(K_TRANSFORM(instance.m_chunk_from_asset));
            }
            sources.push_back({ { "chunk", K_ID.m_value },
                               { "world_from_chunk", K_TRANSFORM(K_CHUNK->m_world_from_chunk) },
                               { "vertices", vertices },
                               { "triangles", mesh.m_triangles },
                               { "triangle_materials", pbr.m_triangle_materials },
                               { "materials", materials },
                               { "instances", instances } });
        }
        save(K_OUTPUT / "scene-input.json", sources);
        auto batch = take(make_snapshot_batch(K_SCENE, { 1 }));
        const auto K_TICKET = take(renderer->updates().enqueue(batch));
        CameraView camera;
        camera.m_position = { 12, 0, 15 };
        camera.m_calibration = CalibratedProjection { { 320, 320, 319.5, 239.5 }, 640, 480 };
        PbrLighting light;
        const auto K_DRAW = [&] {
            SDL_PumpEvents();
            (void)take(renderer->draw(camera, true, SceneRepresentation::NATIVE_PBR, light));
        };
        for (std::size_t frame { 0uz }; frame < 30uz; ++frame) {
            K_DRAW();
        }
        require(take(renderer->updates().query_progress(K_TICKET)).m_state ==
                    UpdateState::RENDER_VISIBLE,
                "PBR initial update did not become visible");
        auto cases = nlohmann::json::array();
        for (std::size_t index { 0uz }; index < 6uz; ++index) {
            camera.m_position = index < 3uz ? Vec3d { 12, 0, 15 } : Vec3d { 30, 4, 15 };
            light.m_to_light = index % 3uz == 0uz ? Vec3d { 0, 0, 1 } : Vec3d { 1, .5, .6 };
            light.m_intensity = index % 3uz == 2uz ? 0 : 2;
            light.m_ambient = { .03, .03, .03 };
            light.m_exposure = 1;
            const auto K_NAME = "case-" + std::to_string(index) + ".ppm";
            (void)take(renderer->request_screenshot(K_OUTPUT / K_NAME));
            K_DRAW();
            (void)take(renderer->wait_for_idle());
            require(std::filesystem::exists(K_OUTPUT / K_NAME), "PBR screenshot absent");
            cases.push_back(
                { { "file", K_NAME },
                 { "camera", { camera.m_position.m_x, camera.m_position.m_y, camera.m_position.m_z } },
                 { "to_light", { light.m_to_light.m_x, light.m_to_light.m_y, light.m_to_light.m_z } },
                 { "intensity", light.m_intensity },
                 { "ambient", { .03, .03, .03 } },
                 { "exposure", light.m_exposure } });
        }
        // Replacement and deletion use full ordinary snapshots and the same GPU
        // residency owner, including late retirement and paused presentation.
        auto replacement = std::make_shared<SceneState>(*K_SCENE);
        replacement->m_revision = { K_SCENE->m_revision.m_value + 1 };
        auto chunk = std::make_shared<SceneChunk>(*replacement->m_chunks.begin()->second);
        ++chunk->m_generation;
        replacement->m_chunks[chunk->m_id] = chunk;
        auto changed = take(make_snapshot_batch(replacement, { 2 }));
        const auto K_REPLACE = take(renderer->updates().enqueue(changed));
        for (std::size_t frame { 0uz }; frame < 20uz; ++frame) {
            K_DRAW();
        }
        require(take(renderer->updates().query_progress(K_REPLACE)).m_state ==
                    UpdateState::RENDER_VISIBLE,
                "PBR replacement did not become visible");
        auto invalid = std::make_shared<SceneState>(*replacement);
        invalid->m_revision = { replacement->m_revision.m_value + 1 };
        auto stale = std::make_shared<SceneChunk>(*chunk);
        ++stale->m_generation;
        stale->m_geometry_valid = false;
        stale->m_pose_revision = { 6 };
        invalid->m_chunks[stale->m_id] = stale;
        auto correction = take(make_snapshot_batch(invalid, { 3 }));
        const auto K_INVALID = take(renderer->updates().enqueue(correction));
        for (std::size_t frame { 0uz }; frame < 20uz; ++frame) {
            K_DRAW();
        }
        require(take(renderer->updates().query_progress(K_INVALID)).m_state ==
                    UpdateState::RENDER_VISIBLE,
                "PBR invalidation did not become visible");
        (void)take(renderer->request_screenshot(K_OUTPUT / "invalidated.ppm"));
        K_DRAW();
        (void)take(renderer->wait_for_idle());
        auto empty = std::make_shared<SceneState>(*invalid);
        empty->m_revision = { invalid->m_revision.m_value + 1 };
        empty->m_chunks.clear();
        auto erased = take(make_snapshot_batch(empty, { 4 }));
        const auto K_ERASE = take(renderer->updates().enqueue(erased));
        for (std::size_t frame { 0uz }; frame < 20uz; ++frame) {
            K_DRAW();
        }
        require(take(renderer->updates().query_progress(K_ERASE)).m_state ==
                    UpdateState::RENDER_VISIBLE,
                "PBR delete did not become visible");
        for (std::size_t frame { 0uz }; frame < 4uz; ++frame) {
            (void)take(renderer->draw(camera, false, SceneRepresentation::NATIVE_PBR, light));
        }
        (void)take(renderer->wait_for_idle());
        const auto K_DIAGNOSTICS = renderer->diagnostics();
        require(K_DIAGNOSTICS.m_validation_errors == 0,
                "Vulkan validation errors during PBR lifecycle");
        require(K_DIAGNOSTICS.m_resident_mesh_vertices == 0 &&
                    K_DIAGNOSTICS.m_resident_mesh_triangles == 0,
                "PBR deleted geometry remained resident");
        save(K_OUTPUT / "report.json",
             { { "kind", "synthetic_pbr_gpu_validation" },
              { "device", renderer->capabilities().m_device_name },
              { "validation_enabled", renderer->capabilities().m_validation_enabled },
              { "validation_errors", K_DIAGNOSTICS.m_validation_errors },
              { "frames", K_DIAGNOSTICS.m_submitted_frames },
              { "cases", cases },
              { "initial_replace_invalidate_delete", true } });
        std::cout << "Native synthetic PBR GPU lifecycle passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
