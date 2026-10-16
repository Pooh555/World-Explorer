#include "world_explorer/core/hash.hpp"
#include "world_explorer/render_vulkan/renderer.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <vector>

using namespace world_explorer;
namespace {
void require(bool valid, const std::string& message) {
    if (!valid)
        throw std::runtime_error(message);
}
std::shared_ptr<const scene_chunk> chunk(chunk_id id, std::size_t count) {
    auto value = std::make_shared<scene_chunk>();
    value->id = id;
    value->bounds = {{-1, -1, -1}, {20001, 1001, 1}};
    gaussian splat;
    splat.mean = {20000, 1000, 0};
    splat.scale = {0.2F, 0.15F, 0.1F};
    splat.opacity = 0;
    value->splats.assign(count, splat);
    value->splats[0].mean = {0, 0, static_cast<float>(id.value) / 20};
    value->splats[0].opacity = 0.7F;
    value->splats[0].sh[static_cast<std::size_t>(id.value % 3)] = 1.2F;
    if (count > 1) {
        value->splats[1] = value->splats[0];
        value->splats[1].mean[0] = 20000;
        value->splats[1].sh[2] = 1.5F;
    }
    return value;
}
struct phase {
    scene_snapshot snapshot;
    vec3d camera;
    std::size_t reused_splats{};
};
std::vector<phase> phases() {
    auto original = std::make_shared<scene_state>();
    original->id = {72};
    original->provenance = {{"source_kind", "synthetic_diagnostic"},
                            {"fixture", "resident_copy_offsets_v1"}};
    original->chunks = {{{2}, chunk({2}, 1)}, {{4}, chunk({4}, 66000)}, {{6}, chunk({6}, 1)}};
    std::vector<phase> result;
    auto append = [&](std::shared_ptr<scene_state> scene, vec3d camera, std::size_t reused) {
        scene->revision = {result.size() + 1};
        result.push_back({std::move(scene), camera, reused});
    };
    auto next = [&] { return std::make_shared<scene_state>(*result.back().snapshot); };
    const vec3d near{0, 0, 4};
    append(std::make_shared<scene_state>(*original), near, 0);
    append(next(), near, 66002);
    auto scene = next();
    auto replacement = std::make_shared<scene_chunk>(*scene->chunks.at({4}));
    replacement->splats[0].sh[1] = -0.8F;
    scene->chunks[{4}] = replacement;
    append(scene, near, 2);
    scene = next();
    scene->chunks.erase({2});
    append(scene, near, 66001);
    scene = next();
    scene->chunks.emplace(chunk_id{1}, chunk({1}, 3));
    append(scene, near, 66001);
    scene = next();
    replacement = std::make_shared<scene_chunk>(*chunk({6}, 2));
    replacement->world_from_chunk.translation.x = 0.25;
    replacement->world_from_chunk.scale = 0.8;
    scene->chunks[{6}] = replacement;
    append(scene, near, 66003);
    scene = next();
    scene->chunks.clear();
    append(scene, near, 0);
    append(std::make_shared<scene_state>(*original), near, 0);
    append(next(), {1.25, 0.5, 4}, 66002);
    append(next(), {20000, 0, 4}, 0); // A new floating origin requires fresh CPU data.
    append(next(), {20000.25, 0, 4}, 66002);
    scene = next();
    replacement = std::make_shared<scene_chunk>(*scene->chunks.at({4}));
    replacement->world_from_chunk.translation.x = 0.1;
    scene->chunks[{4}] = replacement;
    append(scene, {20000.25, 0, 4}, 2);
    return result;
}
} // namespace
int main(int argc, char** argv) try {
    require(argc == 2, "usage: we_gpu_resident_reuse <new-output-directory>");
    const std::filesystem::path output(argv[1]);
    require(std::filesystem::create_directory(output), "output must be a new directory");
    require(SDL_Init(SDL_INIT_VIDEO), "SDL initialization failed");
    struct sdl_owner {
        ~sdl_owner() { SDL_Quit(); }
    } sdl;
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("World Explorer synthetic resident reuse", 320, 240,
                         SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN),
        SDL_DestroyWindow);
    require(window != nullptr, "cannot create SDL verification window");
    const auto cases = phases();
    std::vector<std::string> references;
    nlohmann::json report{{"scope", "synthetic resident-copy versus CPU-pack parity"},
                          {"runs", nlohmann::json::array()}};
    for (const bool reuse : {false, true}) {
        renderer_options options;
        options.max_splats = 100000;
        options.shader_directory = WE_SHADER_DIRECTORY;
        options.collect_timings = true;
        options.reuse_resident_chunks = reuse;
        auto created = vulkan_renderer::create(window.get(), options);
        require(created.has_value(), created ? "" : created.error().message);
        auto renderer = std::move(*created);
        require(renderer->capabilities().validation_enabled, "Vulkan validation is required");
        report["device"] = renderer->capabilities().device_name;
        for (std::size_t index = 0; index < cases.size(); ++index) {
            const auto& test = cases[index];
            camera_view camera;
            camera.position = test.camera;
            auto batch = make_snapshot_batch(test.snapshot, {index + 1}).value();
            const auto ticket = renderer->updates().enqueue(batch);
            require(ticket.has_value(), "synthetic scene admission failed");
            const auto before = renderer->diagnostics().submitted_frames;
            std::map<std::uint64_t, renderer_frame_timing> samples;
            auto collect = [&] {
                const auto stats = renderer->diagnostics();
                require(stats.validation_errors == 0 &&
                            stats.allocated_bytes <= options.residency_bytes,
                        "Vulkan validation or residency error");
                for (const auto& sample : stats.completed_timings)
                    if (sample.sequence > before)
                        samples.try_emplace(sample.sequence, sample);
            };
            bool visible = false;
            for (int frame = 0; frame < 30 && !visible; ++frame) {
                SDL_PumpEvents();
                const auto drawn = renderer->draw(camera);
                require(drawn.has_value(), drawn ? "" : drawn.error().message);
                collect();
                const auto progress = renderer->updates().query_progress(*ticket);
                require(progress && progress->state != update_state::FAILED, "GPU upload failed");
                visible = progress->state == update_state::RENDER_VISIBLE;
                const auto snapshot = renderer->updates().visible_snapshot();
                require((snapshot ? snapshot->revision.value : 0) == (visible ? index + 1 : index),
                        "partial upload advanced the visible revision");
                if (!visible)
                    require(renderer->diagnostics().staging_bytes > 0,
                            "active staging allocation was absent from diagnostics");
            }
            require(visible, "synthetic revision did not become visible");
            const auto path =
                output / ((reuse ? "resident-" : "cpu-") + std::to_string(index) + ".ppm");
            require(renderer->request_screenshot(path).has_value(), "readback request failed");
            require(renderer->draw(camera).has_value(), "readback draw failed");
            collect();
            require(renderer->wait_for_idle().has_value(), "GPU drain failed");
            collect();
            const auto hash = sha256_file(path, std::size_t{4} * 1024 * 1024);
            require(hash.has_value(), "cannot hash synthetic readback");
            if (reuse)
                require(*hash == references[index], "resident copy differs from CPU-packed pixels");
            else
                references.push_back(*hash);
            std::size_t host{}, resident{}, splats{};
            for (const auto& [id, chunk] : test.snapshot->chunks) {
                static_cast<void>(id);
                splats += chunk->splats.size();
            }
            for (const auto& [sequence, sample] : samples) {
                static_cast<void>(sequence);
                host += sample.host_copy_bytes;
                resident += sample.resident_copy_bytes;
                require(sample.host_copy_bytes + sample.resident_copy_bytes <= 15ULL * 1024 * 1024,
                        "per-frame copy byte cap exceeded");
            }
            const auto expected_resident = reuse ? test.reused_splats * 256 : 0;
            require(resident == expected_resident &&
                        host == std::max<std::size_t>(16, splats * 256) - expected_resident,
                    "resident/host transfer bytes differ from the fixture contract");
            report["runs"].push_back({{"reuse", reuse},
                                      {"revision", index + 1},
                                      {"host_bytes", host},
                                      {"resident_bytes", resident},
                                      {"sha256", *hash}});
        }
    }
    require(references[0] != references[9],
            "near and rebased views did not exercise distinct pixels");
    std::ofstream stream(output / "summary.json", std::ios::binary);
    stream << report.dump(2) << '\n';
    require(static_cast<bool>(stream), "cannot write resident-copy report");
    std::cout << "24 synthetic resident/CPU phases passed; validation_errors=0\n";
    return 0;
} catch (const std::exception& exception) {
    std::cerr << "Resident reuse verification failed: " << exception.what() << '\n';
    return 2;
}
