#include "world_explorer/application/fusion_scene.hpp"
#include "world_explorer/application/scenes.hpp"
#include "world_explorer/render_vulkan/renderer.hpp"
#include "world_explorer/scene_store/bundle.hpp"
#include "world_explorer/scene_store/gltf.hpp"
#include "world_explorer/scene_store/mesh_texture.hpp"
#include "world_explorer/scene_store/scene_store.hpp"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace world_explorer;

namespace {

template <class Chunk> decltype(auto) mesh_of(Chunk& chunk) {
    if (!chunk.m_mesh) {
        throw std::runtime_error("Expected mesh fixture payload");
    }
    return *chunk.m_mesh;
}

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::shared_ptr<SceneChunk> fixture() {
    auto chunk = std::make_shared<SceneChunk>();
    chunk->m_id = { 1 };
    chunk->m_pose_revision = { 1 };
    chunk->m_geometry_valid = true;
    chunk->m_bounds = { { -1, -1, 1 }, { 1, 1, 2 } };
    chunk->m_mesh = SceneMesh { { { { -.8F, -.7F, 2 }, { 1, 0, 0 } },
                                { { .8F, -.7F, 2 }, { 1, 0, 0 } },
                                { { 0, .8F, 2 }, { 1, 0, 0 } },
                                { { -.8F, -.7F, 1 }, { 0, 0, 1 } },
                                { { .8F, -.7F, 1 }, { 0, 0, 1 } },
                                { { 0, .8F, 1 }, { 0, 0, 1 } } },
                               { { 3, 4, 5 }, { 0, 1, 2 } },
                               {} };
    return chunk;
}

std::shared_ptr<SceneChunk> texture_fixture(std::uint64_t generation,
                                            bool unknown = false,
                                            bool gradient = false,
                                            std::size_t triangles = 1uz,
                                            bool subpixel = false) {
    auto chunk = fixture();
    chunk->m_generation = generation;
    chunk->m_source_frames = { { 7 } };
    auto& mesh = mesh_of(*chunk);
    mesh.m_vertices.resize(3uz);
    if (subpixel) {
        mesh.m_vertices[0].m_position = { -.000875F, -.007125F, 2 };
        mesh.m_vertices[1].m_position = { .007125F, -.007125F, 2 };
        mesh.m_vertices[2].m_position = { .003125F, .000875F, 2 };
    }
    for (auto& vertex : mesh.m_vertices) {
        vertex.m_normal = { 0, 0, 1 };
    }
    mesh.m_triangles.assign(triangles, { 0, 1, 2 });
    DepthImageAsset depth;
    auto& metadata = depth.m_metadata;
    metadata.m_dependency = { { 1 }, { 1 }, { 7 }, { 2 }, std::string(64uz, 'a'), std::string(64uz, 'b') };
    metadata.m_width = metadata.m_height = 16;
    metadata.m_camera = { 8, 8, 7.5, 7.5 };
    metadata.m_calibrated_image_sha256 = std::string(64uz, 'c');
    metadata.m_recipe_sha256 = std::string(64uz, 'd');
    depth.m_axial_depth.assign(256uz, unknown ? 0.F : 2.F);
    depth.m_valid.assign(256uz, unknown ? 0 : 1);
    depth.m_support_count.assign(256uz, unknown ? 0 : 2);
    depth.m_confidence.assign(256uz, unknown ? 0.F : 1.F);
    std::vector<std::byte> rgb;
    for (std::size_t i { 0uz }; i < 256uz; ++i) {
        rgb.push_back(static_cast<std::byte>(gradient ? i % 16uz * 16uz : 255uz));
        rgb.push_back(static_cast<std::byte>(gradient ? i / 16uz * 16uz : 64uz));
        rgb.push_back(static_cast<std::byte>(gradient ? 64uz : 0uz));
    }
    auto view = ColorProjectionView::create(std::move(depth), std::move(rgb), {}, .01);
    require(view.has_value(), "Texture view fixture failed");
    auto baker = MeshTextureBaker::create(*chunk);
    require(baker.has_value(), "Texture fixture baker failed");
    require((*baker)->add_view(*view, { 7 }).has_value(), "Texture fixture projection failed");
    auto baked = (*baker)->finish();
    require(baked.has_value(), "Texture fixture finalization failed");
    mesh.m_baked = std::move(*baked);
    return chunk;
}

std::vector<unsigned char> pixels(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::string magic;
    std::size_t width {}, height {}, maximum {};
    input >> magic >> width >> height >> maximum;
    require(magic == "P6" && maximum == 255 && width <= 4096uz && height <= 4096uz,
            "Invalid mesh readback");
    input.get();
    std::vector<unsigned char> result(width * height * 3uz);
    input.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(result.size()));
    require(input.good(), "Truncated mesh readback");
    return result;
}

void expect_center(const std::vector<unsigned char>& data, std::array<int, 3> expected) {
    const auto K_OFFSET = (240uz * 640uz + 320uz) * 3uz;
    for (std::size_t i { 0uz }; i < 3uz; ++i) {
        require(std::abs(static_cast<int>(data.at(K_OFFSET + i)) - expected[i]) <= 1,
                "Mesh depth/normal center oracle differs");
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2 || argc == 3 || argc == 4,
                "Usage: we_gpu_mesh NEW_DIRECTORY [BUNDLE | FUSION_REQUEST SHA256]");
        const std::filesystem::path K_OUTPUT { argv[1] };
        require(std::filesystem::create_directory(K_OUTPUT), "Mesh evidence directory must be new");
        require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());

        struct SdlOwner {
            ~SdlOwner() {
                SDL_Quit();
            }
        } sdl;

        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("World Explorer - synthetic mesh validation",
                             640,
                             480,
                             SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN),
            SDL_DestroyWindow);
        require(window != nullptr, SDL_GetError());
        RendererOptions options;
        options.m_shader_directory = WE_SHADER_DIRECTORY;
        options.m_residency_bytes = 256uz * 1024uz * 1024uz;
        auto created = VulkanRenderer::create(window.get(), options);
        require(created.has_value(), created ? "" : created.error().m_message);
        auto renderer = std::move(*created);
        CameraView camera;
        camera.m_position = { 0, 0, 4 };
        camera.m_calibration = CalibratedProjection { { 320, 320, 319.5, 239.5 }, 640, 480 };
        auto representation = SceneRepresentation::OBSERVED_MESH;
        auto draw = [&] {
            SDL_PumpEvents();
            const auto K_RESULT = renderer->draw(camera, true, representation);
            require(K_RESULT.has_value(), K_RESULT ? "" : K_RESULT.error().m_message);
        };
        std::uint64_t operation { 1 };
        const auto K_ENQUEUE = [&](SceneSnapshot scene) {
            auto batch = make_snapshot_batch(std::move(scene), { operation++ });
            require(batch.has_value(), batch ? "" : batch.error().m_message);
            auto ticket = renderer->updates().enqueue(*batch);
            require(ticket.has_value(), ticket ? "" : ticket.error().m_message);
            return *ticket;
        };
        const auto K_VISIBLE = [&](UpdateTicket ticket) {
            for (std::size_t i { 0uz }; i < 30uz; ++i) {
                draw();
                const auto K_STATE = renderer->updates().query_progress(ticket);
                require(K_STATE.has_value(), "Missing mesh update receipt");
                if (K_STATE->m_state == UpdateState::FAILED) {
                    throw std::runtime_error(K_STATE->m_failure->m_message);
                }
                if (K_STATE->m_state == UpdateState::RENDER_VISIBLE) {
                    return;
                }
            }
            throw std::runtime_error("Mesh upload failed to become visible");
        };
        const auto K_CAPTURE = [&](const char* name) {
            require(renderer->request_screenshot(K_OUTPUT / name).has_value(),
                    "Mesh screenshot rejected");
            draw();
            require(renderer->wait_for_idle().has_value(), "Mesh completion wait failed");
            return pixels(K_OUTPUT / name);
        };
        SceneStore store({ 19 });
        auto initial = std::make_shared<SceneState>();
        initial->m_id = { 19 };
        initial->m_epoch = { 1 };
        initial->m_provenance = { { "source_kind", "synthetic_diagnostic" } };
        require(store.reset(initial).has_value(), "Synthetic mesh reset failed");
        auto chunk = fixture();
        require(
            store
                .commit(
                    { { 19 }, { 1 }, {}, { 1 }, { 1 }, { { SceneOperationKind::CREATE, { 1 }, chunk, {}, {} } } })
                .has_value(),
            "Mesh create failed");
        K_VISIBLE(K_ENQUEUE(store.acquire_snapshot()));
        const auto K_BASELINE = K_CAPTURE("baseline.ppm");
        expect_center(K_BASELINE, { 255, 128, 128 });
        std::uint64_t revision { 1 };
        const auto K_REPLACE = [&](std::shared_ptr<SceneChunk> next) {
            next->m_generation = revision + 1;
            const SceneTransaction K_TRANSACTION {
                { 19 },
                { 1 },
                { revision },
                { revision + 1 },
                { revision + 1 },
                { { SceneOperationKind::REPLACE, { 1 }, next, {}, {} } } };
            require(store.commit(K_TRANSACTION).has_value(), "Mesh replacement failed");
            ++revision;
            K_VISIBLE(K_ENQUEUE(store.acquire_snapshot()));
        };
        auto reordered = fixture();
        std::reverse(mesh_of(*reordered).m_triangles.begin(),
                     mesh_of(*reordered).m_triangles.end());
        K_REPLACE(reordered);
        require(K_CAPTURE("reordered.ppm") == K_BASELINE, "Mesh order changed depth-tested pixels");
        camera.m_calibration = CalibratedProjection { { 318.25, 271.75, 287.2, 213.7 }, 640, 480 };
        K_CAPTURE("asymmetric.ppm");
        camera.m_calibration = CalibratedProjection { { 320, 320, 319.5, 239.5 }, 640, 480 };
        camera.m_near_plane = 2.5F;
        expect_center(K_CAPTURE("near-clip.ppm"), { 128, 128, 255 });
        camera.m_near_plane = .01F;
        camera.m_far_plane = 2.5F;
        expect_center(K_CAPTURE("far-clip.ppm"), { 255, 128, 128 });
        camera.m_far_plane = 100000.F;
        for (std::size_t i { 0uz }; i < 12uz; ++i) {
            K_REPLACE(fixture());
        }
        require(K_CAPTURE("replacement.ppm") == K_BASELINE,
                "Mesh replacement/retirement changed pixels");
        camera.m_calibration.reset();
        require(SDL_SetWindowSize(window.get(), 800, 600), "Mesh resize request failed");
        for (std::size_t i { 0uz }; i < 60uz; ++i) {
            draw();
            if (renderer->diagnostics().m_framebuffer_width == 800 &&
                renderer->diagnostics().m_framebuffer_height == 600) {
                break;
            }
            SDL_Delay(5);
        }
        require(renderer->diagnostics().m_framebuffer_width == 800,
                "Mesh resize did not become visible");
        K_CAPTURE("resized.ppm");
        require(SDL_SetWindowSize(window.get(), 640, 480), "Mesh restore size failed");
        for (std::size_t i { 0uz }; i < 60uz; ++i) {
            draw();
            if (renderer->diagnostics().m_framebuffer_width == 640 &&
                renderer->diagnostics().m_framebuffer_height == 480) {
                break;
            }
            SDL_Delay(5);
        }
        camera.m_calibration = CalibratedProjection { { 320, 320, 319.5, 239.5 }, 640, 480 };
        require(K_CAPTURE("restored-size.ppm") == K_BASELINE,
                "Mesh resize/restore changed fixed-camera pixels");
        auto large = fixture();
        large->m_generation = revision + 1;
        mesh_of(*large).m_vertices.resize(800000uz, mesh_of(*large).m_vertices.front());
        require(store
                    .commit({ { 19 },
                             { 1 },
                             { revision },
                             { revision + 1 },
                             { revision + 1 },
                             { { SceneOperationKind::REPLACE, { 1 }, large, {}, {} } } })
                    .has_value(),
                "Large mesh replace failed");
        ++revision;
        auto delayed = K_ENQUEUE(store.acquire_snapshot());
        const auto K_OLD_VISIBLE = renderer->updates().visible_snapshot();
        draw();
        require(renderer->updates().visible_snapshot() == K_OLD_VISIBLE,
                "Partial mesh upload exposed a replacement early");
        K_VISIBLE(delayed);
        require(K_CAPTURE("staged.ppm") == K_BASELINE, "Staged mesh upload changed pixels");
        auto rebased = fixture();
        rebased->m_world_from_chunk.m_translation = { 1e9, -1e9, 1e9 };
        camera.m_position = camera.m_position + rebased->m_world_from_chunk.m_translation;
        K_REPLACE(rebased);
        draw();
        require(K_CAPTURE("rebased.ppm") == K_BASELINE, "Mesh floating-origin parity failed");
        require(store
                    .commit({ { 19 },
                             { 1 },
                             { revision },
                             { revision + 1 },
                             { revision + 1 },
                             { { SceneOperationKind::INVALIDATE_GEOMETRY, { 1 }, {}, {}, { 2 } } } })
                    .has_value(),
                "Mesh invalidation failed");
        ++revision;
        K_VISIBLE(K_ENQUEUE(store.acquire_snapshot()));
        const auto K_INVALIDATED = K_CAPTURE("invalidated.ppm");
        require(K_INVALIDATED != K_BASELINE, "Stale mesh remained visible");
        require(store
                    .commit({ { 19 },
                             { 1 },
                             { revision },
                             { revision + 1 },
                             { revision + 1 },
                             { { SceneOperationKind::ERASE, { 1 }, {}, {}, {} } } })
                    .has_value(),
                "Mesh erase failed");
        K_VISIBLE(K_ENQUEUE(store.acquire_snapshot()));
        require(K_CAPTURE("erased.ppm") == K_INVALIDATED,
                "Erase did not preserve unknown background");

        ++revision;
        camera.m_position = { 0, 0, 4 };
        representation = SceneRepresentation::BAKED_APPEARANCE;
        auto first_texture = texture_fixture(revision + 1);
        require(store
                    .commit({ { 19 },
                             { 1 },
                             { revision },
                             { revision + 1 },
                             { revision + 1 },
                             { { SceneOperationKind::CREATE, { 1 }, first_texture, {}, {} } } })
                    .has_value(),
                "Texture create failed");
        ++revision;
        K_VISIBLE(K_ENQUEUE(store.acquire_snapshot()));
        const auto K_TEXTURE = K_CAPTURE("texture-gold.ppm");
        expect_center(K_TEXTURE, { 255, 64, 0 });
        const auto K_PORTABLE = [&](bool unknown) {
            auto source = std::make_shared<SceneState>();
            source->m_id = { 19 };
            source->m_epoch = { 1 };
            source->m_revision = { 1 };
            source->m_provenance = { { "source_kind", "synthetic_diagnostic" } };
            source->m_chunks[{ 1 }] = texture_fixture(revision + 1, unknown);
            const auto K_BYTES = encode_baked_glb(source);
            require(K_BYTES.has_value(), K_BYTES ? "" : K_BYTES.error().m_message);
            const auto K_IMPORTED = decode_baked_glb(*K_BYTES);
            require(K_IMPORTED.has_value(), K_IMPORTED ? "" : K_IMPORTED.error().m_message);
            return std::make_shared<SceneChunk>(*(*K_IMPORTED)->m_chunks.at({ 1 }));
        };
        K_REPLACE(K_PORTABLE(false));
        const auto K_PORTABLE_PIXELS = K_CAPTURE("portable-gold.ppm");
        expect_center(K_PORTABLE_PIXELS, { 255, 64, 0 });
        const auto K_GOLD = [](const std::vector<unsigned char>& data, std::size_t i) {
            return data[i] == 255 && data[i + 1uz] == 64 && data[i + 2uz] == 0;
        };
        std::size_t portable_count {}, native_count {};
        for (std::size_t i { 0uz }; i < K_TEXTURE.size(); i += 3uz) {
            const bool K_VISIBLE_PORTABLE = K_GOLD(K_PORTABLE_PIXELS, i);
            if (K_VISIBLE_PORTABLE) {
                ++portable_count;
                require(K_GOLD(K_TEXTURE, i), "Portable filter filled an unknown native fragment");
            }
            if (K_GOLD(K_TEXTURE, i)) {
                ++native_count;
            }
        }
        require(portable_count > 0uz && portable_count < native_count,
                "Conservative bilinear coverage did not retain its declared loss");
        K_REPLACE(K_PORTABLE(true));
        require(K_CAPTURE("portable-unknown.ppm") == K_INVALIDATED,
                "Portable unknown fragments were filled");
        for (std::size_t i { 0uz }; i < 12uz; ++i) {
            K_REPLACE(texture_fixture(revision + 1));
        }
        require(K_CAPTURE("texture-replaced.ppm") == K_TEXTURE, "Texture replacement changed RGB");
        K_REPLACE(texture_fixture(revision + 1, false, false, 1uz, true));
        expect_center(K_CAPTURE("texture-subpixel.ppm"), { 255, 64, 0 });
        K_REPLACE(texture_fixture(revision + 1, false, true));
        const auto K_GRADIENT = K_CAPTURE("texture-gradient.ppm");
        const auto K_PIXEL = [&](std::size_t x, std::size_t y, std::size_t channel) {
            return K_GRADIENT.at((y * 640uz + x) * 3uz + channel);
        };
        require(K_PIXEL(280uz, 240uz, 0uz) < K_PIXEL(360uz, 240uz, 0uz) &&
                    K_PIXEL(320uz, 200uz, 1uz) > K_PIXEL(320uz, 280uz, 1uz) &&
                    std::abs(static_cast<int>(K_PIXEL(320uz, 240uz, 2uz)) - 64) <= 1,
                "Texture UV orientation or linear-to-sRGB conversion differs");
        K_REPLACE(texture_fixture(revision + 1, true));
        require(K_CAPTURE("texture-unknown.ppm") == K_INVALIDATED,
                "Unknown texture failed to discard");
        auto large_texture = texture_fixture(revision + 1, false, false, 40000uz);
        require(store
                    .commit({ { 19 },
                             { 1 },
                             { revision },
                             { revision + 1 },
                             { revision + 1 },
                             { { SceneOperationKind::REPLACE, { 1 }, large_texture, {}, {} } } })
                    .has_value(),
                "Large texture replacement failed");
        ++revision;
        const auto K_TEXTURE_TICKET = K_ENQUEUE(store.acquire_snapshot());
        const auto K_OLD_TEXTURE = renderer->updates().visible_snapshot();
        draw();
        require(renderer->updates().visible_snapshot() == K_OLD_TEXTURE,
                "Partial texture upload became visible early");
        K_VISIBLE(K_TEXTURE_TICKET);
        require(K_CAPTURE("texture-staged.ppm") == K_TEXTURE, "Staged texture differs");
        require(store
                    .commit({ { 19 },
                             { 1 },
                             { revision },
                             { revision + 1 },
                             { revision + 1 },
                             { { SceneOperationKind::INVALIDATE_GEOMETRY, { 1 }, {}, {}, { 2 } } } })
                    .has_value(),
                "Texture invalidation failed");
        ++revision;
        K_VISIBLE(K_ENQUEUE(store.acquire_snapshot()));
        require(K_CAPTURE("texture-invalidated.ppm") == K_INVALIDATED,
                "Stale texture remained visible");
        representation = SceneRepresentation::OBSERVED_MESH;
        std::size_t real_vertices {}, real_triangles {};
        if (argc >= 3) {
            const auto K_REAL =
                argc == 3 ? load_bundle(argv[2]) : load_fusion_scene_request(argv[2], argv[3]);
            require(K_REAL.has_value(), K_REAL ? "" : K_REAL.error().m_message);
            FlyCamera fly;
            fly.fit(world_bounds(**K_REAL));
            camera = { fly.m_position, fly.right(), fly.down(), fly.forward() };
            if (std::ranges::any_of((*K_REAL)->m_chunks, [](const auto& entry) {
                    return entry.second->m_mesh && entry.second->m_mesh->m_baked;
                })) {
                representation = SceneRepresentation::BAKED_APPEARANCE;
            }
            K_VISIBLE(K_ENQUEUE(*K_REAL));
            K_CAPTURE("real.ppm");
            const auto& mesh = mesh_of(*(*K_REAL)->m_chunks.begin()->second);
            real_vertices = mesh.m_vertices.size();
            real_triangles = mesh.m_triangles.size();
        }
        require(renderer->wait_for_idle().has_value(), "Final mesh idle failed");
        const auto K_STATS = renderer->diagnostics();
        require(K_STATS.m_validation_errors == 0 &&
                    K_STATS.m_allocated_bytes <= options.m_residency_bytes,
                "Mesh validation/residency check failed");
        std::cout << nlohmann::json { { "device", renderer->capabilities().m_device_name },
                                     { "scope",
                                      "synthetic texture RGB/UV/unknown/staging/invalidation and "
                                      "mesh depth/update/clip/rebase; "
                                      "optional partial real geometry" },
                                     { "frames", K_STATS.m_submitted_frames },
                                     { "validation_errors", K_STATS.m_validation_errors },
                                     { "allocated_bytes", K_STATS.m_allocated_bytes },
                                     { "real_vertices", real_vertices },
                                     { "real_triangles", real_triangles } }
                         .dump(2)
                  << '\n';
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 2;
    }
}
