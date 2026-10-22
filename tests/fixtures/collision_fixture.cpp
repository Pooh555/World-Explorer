#include "collision_support_fixture.hpp"
#include "world_explorer/collision/kinematic_capsule.hpp"
#include "world_explorer/scene_store/bundle.hpp"

#include <cmath>
#include <fstream>
#include <iostream>
#include <numbers>
#include <utility>

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

void trace_step_proposals(const std::filesystem::path& directory,
                          std::string_view source_hash,
                          std::string_view support_hash,
                          bool motion) {
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
    visible->m_id = { 41 };
    visible->m_world_frame.reset();
    visible->m_provenance["fixture"] =
        motion ? "synthetic_step_motion_v1" : "synthetic_step_proposals_v1";
    const double K_DISTANCE { std::sqrt(31.25) };
    visible->m_provenance["source_inspection_view"] =
        nlohmann::json { { "schema_version", 1 },
                        { "position", { 1, 2, -1 } },
                        { "forward", { 2 / K_DISTANCE, -1.5 / K_DISTANCE, 5 / K_DISTANCE } },
                        { "vertical_fov_radians", .9 },
                        { "speed", 2 },
                        { "frame_name",
                         motion ? "Synthetic step motion proof (no walking)"
                                : "Synthetic rejected step proposals (no walking)" } }
            .dump();
    auto markers = std::make_shared<SceneChunk>();
    markers->m_id = { 9007199254740997ULL };
    markers->m_geometry_valid = true;
    markers->m_pose_revision = { 4 };
    markers->m_bounds = { {}, { 8, 2, 8 } };
    markers->m_mesh = SceneMesh {};
    nlohmann::json trace {
        { "schema", motion ? "SYNTHETIC_STEP_MOTION_V1" : "SYNTHETIC_STEP_PROPOSALS_V1" },
        { "source_manifest_sha256", source_hash },
        { "support_sha256", support_hash },
        { "canonical_sha256", (*K_COOK)->digest() },
        { "proposal_only", true },
        { "walking_enabled", false },
        { "measured_collision", false },
        { "radius", .25 },
        { "height", 1.7 },
        { "ticks_per_second", 120U },
        { "minimum_step_forward", 0 },
        { "routes", nlohmann::json::array() } };
    const double K_HEIGHT { std::nextafter(.3F, 0.F) };
    const std::array<Vec3d, 4uz> K_STARTS {
        { { 3.4, 0, 3.5 }, { 3.4, 0, 3.5 }, { 5.4, K_HEIGHT, 3.5 }, { 3.4, 0, 3.5 } } };
    const std::array<Vec3d, 4uz> K_VELOCITIES { { { 3, 0, 0 }, { .3, 0, 0 }, { -3, 0, 0 }, { 10, 0, 0 } } };
    const std::array<std::size_t, 4uz> K_TICKS { 90uz, 600uz, 90uz, 18uz };
    const std::size_t K_ROUTE_COUNT { motion ? 4uz : 3uz };
    for (std::size_t route { 0uz }; route < K_ROUTE_COUNT; ++route) {
        const auto K_PROPOSALS = SyntheticCapsuleController::inspect_step_motion(
            *K_SHAPE, **K_LOADED, { .25, 1.7 }, K_STARTS[route], K_VELOCITIES[route], K_TICKS[route]);
        if (!K_PROPOSALS) {
            throw std::runtime_error(K_PROPOSALS.error().m_message);
        }
        auto samples = nlohmann::json::array();
        Vec3d previous { K_STARTS[route] };
        for (const auto& proposal : *K_PROPOSALS) {
            const auto K_FEET = proposal.m_raw_feet;
            auto sample = nlohmann::json(
                { { "raw_feet", { K_FEET.m_x, K_FEET.m_y, K_FEET.m_z } },
                 { "jolt_supported", proposal.m_jolt_supported },
                 { "horizontal_distance", proposal.m_horizontal_distance },
                 { "straight_clearance", clearance_label(proposal.m_straight_probe.m_state) },
                 { "straight_checked_cells", proposal.m_straight_probe.m_checked_cells },
                 { "transition_clearance", clearance_label(proposal.m_transition_probe.m_state) },
                 { "transition_checked_cells", proposal.m_transition_probe.m_checked_cells } });
            ClearanceProbe motion_probe {};
            if (motion) {
                const auto K_QUERY = [&](Vec3d point) {
                    const double K_FLOOR { (*K_COOK)->floor_height(point) };
                    if (std::abs(point.m_y - K_FLOOR) <= .002) {
                        point.m_y = K_FLOOR;
                    }
                    return point;
                };
                const auto K_MOTION = (*K_COOK)->probe_motion(
                    **K_LOADED, { K_QUERY(previous), K_QUERY(K_FEET), .25, 1.7 });
                if (!K_MOTION) {
                    throw std::runtime_error(K_MOTION.error().m_message);
                }
                motion_probe = *K_MOTION;
                sample["motion_clearance"] = clearance_label(motion_probe.m_state);
                sample["motion_checked_cells"] = motion_probe.m_checked_cells;
            }
            samples.push_back(std::move(sample));
            previous = K_FEET;
            auto& mesh = *markers->m_mesh;
            const auto K_BASE = static_cast<std::uint32_t>(mesh.m_vertices.size());
            const float K_X { static_cast<float>(K_FEET.m_x) };
            const float K_Y { static_cast<float>(K_FEET.m_y) + .02F };
            const float K_Z { static_cast<float>(K_FEET.m_z) };
            const std::array<float, 3uz> K_NORMAL {
                (motion ? motion_probe.m_state : proposal.m_straight_probe.m_state) ==
                        ClearanceState::CLEAR_DIAGNOSTIC
                    ? -1.F
                    : 1.F,
                0,
                0 };
            mesh.m_vertices.push_back({ { K_X - .01F, K_Y, K_Z - .01F }, K_NORMAL });
            mesh.m_vertices.push_back({ { K_X + .01F, K_Y, K_Z - .01F }, K_NORMAL });
            mesh.m_vertices.push_back({ { K_X, K_Y, K_Z + .01F }, K_NORMAL });
            mesh.m_vertices.push_back({ { K_X, K_Y + .04F, K_Z }, K_NORMAL });
            mesh.m_triangles.push_back({ K_BASE, K_BASE + 1U, K_BASE + 2U });
            mesh.m_triangles.push_back({ K_BASE, K_BASE + 3U, K_BASE + 1U });
            mesh.m_triangles.push_back({ K_BASE + 1U, K_BASE + 3U, K_BASE + 2U });
            mesh.m_triangles.push_back({ K_BASE + 2U, K_BASE + 3U, K_BASE });
        }
        const auto K_START = K_STARTS[route], K_VELOCITY = K_VELOCITIES[route];
        trace["routes"].push_back({ { "start_feet", { K_START.m_x, K_START.m_y, K_START.m_z } },
                                   { "velocity", { K_VELOCITY.m_x, K_VELOCITY.m_y, K_VELOCITY.m_z } },
                                   { "samples", samples } });
    }
    visible->m_chunks.emplace(markers->m_id, markers);
    const auto K_WRITTEN =
        write_bundle(directory / (motion ? "motion.wexplore" : "proposals.wexplore"), visible);
    if (!K_WRITTEN) {
        throw std::runtime_error(K_WRITTEN.error().m_message);
    }
    write(directory / (motion ? "step-motion.json" : "step-proposals.json"), trace);
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
        const std::array<std::string_view, 3uz> K_NAMES { "wall", "unknown", "unsupported" };
        const auto K_START = K_STARTS[route];
        write(directory / ("session-" + std::string(K_NAMES[route]) + ".json"),
              { { "kind", "SYNTHETIC_CAPSULE_SESSION_V1" },
               { "source_bundle", "source.wexplore" },
               { "source_manifest_sha256", source_hash },
               { "support", "support.json" },
               { "support_sha256", support_hash },
               { "capsule",
                { { "feet", { K_START.m_x, K_START.m_y, K_START.m_z } },
                 { "radius", .25 },
                 { "height", 1.7 },
                 { "eye_height", 1.6 },
                 { "speed", std::hypot(K_VELOCITY.m_x, K_VELOCITY.m_z) },
                 { "yaw_radians", std::atan2(K_VELOCITY.m_x, -K_VELOCITY.m_z) } } } });
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
        if ((argc != 2 && argc != 3) ||
            (argc == 3 && std::string_view(argv[2]) != "--walls" &&
             std::string_view(argv[2]) != "--capsule" && std::string_view(argv[2]) != "--ramp" &&
             std::string_view(argv[2]) != "--ramp-45" &&
             std::string_view(argv[2]) != "--ramp-steep" && std::string_view(argv[2]) != "--step" &&
             std::string_view(argv[2]) != "--step-clearance" &&
             std::string_view(argv[2]) != "--step-proposals" &&
             std::string_view(argv[2]) != "--step-motion" &&
             std::string_view(argv[2]) != "--step-session")) {
            std::cerr << "Usage: we_collision_fixture NEW_DIRECTORY "
                         "[--walls|--capsule|--ramp|--ramp-45|--ramp-steep|--step|--step-clearance|"
                         "--step-proposals|--step-motion|--step-session]"
                         " (synthetic "
                         "only)\n";
            return 2;
        }
        const bool K_STEP_MOTION { argc == 3 && std::string_view(argv[2]) == "--step-motion" };
        const bool K_STEP_SESSION { argc == 3 && std::string_view(argv[2]) == "--step-session" };
        const bool K_STEP_PROPOSALS {
            K_STEP_MOTION || (argc == 3 && std::string_view(argv[2]) == "--step-proposals") };
        const bool K_STEP_CLEARANCE {
            K_STEP_PROPOSALS || K_STEP_SESSION ||
            (argc == 3 && std::string_view(argv[2]) == "--step-clearance") };
        const bool K_STEP { K_STEP_CLEARANCE ||
                           (argc == 3 && std::string_view(argv[2]) == "--step") };
        const bool K_WITH_WALL = argc == 3 && !K_STEP;
        const bool K_RAMP { argc == 3 && std::string_view(argv[2]).starts_with("--ramp") };
        const float K_RISE { !K_RAMP                                    ? 0.F
                            : std::string_view(argv[2]) == "--ramp"    ? .5F
                            : std::string_view(argv[2]) == "--ramp-45" ? 1.F
                                                                       : 1.F + 1.F / 1024.F };
        const float K_STEP_HEIGHT { K_STEP ? std::nextafter(.3F, 0.F) : 0.F };
        const auto K_OUTPUT = std::filesystem::absolute(argv[1]);
        if (std::filesystem::exists(std::filesystem::symlink_status(K_OUTPUT)) ||
            !std::filesystem::create_directory(K_OUTPUT)) {
            throw std::runtime_error("Choose a new synthetic fixture directory");
        }
        const auto K_SOURCE = std::make_shared<SceneState>(
            collision_source(K_WITH_WALL, false, "NEG_X", K_RISE, 0, K_STEP_HEIGHT));
        const auto K_WRITTEN = write_bundle(K_OUTPUT / "source.wexplore", K_SOURCE);
        if (!K_WRITTEN) {
            throw std::runtime_error(K_WRITTEN.error().m_message);
        }
        auto document = K_STEP ? collision_step_document(*K_SOURCE, K_STEP_HEIGHT, K_STEP_CLEARANCE)
                        : K_RAMP      ? collision_ramp_document(*K_SOURCE, K_RISE, 0, true)
                        : K_WITH_WALL ? collision_wall_document(*K_SOURCE)
                                      : collision_document(*K_SOURCE);
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
        const auto K_PROBE = [&](std::array<double, 3uz> start, std::array<double, 3uz> end) {
            if (K_RAMP) {
                start[1uz] = collision_ramp_feet_y(start[0uz], start[2uz], K_RISE, 0, .2);
                end[1uz] = collision_ramp_feet_y(end[0uz], end[2uz], K_RISE, 0, .2);
            }
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
        if (K_STEP_SESSION) {
            const std::array<std::string_view, 5uz> K_NAMES {
                "up", "down", "slow", "short", "corner" };
            for (std::size_t route { 0uz }; route < K_NAMES.size(); ++route) {
                const bool K_DOWN { route == 1uz };
                const bool K_CORNER { route == 4uz };
                auto selected_support = std::string("support.json");
                auto support_hash = *K_SUPPORT_HASH;
                if (K_CORNER) {
                    auto blocked = document;
                    blocked["grid"]["states"]
                        .get_ref<std::string&>()[4uz + 8uz * (1uz + 4uz * 4uz)] = '#';
                    write(K_OUTPUT / "corner-support.json", blocked);
                    selected_support = "corner-support.json";
                    const auto K_HASH = sha256_file(K_OUTPUT / selected_support, 1024uz * 1024uz);
                    if (!K_HASH) {
                        throw std::runtime_error("Could not hash corner support");
                    }
                    support_hash = *K_HASH;
                }
                write(K_OUTPUT / ("session-" + std::string(K_NAMES[route]) + ".json"),
                      { { "kind", "SYNTHETIC_CAPSULE_SESSION_V1" },
                       { "source_bundle", "source.wexplore" },
                       { "source_manifest_sha256", *K_SOURCE_HASH },
                       { "support", selected_support },
                       { "support_sha256", support_hash },
                       { "capsule",
                        { { "feet",
                          { K_DOWN         ? 4.6
                           : route == 3uz ? 3.8
                                          : 3.4,
                           K_DOWN ? K_STEP_HEIGHT : 0,
                           K_CORNER ? 3.85 : 3.5 } },
                         { "radius", .25 },
                         { "height", 1.7 },
                         { "eye_height", 1.6 },
                         { "speed", route == 2uz ? .3 : 3 },
                         { "yaw_radians", K_DOWN ? -std::numbers::pi / 2 : std::numbers::pi / 2 } } } });
            }
        }
        if (K_RAMP) {
            const std::array<std::string_view, 3uz> K_NAMES { "wall", "unknown", "unsupported" };
            const std::array<Vec3d, 3uz> K_STARTS { { { 2.5, 0, 1.3 }, { 1.5, 0, 3.5 }, { 2.5, 0, 3.5 } } };
            const std::array<Vec3d, 3uz> K_VELOCITIES { { { 3, 0, .4 }, { 0, 0, 3 }, { 3, 0, 0 } } };
            for (std::size_t route { 0uz }; route < K_NAMES.size(); ++route) {
                const auto K_START = K_STARTS[route];
                const auto K_VELOCITY = K_VELOCITIES[route];
                write(K_OUTPUT / ("session-" + std::string(K_NAMES[route]) + ".json"),
                      { { "kind", "SYNTHETIC_CAPSULE_SESSION_V1" },
                       { "source_bundle", "source.wexplore" },
                       { "source_manifest_sha256", *K_SOURCE_HASH },
                       { "support", "support.json" },
                       { "support_sha256", *K_SUPPORT_HASH },
                       { "capsule",
                        { { "feet",
                          { K_START.m_x,
                           collision_ramp_feet_y(K_START.m_x, K_START.m_z, K_RISE, 0),
                           K_START.m_z } },
                         { "radius", .25 },
                         { "height", 1.7 },
                         { "eye_height", 1.6 },
                         { "speed", std::hypot(K_VELOCITY.m_x, K_VELOCITY.m_z) },
                         { "yaw_radians", std::atan2(K_VELOCITY.m_x, -K_VELOCITY.m_z) } } } });
            }
        }
        if (argc == 3 && std::string_view(argv[2]) == "--capsule") {
            trace_capsules(K_OUTPUT, *K_SOURCE_HASH, *K_SUPPORT_HASH);
        }
        if (K_STEP_PROPOSALS) {
            trace_step_proposals(K_OUTPUT, *K_SOURCE_HASH, *K_SUPPORT_HASH, K_STEP_MOTION);
        }
        std::cout << "Synthetic collision source/support/request saved; no measured collision or "
                     "walking\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
