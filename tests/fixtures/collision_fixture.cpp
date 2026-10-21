#include "collision_support_fixture.hpp"
#include "world_explorer/collision/kinematic_capsule.hpp"
#include "world_explorer/scene_store/bundle.hpp"

#include <cmath>
#include <fstream>
#include <iostream>

using namespace world_explorer;
using namespace world_explorer::testing;

namespace {

void write(const std::filesystem::path& path, const nlohmann::json& value) {
    std::ofstream output(path, std::ios::binary);
    output << value.dump(2) << '\n';
    if (!output) {
        throw std::runtime_error("Could not write synthetic collision fixture");
    }
}

void trace_capsules(const std::filesystem::path& directory,
                    std::string_view source_hash,
                    std::string_view support_hash) {
    const auto K_LOADED = load_bundle(directory / "source.wexplore", {}, source_hash);
    if (!K_LOADED) {
        throw std::runtime_error(K_LOADED.error().m_message);
    }
    std::ifstream input(directory / "support.json", std::ios::binary);
    const std::string K_TEXT { std::istreambuf_iterator<char>(input), {} };
    const auto K_SUPPORT = admit_collision_support(**K_LOADED, K_TEXT, support_hash);
    if (!K_SUPPORT) {
        throw std::runtime_error(K_SUPPORT.error().m_message);
    }
    const auto K_COOK = cook_synthetic_collision(*K_LOADED, *K_SUPPORT);
    if (!K_COOK) {
        throw std::runtime_error(K_COOK.error().m_message);
    }
    const auto K_SHAPE = build_jolt_static_collision(*K_COOK, **K_LOADED);
    if (!K_SHAPE) {
        throw std::runtime_error(K_SHAPE.error().m_message);
    }
    auto visible = std::make_shared<SceneState>(**K_LOADED);
    visible->m_id = { 40 };
    visible->m_world_frame.reset();
    visible->m_provenance["fixture"] = "synthetic_capsule_paths_v1";
    const double K_VIEW_DISTANCE { std::sqrt(116.0) };
    visible->m_provenance["source_inspection_view"] =
        nlohmann::json {
            { "schema_version", 1 },
            { "position", { 0, 6, -4 } },
            { "forward", { 4 / K_VIEW_DISTANCE, -6 / K_VIEW_DISTANCE, 8 / K_VIEW_DISTANCE } },
            { "vertical_fov_radians", .9 },
            { "speed", 2 },
            { "frame_name", "Synthetic capsule paths (no measured walking)" } }
            .dump();
    auto path = std::make_shared<SceneChunk>();
    path->m_id = { 9007199254740995ULL };
    path->m_geometry_valid = true;
    path->m_pose_revision = { 4 };
    path->m_bounds = { {}, { 8, 2, 8 } };
    path->m_mesh = SceneMesh {};
    nlohmann::json trace { { "schema", "SYNTHETIC_CAPSULE_TRACE_V1" },
                          { "source_manifest_sha256", source_hash },
                          { "support_sha256", support_hash },
                          { "canonical_sha256", (*K_COOK)->digest() },
                          { "walking_enabled", false },
                          { "measured_collision", false },
                          { "radius", .25 },
                          { "height", 1.7 },
                          { "frame_nanoseconds", 10000000 },
                          { "routes", nlohmann::json::array() } };
    const std::array<Vec3d, 3uz> K_STARTS { { { 2.5, 0, 1.3 }, { 1.5, 0, 3.5 }, { 2.5, 0, 3.5 } } };
    const std::array<Vec3d, 3uz> K_VELOCITIES { { { 3, 0, .4 }, { 0, 0, 3 }, { 5, 0, 0 } } };
    const std::array<std::array<float, 3uz>, 3uz> K_NORMALS { { { 1, 0, 0 }, { 0, 0, 1 }, { 0, -1, 0 } } };
    for (std::size_t route { 0uz }; route < K_STARTS.size(); ++route) {
        auto controller =
            SyntheticCapsuleController::create(*K_SHAPE, **K_LOADED, { .25, 1.7 }, K_STARTS[route]);
        if (!controller) {
            throw std::runtime_error(controller.error().m_message);
        }
        nlohmann::json samples = nlohmann::json::array();
        for (std::size_t frame { 0uz }; frame < 100uz; ++frame) {
            const auto K_ADVANCE =
                (*controller)
                    ->advance(**K_LOADED, K_VELOCITIES[route], std::chrono::milliseconds(10));
            if (!K_ADVANCE) {
                throw std::runtime_error(K_ADVANCE.error().m_message);
            }
            const auto K_FEET = K_ADVANCE->m_state.m_feet;
            samples.push_back({ { "feet", { K_FEET.m_x, K_FEET.m_y, K_FEET.m_z } },
                               { "tick", K_ADVANCE->m_state.m_simulated_ticks },
                               { "clearance", static_cast<unsigned>(K_ADVANCE->m_clearance) } });
            auto& mesh = *path->m_mesh;
            const auto K_BASE = static_cast<std::uint32_t>(mesh.m_vertices.size());
            const float K_X { static_cast<float>(K_FEET.m_x) };
            const float K_Z { static_cast<float>(K_FEET.m_z) };
            mesh.m_vertices.push_back({ { K_X - .025F, .02F, K_Z - .025F }, K_NORMALS[route] });
            mesh.m_vertices.push_back({ { K_X + .025F, .02F, K_Z - .025F }, K_NORMALS[route] });
            mesh.m_vertices.push_back({ { K_X, .02F, K_Z + .025F }, K_NORMALS[route] });
            mesh.m_vertices.push_back({ { K_X, .12F, K_Z }, K_NORMALS[route] });
            mesh.m_triangles.push_back({ K_BASE, K_BASE + 1U, K_BASE + 2U });
            mesh.m_triangles.push_back({ K_BASE, K_BASE + 3U, K_BASE + 1U });
            mesh.m_triangles.push_back({ K_BASE + 1U, K_BASE + 3U, K_BASE + 2U });
            mesh.m_triangles.push_back({ K_BASE + 2U, K_BASE + 3U, K_BASE });
        }
        const auto K_VELOCITY = K_VELOCITIES[route];
        trace["routes"].push_back(
            { { "velocity", { K_VELOCITY.m_x, K_VELOCITY.m_y, K_VELOCITY.m_z } }, { "samples", samples } });
    }
    visible->m_chunks.emplace(path->m_id, path);
    const auto K_WRITTEN = write_bundle(directory / "paths.wexplore", visible);
    if (!K_WRITTEN) {
        throw std::runtime_error(K_WRITTEN.error().m_message);
    }
    write(directory / "capsule-trace.json", trace);
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if ((argc != 2 && argc != 3) || (argc == 3 && std::string_view(argv[2]) != "--walls" &&
                                         std::string_view(argv[2]) != "--capsule")) {
            std::cerr << "Usage: we_collision_fixture NEW_DIRECTORY [--walls|--capsule] (synthetic "
                         "only)\n";
            return 2;
        }
        const bool K_WITH_WALL = argc == 3;
        const auto K_OUTPUT = std::filesystem::absolute(argv[1]);
        if (std::filesystem::exists(std::filesystem::symlink_status(K_OUTPUT)) ||
            !std::filesystem::create_directory(K_OUTPUT)) {
            throw std::runtime_error("Choose a new synthetic fixture directory");
        }
        const auto K_SOURCE = std::make_shared<SceneState>(collision_source(K_WITH_WALL));
        const auto K_WRITTEN = write_bundle(K_OUTPUT / "source.wexplore", K_SOURCE);
        if (!K_WRITTEN) {
            throw std::runtime_error(K_WRITTEN.error().m_message);
        }
        auto document =
            K_WITH_WALL ? collision_wall_document(*K_SOURCE) : collision_document(*K_SOURCE);
        auto& cells = document["grid"]["states"].get_ref<std::string&>();
        // V1 keeps its occupied stripe; V2 selects one explicit mesh face.
        // Both retain an unknown column; no state is inferred from TSDF allocation.
        for (std::size_t y { 0uz }; y < 4uz; ++y) {
            if (!K_WITH_WALL) {
                for (std::size_t z { 0uz }; z < 3uz; ++z) {
                    cells[3uz + 8uz * (y + 4uz * z)] = '#';
                }
            }
            cells[1uz + 8uz * (y + 4uz * 4uz)] = '?';
        }
        write(K_OUTPUT / "support.json", document);
        const auto K_SUPPORT_HASH = sha256_file(K_OUTPUT / "support.json", 1024uz * 1024uz);
        const auto K_SOURCE_HASH =
            sha256_file(K_OUTPUT / "source.wexplore/manifest.json", 1024uz * 1024uz);
        if (!K_SUPPORT_HASH || !K_SOURCE_HASH) {
            throw std::runtime_error("Could not hash synthetic inputs");
        }
        const auto K_PROBE = [](std::array<double, 3uz> start, std::array<double, 3uz> end) {
            return nlohmann::json {
                { "start_feet", start }, { "end_feet", end }, { "radius", .2 }, { "height", 1.7 } };
        };
        write(K_OUTPUT / "request.json",
              { { "kind", "COLLISION_SUPPORT_INSPECTION_V1" },
               { "source_bundle", "source.wexplore" },
               { "source_manifest_sha256", *K_SOURCE_HASH },
               { "support", "support.json" },
               { "support_sha256", *K_SUPPORT_HASH },
               { "probes",
                nlohmann::json::array({ K_PROBE({ 1.5, 0, 1.5 }, { 1.5, 0, 1.5 }),
                                       K_PROBE({ 1.5, 0, 1.5 }, { 4.5, 0, 1.5 }),
                                       K_PROBE({ 1.5, 0, 4.5 }, { 1.5, 0, 4.5 }),
                                       K_PROBE({ 3.5, 0, 4.5 }, { 3.5, 0, 4.5 }) }) } });
        if (argc == 3 && std::string_view(argv[2]) == "--capsule") {
            trace_capsules(K_OUTPUT, *K_SOURCE_HASH, *K_SUPPORT_HASH);
        }
        std::cout << "Synthetic collision source/support/request saved; no measured collision or "
                     "walking\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
