#include "world_explorer/scene_store/collision_support.hpp"

#include "world_explorer/core/hash.hpp"
#include "world_explorer/scene_store/world_frame.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <limits>
#include <set>

namespace world_explorer {
namespace {

using Json = nlohmann::json;
using GridPoint = std::array<std::int64_t, 2uz>;
using GridTriangle = std::array<GridPoint, 3uz>;

void fields(const Json& value, std::initializer_list<const char*> names) {
    if (!value.is_object() || value.size() != names.size()) {
        throw std::runtime_error("Unexpected collision-support fields");
    }
    for (const auto* name : names) {
        if (!value.contains(name)) {
            throw std::runtime_error("Missing collision-support field");
        }
    }
}

std::uint64_t integer(const Json& value) {
    if (!value.is_number_unsigned()) {
        throw std::runtime_error("Collision-support IDs and indices must be unsigned integers");
    }
    return value.get<std::uint64_t>();
}

double real(const Json& value) {
    if (!value.is_number() || !std::isfinite(value.get<double>())) {
        throw std::runtime_error("Collision-support scalar must be finite numeric data");
    }
    return value.get<double>();
}

bool inside(const GridTriangle& triangle, GridPoint p) {
    // Exact integer orientation on a bounded lattice; no epsilon can close a gap.
    bool positive {}, negative {};
    for (std::size_t edge { 0uz }; edge < 3uz; ++edge) {
        const auto& a = triangle[edge];
        const auto& b = triangle[(edge + 1uz) % 3uz];
        const auto K_CROSS =
            (b[0uz] - a[0uz]) * (p[1uz] - a[1uz]) - (b[1uz] - a[1uz]) * (p[0uz] - a[0uz]);
        positive = positive || K_CROSS > 0;
        negative = negative || K_CROSS < 0;
    }
    return !(positive && negative);
}

}  // namespace

Result<std::shared_ptr<const CollisionSupport>>
admit_collision_support(const SceneState& source,
                        std::string_view document,
                        std::string_view digest,
                        const std::stop_token& stop) {
    if (stop.stop_requested()) {
        return fail(ErrorCode::CANCELLED, "Collision-support admission cancelled");
    }
    if (document.size() > K_COLLISION_SUPPORT_DOCUMENT_BYTES) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED,
                    "Collision-support document exceeds byte budget");
    }
    const auto K_VALID = validate_scene(source);
    if (!K_VALID) {
        return std::unexpected(K_VALID.error());
    }
    if (!source.m_world_frame || !source.m_world_frame->m_synthetic) {
        return fail(ErrorCode::UNSUPPORTED_OPERATION,
                    "Only explicit synthetic aligned support is implemented; measured collision "
                    "unavailable");
    }
    if (document.find('\0') != document.npos ||
        sha256(std::as_bytes(std::span(document.data(), document.size()))) != digest) {
        return fail(ErrorCode::INVALID_DATA, "Collision-support checksum or text differs");
    }
    try {
        std::vector<std::set<std::string>> keys;
        const auto K_DOCUMENT =
            Json::parse(document, [&](int depth, Json::parse_event_t event, Json& value) {
                if (depth > 8) {
                    throw std::runtime_error("Collision-support nesting exceeds budget");
                }
                const auto K_DEPTH = static_cast<std::size_t>(depth);
                if (event == Json::parse_event_t::object_start) {
                    keys.resize(K_DEPTH + 1uz);
                    keys[K_DEPTH].clear();
                }
                if (event == Json::parse_event_t::key &&
                    !keys.at(K_DEPTH - 1uz).insert(value.get<std::string>()).second) {
                    throw std::runtime_error("Duplicate collision-support key");
                }
                return true;
            });
        fields(K_DOCUMENT,
               { "schema",
                "scene_sha256",
                "epoch",
                "revision",
                "world_frame_sha256",
                "grid",
                "floors" });
        if (K_DOCUMENT.at("schema") != "SYNTHETIC_COLLISION_SUPPORT_V1") {
            throw std::runtime_error("Unsupported collision-support profile");
        }
        if (integer(K_DOCUMENT.at("epoch")) != source.m_epoch.m_value) {
            return fail(ErrorCode::WRONG_EPOCH,
                        "Collision support uses a different world epoch; resync");
        }
        const auto K_SOURCE_HASH = scene_content_hash(source);
        if (integer(K_DOCUMENT.at("revision")) != source.m_revision.m_value ||
            K_DOCUMENT.at("scene_sha256").get<std::string>() != K_SOURCE_HASH ||
            K_DOCUMENT.at("world_frame_sha256").get<std::string>() !=
                source.m_world_frame->m_document_sha256) {
            return fail(ErrorCode::WRONG_BASE,
                        "Collision support uses different scene/frame content; resync");
        }
        const auto& grid = K_DOCUMENT.at("grid");
        fields(grid, { "origin", "cell_size", "dimensions", "states" });
        if (!grid.at("origin").is_array() || grid.at("origin").size() != 3uz ||
            !grid.at("dimensions").is_array() || grid.at("dimensions").size() != 3uz) {
            throw std::runtime_error("Collision grid vectors must have three components");
        }
        auto support = std::make_shared<CollisionSupport>(CollisionSupport::AdmissionToken {});
        support->m_scene = source.m_id;
        support->m_epoch = source.m_epoch;
        support->m_revision = source.m_revision;
        support->m_source_sha256 = K_SOURCE_HASH;
        support->m_document_sha256 = digest;
        support->m_origin = { real(grid.at("origin").at(0uz)),
                             real(grid.at("origin").at(1uz)),
                             real(grid.at("origin").at(2uz)) };
        support->m_cell_size = real(grid.at("cell_size"));
        if (support->m_cell_size < 1e-6 || support->m_cell_size > 1000 ||
            std::abs(support->m_origin.m_x) > 1e6 || std::abs(support->m_origin.m_y) > 1e6 ||
            std::abs(support->m_origin.m_z) > 1e6) {
            throw std::runtime_error(
                "Collision grid exceeds diagnostic coordinate/precision limits");
        }
        std::size_t count { 1uz };
        for (std::size_t axis { 0uz }; axis < 3uz; ++axis) {
            const auto K_SIZE = integer(grid.at("dimensions").at(axis));
            if (K_SIZE == 0 || K_SIZE > 64) {
                throw std::runtime_error("Collision grid dimensions must be 1..64");
            }
            support->m_dimensions[axis] = static_cast<std::size_t>(K_SIZE);
            count *= static_cast<std::size_t>(K_SIZE);
        }
        const auto K_STATES = grid.at("states").get<std::string>();
        if (K_STATES.size() != count || K_STATES.find_first_not_of("?.#") != K_STATES.npos) {
            throw std::runtime_error(
                "Collision states must explicitly cover every cell with ?, . or #");
        }
        support->m_cells.reserve(count);
        for (char state : K_STATES) {
            support->m_cells.push_back(state == '.'   ? SpaceState::FREE
                                       : state == '#' ? SpaceState::OCCUPIED
                                                      : SpaceState::UNKNOWN);
        }
        const auto& floors = K_DOCUMENT.at("floors");
        if (!floors.is_array() || floors.empty() || floors.size() > 256uz) {
            throw std::runtime_error("Collision floor reference count exceeds profile limits");
        }
        std::set<std::pair<ChunkId, std::uint64_t>> seen;
        std::vector<GridTriangle> triangles;
        for (const auto& floor : floors) {
            fields(floor, { "chunk_id", "generation", "pose_revision", "chunk_sha256", "triangles" });
            const ChunkId K_ID { integer(floor.at("chunk_id")) };
            const auto K_FOUND = source.m_chunks.find(K_ID);
            if (K_FOUND == source.m_chunks.end()) {
                throw std::runtime_error("Missing collision floor chunk");
            }
            const auto& chunk = *K_FOUND->second;
            if (!chunk.m_mesh || !chunk.m_geometry_valid) {
                throw std::runtime_error("Collision floor requires current mesh geometry");
            }
            if (integer(floor.at("generation")) != chunk.m_generation ||
                integer(floor.at("pose_revision")) != chunk.m_pose_revision.m_value ||
                floor.at("chunk_sha256").get<std::string>() != scene_chunk_content_hash(chunk)) {
                return fail(ErrorCode::WRONG_BASE, "Collision floor mesh/pose dependency differs");
            }
            const auto& indices = floor.at("triangles");
            if (!indices.is_array() || indices.empty() ||
                indices.size() > 256uz - triangles.size()) {
                throw std::runtime_error("Collision floor triangle budget exceeded");
            }
            const auto K_TRANSFORM = scene_world_from_chunk(source, chunk);
            const auto& mesh = *chunk.m_mesh;
            for (const auto& index : indices) {
                const auto K_INDEX = integer(index);
                if (K_INDEX >= mesh.m_triangles.size() || !seen.emplace(K_ID, K_INDEX).second) {
                    throw std::runtime_error("Invalid/duplicate collision floor triangle");
                }
                GridTriangle triangle;
                for (std::size_t corner { 0uz }; corner < 3uz; ++corner) {
                    const auto& p =
                        mesh.m_vertices
                            .at(mesh.m_triangles.at(static_cast<std::size_t>(K_INDEX))[corner])
                            .m_position;
                    const auto K_WORLD = transform_point(K_TRANSFORM, { p[0uz], p[1uz], p[2uz] });
                    const double K_X { (K_WORLD.m_x - support->m_origin.m_x) / support->m_cell_size };
                    const double K_Z { (K_WORLD.m_z - support->m_origin.m_z) / support->m_cell_size };
                    if (K_WORLD.m_y != support->m_origin.m_y || !std::isfinite(K_X) ||
                        !std::isfinite(K_Z) || K_X < 0 ||
                        K_X > static_cast<double>(support->m_dimensions[0uz]) || K_Z < 0 ||
                        K_Z > static_cast<double>(support->m_dimensions[2uz]) ||
                        K_X != std::round(K_X) || K_Z != std::round(K_Z) ||
                        K_WORLD.m_x != support->m_origin.m_x + K_X * support->m_cell_size ||
                        K_WORLD.m_z != support->m_origin.m_z + K_Z * support->m_cell_size) {
                        throw std::runtime_error(
                            "Diagnostic floors must lie exactly on the bounded flat grid lattice");
                    }
                    triangle[corner] = { static_cast<std::int64_t>(K_X),
                                        static_cast<std::int64_t>(K_Z) };
                }
                const auto& a = triangle[0uz];
                const auto& b = triangle[1uz];
                const auto& c = triangle[2uz];
                if ((b[1uz] - a[1uz]) * (c[0uz] - a[0uz]) - (b[0uz] - a[0uz]) * (c[1uz] - a[1uz]) <=
                    0) {
                    throw std::runtime_error(
                        "Collision floor must have upward nondegenerate geometric winding");
                }
                triangles.push_back(triangle);
            }
        }
        const auto K_NX = support->m_dimensions[0uz], K_NZ = support->m_dimensions[2uz];
        support->m_floors.assign(K_NX * K_NZ, 0);
        for (std::size_t z { 0uz }; z < K_NZ; ++z) {
            if (stop.stop_requested()) {
                return fail(ErrorCode::CANCELLED, "Collision floor admission cancelled");
            }
            for (std::size_t x { 0uz }; x < K_NX; ++x) {
                const auto K_X = static_cast<std::int64_t>(x), K_Z = static_cast<std::int64_t>(z);
                for (const auto& triangle : triangles) {
                    if (inside(triangle, { K_X, K_Z }) && inside(triangle, { K_X + 1, K_Z }) &&
                        inside(triangle, { K_X, K_Z + 1 }) && inside(triangle, { K_X + 1, K_Z + 1 })) {
                        support->m_floors[x + K_NX * z] = 1;
                        break;
                    }
                }
            }
        }
        return std::shared_ptr<const CollisionSupport> { std::move(support) };
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Collision-support allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA,
                    std::string("Invalid collision support: ") + error.what());
    }
}

Result<ClearanceProbe> probe_collision_support(const CollisionSupport& support,
                                               const SceneState& source,
                                               const CapsuleSweep& sweep,
                                               const std::stop_token& stop) {
    if (stop.stop_requested()) {
        return fail(ErrorCode::CANCELLED, "Collision clearance probe cancelled");
    }
    if (source.m_id != support.m_scene || source.m_epoch != support.m_epoch) {
        return fail(ErrorCode::WRONG_EPOCH, "Collision context changed; stop and resync");
    }
    if (source.m_revision != support.m_revision) {
        return fail(ErrorCode::WRONG_BASE, "Collision scene revision changed; stop and resync");
    }
    const auto K_VALID = validate_scene(source);
    if (!K_VALID) {
        return std::unexpected(K_VALID.error());
    }
    if (scene_content_hash(source) != support.m_source_sha256) {
        return fail(ErrorCode::WRONG_BASE, "Collision source content changed; stop and resync");
    }
    if (!finite(sweep.m_start_feet) || !finite(sweep.m_end_feet) ||
        !std::isfinite(sweep.m_radius) || !std::isfinite(sweep.m_height) || sweep.m_radius <= 0 ||
        sweep.m_height <= 0 || sweep.m_radius > sweep.m_height / 2) {
        return fail(ErrorCode::INVALID_ARGUMENT, "Invalid diagnostic capsule dimensions or sweep");
    }
    if (sweep.m_start_feet.m_y != support.m_origin.m_y ||
        sweep.m_end_feet.m_y != support.m_origin.m_y) {
        return ClearanceProbe { ClearanceState::UNSUPPORTED_FLOOR, 0uz };
    }
    const auto K_DOWN = -std::numeric_limits<double>::infinity();
    const auto K_UP = std::numeric_limits<double>::infinity();
    const std::array<double, 3uz> K_MINIMUM {
        std::nextafter(std::min(sweep.m_start_feet.m_x, sweep.m_end_feet.m_x) - sweep.m_radius,
                       K_DOWN),
        support.m_origin.m_y,
        std::nextafter(std::min(sweep.m_start_feet.m_z, sweep.m_end_feet.m_z) - sweep.m_radius,
                       K_DOWN) };
    const std::array<double, 3uz> K_MAXIMUM {
        std::nextafter(std::max(sweep.m_start_feet.m_x, sweep.m_end_feet.m_x) + sweep.m_radius,
                       K_UP),
        std::nextafter(support.m_origin.m_y + sweep.m_height, K_UP),
        std::nextafter(std::max(sweep.m_start_feet.m_z, sweep.m_end_feet.m_z) + sweep.m_radius,
                       K_UP) };
    const std::array<double, 3uz> K_ORIGIN {
        support.m_origin.m_x, support.m_origin.m_y, support.m_origin.m_z };
    std::array<std::size_t, 3uz> first {}, last {};
    for (std::size_t axis { 0uz }; axis < 3uz; ++axis) {
        const double K_LOWER {
            axis == 1uz
                ? 0.
                : std::nextafter((K_MINIMUM[axis] - K_ORIGIN[axis]) / support.m_cell_size, K_DOWN) };
        const double K_UPPER {
            std::nextafter((K_MAXIMUM[axis] - K_ORIGIN[axis]) / support.m_cell_size, K_UP) };
        if (!std::isfinite(K_LOWER) || !std::isfinite(K_UPPER) || K_LOWER < 0 ||
            K_UPPER >= static_cast<double>(support.m_dimensions[axis])) {
            return ClearanceProbe { ClearanceState::UNKNOWN, 0uz };
        }
        first[axis] = static_cast<std::size_t>(std::floor(K_LOWER));
        last[axis] = static_cast<std::size_t>(std::floor(K_UPPER));
    }
    ClearanceProbe result { ClearanceState::CLEAR_DIAGNOSTIC, 0uz };
    const auto K_NX = support.m_dimensions[0uz], K_NY = support.m_dimensions[1uz];
    for (std::size_t z { first[2uz] }; z <= last[2uz]; ++z) {
        if (stop.stop_requested()) {
            return fail(ErrorCode::CANCELLED, "Collision clearance probe cancelled");
        }
        for (std::size_t x { first[0uz] }; x <= last[0uz]; ++x) {
            if (!support.m_floors[x + K_NX * z]) {
                return ClearanceProbe { ClearanceState::UNSUPPORTED_FLOOR, result.m_checked_cells };
            }
            for (std::size_t y { first[1uz] }; y <= last[1uz]; ++y) {
                const auto K_STATE = support.m_cells[x + K_NX * (y + K_NY * z)];
                ++result.m_checked_cells;
                if (K_STATE != SpaceState::FREE) {
                    return ClearanceProbe { K_STATE == SpaceState::OCCUPIED
                                               ? ClearanceState::OCCUPIED
                                               : ClearanceState::UNKNOWN,
                                           result.m_checked_cells };
                }
            }
        }
    }
    return result;
}

std::string_view clearance_label(ClearanceState state) {
    switch (state) {
    case ClearanceState::CLEAR_DIAGNOSTIC:
        return "CLEAR_DIAGNOSTIC";
    case ClearanceState::UNKNOWN:
        return "UNKNOWN";
    case ClearanceState::OCCUPIED:
        return "OCCUPIED";
    case ClearanceState::UNSUPPORTED_FLOOR:
        return "UNSUPPORTED_FLOOR";
    }
    return "INVALID";
}

}  // namespace world_explorer
