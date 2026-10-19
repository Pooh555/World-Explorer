#include "world_explorer/core/hash.hpp"
#include "world_explorer/render_vulkan/renderer.hpp"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>

using namespace world_explorer;

namespace {

void require(bool valid, const std::string& message) {
    if (!valid) {
        throw std::runtime_error(message);
    }
}

std::shared_ptr<const SceneChunk> chunk(ChunkId id, std::size_t count) {
    auto value = std::make_shared<SceneChunk>();
    value->m_id = id;
    value->m_bounds = { { -1, -1, -1 }, { 20001, 1001, 1 } };
    Gaussian splat;
    splat.m_mean = { 20000, 1000, 0 };
    splat.m_scale = { 0.2F, 0.15F, 0.1F };
    splat.m_opacity = 0;
    value->m_splats.assign(count, splat);
    value->m_splats[0].m_mean = { 0, 0, static_cast<float>(id.m_value) / 20 };
    value->m_splats[0].m_opacity = 0.7F;
    value->m_splats[0].m_sh[static_cast<std::size_t>(id.m_value % 3)] = 1.2F;
    if (count > 1) {
        value->m_splats[1] = value->m_splats[0];
        value->m_splats[1].m_mean[0] = 20000;
        value->m_splats[1].m_sh[2] = 1.5F;
    }
    return value;
}

struct Phase {
    SceneSnapshot m_snapshot;
    Vec3d m_camera;
    std::size_t m_reused_splats {};
};

std::vector<Phase> phases() {
    auto original = std::make_shared<SceneState>();
    original->m_id = { 72 };
    original->m_provenance = { { "source_kind", "synthetic_diagnostic" },
                              { "fixture", "resident_copy_offsets_v1" } };
    original->m_chunks = { { { 2 }, chunk({ 2 }, 1) }, { { 4 }, chunk({ 4 }, 66000) }, { { 6 }, chunk({ 6 }, 1) } };
    std::vector<Phase> result;
    auto append = [&](std::shared_ptr<SceneState> scene, Vec3d camera, std::size_t reused) {
        scene->m_revision = { result.size() + 1 };
        result.push_back({ std::move(scene), camera, reused });
    };
    auto next = [&] {
        return std::make_shared<SceneState>(*result.back().m_snapshot);
    };
    const Vec3d K_NEAR { 0, 0, 4 };
    append(std::make_shared<SceneState>(*original), K_NEAR, 0);
    append(next(), K_NEAR, 66002);
    auto scene = next();
    auto replacement = std::make_shared<SceneChunk>(*scene->m_chunks.at({ 4 }));
    replacement->m_splats[0].m_sh[1] = -0.8F;
    scene->m_chunks[{ 4 }] = replacement;
    append(scene, K_NEAR, 2);
    scene = next();
    scene->m_chunks.erase({ 2 });
    append(scene, K_NEAR, 66001);
    scene = next();
    scene->m_chunks.emplace(ChunkId { 1 }, chunk({ 1 }, 3));
    append(scene, K_NEAR, 66001);
    scene = next();
    replacement = std::make_shared<SceneChunk>(*chunk({ 6 }, 2));
    replacement->m_world_from_chunk.m_translation.m_x = 0.25;
    replacement->m_world_from_chunk.m_scale = 0.8;
    scene->m_chunks[{ 6 }] = replacement;
    append(scene, K_NEAR, 66003);
    scene = next();
    scene->m_chunks.clear();
    append(scene, K_NEAR, 0);
    append(std::make_shared<SceneState>(*original), K_NEAR, 0);
    append(next(), { 1.25, 0.5, 4 }, 66002);
    append(next(), { 20000, 0, 4 }, 0);  // A new floating origin requires fresh CPU data.
    append(next(), { 20000.25, 0, 4 }, 66002);
    scene = next();
    replacement = std::make_shared<SceneChunk>(*scene->m_chunks.at({ 4 }));
    replacement->m_world_from_chunk.m_translation.m_x = 0.1;
    scene->m_chunks[{ 4 }] = replacement;
    append(scene, { 20000.25, 0, 4 }, 2);
    return result;
}

}  // namespace
int main(int argc, char** argv) try {
    require(argc == 2, "usage: we_gpu_resident_reuse <new-output-directory>");
    const std::filesystem::path K_OUTPUT(argv[1]);
    require(std::filesystem::create_directory(K_OUTPUT), "output must be a new directory");
    require(SDL_Init(SDL_INIT_VIDEO), "SDL initialization failed");

    struct SdlOwner {
        ~SdlOwner() {
            SDL_Quit();
        }
    } sdl;

    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("World Explorer synthetic resident reuse",
                         320,
                         240,
                         SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN),
        SDL_DestroyWindow);
    require(window != nullptr, "cannot create SDL verification window");
    const auto K_CASES = phases();
    std::vector<std::string> references;
    nlohmann::json report { { "scope", "synthetic resident-copy versus CPU-pack parity" },
                           { "runs", nlohmann::json::array() } };
    for (const auto K_PROFILE : { GpuSplatStorage::FP32, GpuSplatStorage::FP32_GEOMETRY_FP16_SH }) {
        references.clear();
        const bool K_PACKED = K_PROFILE == GpuSplatStorage::FP32_GEOMETRY_FP16_SH;
        const std::size_t K_STRIDE = K_PACKED ? 160 : 256;
        for (const bool K_REUSE : { false, true }) {
            RendererOptions options;
            options.m_max_splats = 100000;
            options.m_splat_storage = K_PROFILE;
            options.m_shader_directory = WE_SHADER_DIRECTORY;
            options.m_collect_timings = true;
            options.m_reuse_resident_chunks = K_REUSE;
            auto created = VulkanRenderer::create(window.get(), options);
            require(created.has_value(), created ? "" : created.error().m_message);
            auto renderer = std::move(*created);
            require(renderer->capabilities().m_validation_enabled, "Vulkan validation is required");
            report["device"] = renderer->capabilities().m_device_name;
            for (std::size_t index { 0uz }; index < K_CASES.size(); ++index) {
                const auto& test = K_CASES[index];
                CameraView camera;
                camera.m_position = test.m_camera;
                auto batch = make_snapshot_batch(test.m_snapshot, { index + 1 }).value();
                const auto K_TICKET = renderer->updates().enqueue(batch);
                require(K_TICKET.has_value(), "synthetic scene admission failed");
                const auto K_BEFORE = renderer->diagnostics().m_submitted_frames;
                std::map<std::uint64_t, RendererFrameTiming> samples;
                auto collect = [&] {
                    const auto K_STATS = renderer->diagnostics();
                    require(K_STATS.m_validation_errors == 0 &&
                                K_STATS.m_allocated_bytes <= options.m_residency_bytes,
                            "Vulkan validation or residency error");
                    for (const auto& sample : K_STATS.m_completed_timings) {
                        if (sample.m_sequence > K_BEFORE) {
                            samples.try_emplace(sample.m_sequence, sample);
                        }
                    }
                };
                bool visible = false;
                for (int frame { 0 }; frame < 30 && !visible; ++frame) {
                    SDL_PumpEvents();
                    const auto K_DRAWN = renderer->draw(camera);
                    require(K_DRAWN.has_value(), K_DRAWN ? "" : K_DRAWN.error().m_message);
                    collect();
                    const auto K_PROGRESS = renderer->updates().query_progress(*K_TICKET);
                    require(K_PROGRESS && K_PROGRESS->m_state != UpdateState::FAILED,
                            "GPU upload failed");
                    visible = K_PROGRESS->m_state == UpdateState::RENDER_VISIBLE;
                    const auto K_SNAPSHOT = renderer->updates().visible_snapshot();
                    require((K_SNAPSHOT ? K_SNAPSHOT->m_revision.m_value : 0) ==
                                (visible ? index + 1 : index),
                            "partial upload advanced the visible revision");
                    if (!visible) {
                        require(renderer->diagnostics().m_staging_bytes > 0,
                                "active staging allocation was absent from diagnostics");
                    }
                }
                require(visible, "synthetic revision did not become visible");
                const auto K_PATH =
                    K_OUTPUT / ((K_PACKED ? std::string("packed-") : std::string()) +
                                (K_REUSE ? "resident-" : "cpu-") + std::to_string(index) + ".ppm");
                require(renderer->request_screenshot(K_PATH).has_value(),
                        "readback request failed");
                require(renderer->draw(camera).has_value(), "readback draw failed");
                collect();
                require(renderer->wait_for_idle().has_value(), "GPU drain failed");
                collect();
                const auto K_HASH = sha256_file(K_PATH, 4uz * 1024 * 1024);
                require(K_HASH.has_value(), "cannot hash synthetic readback");
                if (K_REUSE) {
                    require(*K_HASH == references[index],
                            "resident copy differs from CPU-packed pixels");
                } else {
                    references.push_back(*K_HASH);
                }
                std::size_t host {}, resident {}, splats {};
                for (const auto& [K_ID, K_CHUNK] : test.m_snapshot->m_chunks) {
                    static_cast<void>(K_ID);
                    splats += K_CHUNK->m_splats.size();
                }
                for (const auto& [K_SEQUENCE, K_SAMPLE] : samples) {
                    static_cast<void>(K_SEQUENCE);
                    host += K_SAMPLE.m_host_copy_bytes;
                    resident += K_SAMPLE.m_resident_copy_bytes;
                    require(K_SAMPLE.m_host_copy_bytes + K_SAMPLE.m_resident_copy_bytes <=
                                15ULL * 1024 * 1024,
                            "per-frame copy byte cap exceeded");
                }
                const auto K_EXPECTED_RESIDENT = K_REUSE ? test.m_reused_splats * K_STRIDE : 0;
                require(resident == K_EXPECTED_RESIDENT &&
                            host ==
                                std::max<std::size_t>(16, splats * K_STRIDE) - K_EXPECTED_RESIDENT,
                        "resident/host transfer bytes differ from the fixture contract");
                report["runs"].push_back({ { "reuse", K_REUSE },
                                          { "stride", K_STRIDE },
                                          { "revision", index + 1 },
                                          { "host_bytes", host },
                                          { "resident_bytes", resident },
                                          { "sha256", *K_HASH } });
            }
            if (K_PACKED && K_REUSE) {
                auto invalid = std::make_shared<SceneState>(*K_CASES.back().m_snapshot);
                invalid->m_revision = { 13 };
                auto replacement = std::make_shared<SceneChunk>(*invalid->m_chunks.at({ 4 }));
                replacement->m_splats.front().m_sh[47] = 65505.F;
                invalid->m_chunks[{ 4 }] = replacement;
                const auto K_ORIGINAL_HASH = scene_content_hash(*invalid);
                auto batch = make_snapshot_batch(invalid, { 13 }).value();
                const auto K_TICKET = renderer->updates().enqueue(batch);
                require(K_TICKET.has_value(), "finite canonical scene should pass CPU admission");
                CameraView camera;
                camera.m_position = K_CASES.back().m_camera;
                require(renderer->draw(camera).has_value(), "packing rejection draw failed");
                require(renderer->wait_for_idle().has_value(), "packing rejection drain failed");
                const auto K_PROGRESS = renderer->updates().query_progress(*K_TICKET);
                require(K_PROGRESS && K_PROGRESS->m_state == UpdateState::FAILED &&
                            K_PROGRESS->m_failure &&
                            K_PROGRESS->m_failure->m_code == ErrorCode::INVALID_ARGUMENT,
                        "binary16 overflow did not fail explicitly");
                require(renderer->updates().visible_snapshot()->m_revision.m_value == 12 &&
                            scene_content_hash(*invalid) == K_ORIGINAL_HASH,
                        "packing rejection changed visible scene or caller data");
                const auto K_PATH = K_OUTPUT / "rejected-packed.ppm";
                require(renderer->request_screenshot(K_PATH).has_value(),
                        "rejection capture failed");
                require(renderer->draw(camera).has_value() && renderer->wait_for_idle().has_value(),
                        "rejection redraw failed");
                require(sha256_file(K_PATH, 4ULL * 1024 * 1024).value() == references.back(),
                        "packing rejection changed prior pixels");
                auto repaired = std::make_shared<SceneState>(*K_CASES.back().m_snapshot);
                repaired->m_revision = { 14 };
                auto repair_batch = make_snapshot_batch(repaired, { 14 }).value();
                const auto K_REPAIR_TICKET = renderer->updates().enqueue(repair_batch);
                require(K_REPAIR_TICKET.has_value(), "repair full snapshot was rejected");
                bool visible = false;
                for (int frame { 0 }; frame < 30 && !visible; ++frame) {
                    require(renderer->draw(camera).has_value(), "repair draw failed");
                    visible = renderer->updates().query_progress(*K_REPAIR_TICKET)->m_state ==
                              UpdateState::RENDER_VISIBLE;
                }
                require(visible && renderer->diagnostics().m_validation_errors == 0,
                        "valid full snapshot could not recover after packing failure");
                report["packed_overflow_preservation_and_repair"] = true;
            }
        }
        require(references[0] != references[9],
                "near and rebased views did not exercise distinct pixels");
    }
    std::ofstream stream(K_OUTPUT / "summary.json", std::ios::binary);
    stream << report.dump(2) << '\n';
    require(static_cast<bool>(stream), "cannot write resident-copy report");
    std::cout << "48 synthetic resident/CPU/profile phases passed; validation_errors=0\n";
    return 0;
} catch (const std::exception& exception) {
    std::cerr << "Resident reuse verification failed: " << exception.what() << '\n';
    return 2;
}
