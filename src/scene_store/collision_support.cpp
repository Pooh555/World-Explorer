#include "world_explorer/scene_store/collision_support.hpp"

#include "world_explorer/core/hash.hpp"
#include "world_explorer/scene_store/world_frame.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <limits>
#include <set>
#include <tuple>
#include <utility>

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
        const bool K_V2 = K_DOCUMENT.is_object() && K_DOCUMENT.contains("schema") &&
                          K_DOCUMENT.at("schema") == "SYNTHETIC_COLLISION_SUPPORT_V2";
        const bool K_V3 = K_DOCUMENT.is_object() && K_DOCUMENT.contains("schema") &&
                          K_DOCUMENT.at("schema") == "SYNTHETIC_COLLISION_SUPPORT_V3";
        const bool K_V4 = K_DOCUMENT.is_object() && K_DOCUMENT.contains("schema") &&
                          K_DOCUMENT.at("schema") == "SYNTHETIC_COLLISION_SUPPORT_V4";
        if (K_V4) {
            fields(K_DOCUMENT,
                   { "schema",
                    "scene_sha256",
                    "epoch",
                    "revision",
                    "world_frame_sha256",
                    "grid",
                    "floors",
                    "walls",
                    "step" });
        } else if (K_V3) {
            fields(K_DOCUMENT,
                   { "schema",
                    "scene_sha256",
                    "epoch",
                    "revision",
                    "world_frame_sha256",
                    "grid",
                    "floors",
                    "walls",
                    "plane" });
        } else if (K_V2) {
            fields(K_DOCUMENT,
                   { "schema",
                    "scene_sha256",
                    "epoch",
                    "revision",
                    "world_frame_sha256",
                    "grid",
                    "floors",
                    "walls" });
        } else {
            fields(K_DOCUMENT,
                   { "schema",
                    "scene_sha256",
                    "epoch",
                    "revision",
                    "world_frame_sha256",
                    "grid",
                    "floors" });
        }
        if (!K_V2 && !K_V3 && !K_V4 &&
            K_DOCUMENT.at("schema") != "SYNTHETIC_COLLISION_SUPPORT_V1") {
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
        support->m_format_version = K_V4 ? 4U : K_V3 ? 3U : K_V2 ? 2U : 1U;
        support->m_origin = { real(grid.at("origin").at(0uz)),
                             real(grid.at("origin").at(1uz)),
                             real(grid.at("origin").at(2uz)) };
        support->m_cell_size = real(grid.at("cell_size"));
        if (K_V3) {
            const auto& plane = K_DOCUMENT.at("plane");
            fields(plane, { "rise_x", "rise_z" });
            support->m_floor_gradient = { real(plane.at("rise_x")), 0, real(plane.at("rise_z")) };
            if (std::abs(support->m_floor_gradient.m_x) > 4 ||
                std::abs(support->m_floor_gradient.m_z) > 4) {
                throw std::runtime_error("Synthetic plane gradient exceeds bounded profile");
            }
        }
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
        if (K_V4) {
            const auto& step = K_DOCUMENT.at("step");
            fields(step, { "boundary_x", "height", "riser" });
            const auto K_BOUNDARY = integer(step.at("boundary_x"));
            support->m_step_height = real(step.at("height"));
            if (K_BOUNDARY == 0 || K_BOUNDARY >= support->m_dimensions[0uz] ||
                support->m_step_height <= 0 || support->m_step_height > .30) {
                throw std::runtime_error("Synthetic step exceeds bounded height or grid");
            }
            support->m_step_boundary_x =
                support->m_origin.m_x + static_cast<double>(K_BOUNDARY) * support->m_cell_size;
            support->m_step_boundary_cell = static_cast<std::size_t>(K_BOUNDARY);
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
        std::vector<std::pair<GridTriangle, double>> triangles;
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
            const auto K_CHUNK_HASH = scene_chunk_content_hash(chunk);
            if (integer(floor.at("generation")) != chunk.m_generation ||
                integer(floor.at("pose_revision")) != chunk.m_pose_revision.m_value ||
                floor.at("chunk_sha256").get<std::string>() != K_CHUNK_HASH) {
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
                support->m_floor_triangles.push_back(
                    { K_ID, chunk.m_generation, chunk.m_pose_revision, K_CHUNK_HASH, K_INDEX });
                GridTriangle triangle;
                double triangle_height {};
                for (std::size_t corner { 0uz }; corner < 3uz; ++corner) {
                    const auto& p =
                        mesh.m_vertices
                            .at(mesh.m_triangles.at(static_cast<std::size_t>(K_INDEX))[corner])
                            .m_position;
                    const auto K_WORLD = transform_point(K_TRANSFORM, { p[0uz], p[1uz], p[2uz] });
                    const double K_X { (K_WORLD.m_x - support->m_origin.m_x) / support->m_cell_size };
                    const double K_Z { (K_WORLD.m_z - support->m_origin.m_z) / support->m_cell_size };
                    if (corner == 0uz) {
                        triangle_height = K_WORLD.m_y;
                    }
                    const bool K_STEP_VERTEX {
                        K_V4 && K_WORLD.m_y == triangle_height &&
                        ((triangle_height == support->m_origin.m_y &&
                          K_WORLD.m_x <= support->m_step_boundary_x) ||
                         (triangle_height == support->m_origin.m_y + support->m_step_height &&
                          K_WORLD.m_x >= support->m_step_boundary_x)) };
                    if ((K_V4 ? !K_STEP_VERTEX : K_WORLD.m_y != support->floor_height(K_WORLD)) ||
                        !std::isfinite(K_X) || !std::isfinite(K_Z) || K_X < 0 ||
                        K_X > static_cast<double>(support->m_dimensions[0uz]) || K_Z < 0 ||
                        K_Z > static_cast<double>(support->m_dimensions[2uz]) ||
                        K_X != std::round(K_X) || K_Z != std::round(K_Z) ||
                        K_WORLD.m_x != support->m_origin.m_x + K_X * support->m_cell_size ||
                        K_WORLD.m_z != support->m_origin.m_z + K_Z * support->m_cell_size) {
                        throw std::runtime_error("Diagnostic floors must lie exactly on the "
                                                 "declared plane and X/Z lattice");
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
                triangles.emplace_back(triangle, triangle_height);
            }
        }
        if (K_V2 || K_V3 || K_V4) {
            const auto& walls = K_DOCUMENT.at("walls");
            if (!walls.is_array() || (K_V2 && walls.empty()) || (K_V4 && !walls.empty()) ||
                walls.size() > 128uz ||
                walls.size() > (256uz - support->m_floor_triangles.size()) / 2uz) {
                throw std::runtime_error("Collision wall face count exceeds profile limits");
            }
            std::set<std::tuple<std::array<std::uint64_t, 3uz>, std::string>> seen_faces;
            for (const auto& wall : walls) {
                if (stop.stop_requested()) {
                    return fail(ErrorCode::CANCELLED, "Collision wall admission cancelled");
                }
                fields(wall,
                       { "chunk_id",
                        "generation",
                        "pose_revision",
                        "chunk_sha256",
                        "occupied_cell",
                        "face",
                        "triangles" });
                const ChunkId K_ID { integer(wall.at("chunk_id")) };
                const auto K_FOUND = source.m_chunks.find(K_ID);
                if (K_FOUND == source.m_chunks.end() || !K_FOUND->second ||
                    !K_FOUND->second->m_mesh || !K_FOUND->second->m_geometry_valid) {
                    throw std::runtime_error("Collision wall requires current mesh geometry");
                }
                const auto& chunk = *K_FOUND->second;
                const auto K_CHUNK_HASH = scene_chunk_content_hash(chunk);
                if (integer(wall.at("generation")) != chunk.m_generation ||
                    integer(wall.at("pose_revision")) != chunk.m_pose_revision.m_value ||
                    wall.at("chunk_sha256").get<std::string>() != K_CHUNK_HASH) {
                    return fail(ErrorCode::WRONG_BASE,
                                "Collision wall mesh/pose dependency differs");
                }
                const auto& cell_json = wall.at("occupied_cell");
                if (!cell_json.is_array() || cell_json.size() != 3uz) {
                    throw std::runtime_error("Collision wall cell must have three indices");
                }
                std::array<std::uint64_t, 3uz> cell {};
                for (std::size_t axis { 0uz }; axis < 3uz; ++axis) {
                    cell[axis] = integer(cell_json.at(axis));
                    if (cell[axis] >= support->m_dimensions[axis]) {
                        throw std::runtime_error("Collision wall cell leaves the grid");
                    }
                }
                const auto K_CELL_OFFSET =
                    static_cast<std::size_t>(cell[0uz]) +
                    support->m_dimensions[0uz] *
                        (static_cast<std::size_t>(cell[1uz]) +
                         support->m_dimensions[1uz] * static_cast<std::size_t>(cell[2uz]));
                if (support->m_cells[K_CELL_OFFSET] != SpaceState::OCCUPIED) {
                    throw std::runtime_error(
                        "Collision wall face requires an occupied source cell");
                }
                const auto K_FACE = wall.at("face").get<std::string>();
                if (K_FACE != "NEG_X" && K_FACE != "POS_X" && K_FACE != "NEG_Z" &&
                    K_FACE != "POS_Z") {
                    throw std::runtime_error(
                        "Collision wall face must be vertical and axis aligned");
                }
                if (!seen_faces.emplace(cell, K_FACE).second) {
                    throw std::runtime_error("Duplicate collision wall face");
                }
                const double K_X0 { support->m_origin.m_x +
                                   static_cast<double>(cell[0uz]) * support->m_cell_size };
                const double K_Y0 { support->m_origin.m_y +
                                   static_cast<double>(cell[1uz]) * support->m_cell_size };
                const double K_Z0 { support->m_origin.m_z +
                                   static_cast<double>(cell[2uz]) * support->m_cell_size };
                const double K_X1 { K_X0 + support->m_cell_size };
                const double K_Y1 { K_Y0 + support->m_cell_size };
                const double K_Z1 { K_Z0 + support->m_cell_size };
                const bool K_X_FACE = K_FACE == "NEG_X" || K_FACE == "POS_X";
                const double K_FIXED = K_X_FACE ? (K_FACE == "NEG_X" ? K_X0 : K_X1)
                                                : (K_FACE == "NEG_Z" ? K_Z0 : K_Z1);
                const auto K_CORNER = [&](double x, double y, double z) {
                    Vec3d point { x, y, z };
                    if (K_V3) {
                        point.m_y += support->floor_height(point) - support->m_origin.m_y;
                    }
                    return point;
                };
                const std::array<Vec3d, 4uz> K_CORNERS =
                    K_X_FACE ? std::array<Vec3d, 4uz> { K_CORNER(K_FIXED, K_Y0, K_Z0),
                                                       K_CORNER(K_FIXED, K_Y1, K_Z0),
                                                       K_CORNER(K_FIXED, K_Y0, K_Z1),
                                                       K_CORNER(K_FIXED, K_Y1, K_Z1) }
                             : std::array<Vec3d, 4uz> { K_CORNER(K_X0, K_Y0, K_FIXED),
                                                       K_CORNER(K_X1, K_Y0, K_FIXED),
                                                       K_CORNER(K_X0, K_Y1, K_FIXED),
                                                       K_CORNER(K_X1, K_Y1, K_FIXED) };
                const Vec3d K_OUTWARD = K_FACE == "NEG_X"   ? Vec3d { -1, 0, 0 }
                                        : K_FACE == "POS_X" ? Vec3d { 1, 0, 0 }
                                        : K_FACE == "NEG_Z" ? Vec3d { 0, 0, -1 }
                                                            : Vec3d { 0, 0, 1 };
                const auto& indices = wall.at("triangles");
                if (!indices.is_array() || indices.size() != 2uz) {
                    throw std::runtime_error("Collision wall face needs exactly two triangles");
                }
                std::array<std::array<std::size_t, 3uz>, 2uz> corner_ids {};
                for (std::size_t side { 0uz }; side < 2uz; ++side) {
                    const auto K_INDEX = integer(indices.at(side));
                    const auto& mesh = *chunk.m_mesh;
                    if (K_INDEX >= mesh.m_triangles.size() || !seen.emplace(K_ID, K_INDEX).second) {
                        throw std::runtime_error("Invalid/duplicate collision wall triangle");
                    }
                    const auto K_TRANSFORM = scene_world_from_chunk(source, chunk);
                    std::array<Vec3d, 3uz> points {};
                    for (std::size_t corner { 0uz }; corner < 3uz; ++corner) {
                        const auto K_VERTEX =
                            mesh.m_triangles.at(static_cast<std::size_t>(K_INDEX))[corner];
                        const auto& position = mesh.m_vertices.at(K_VERTEX).m_position;
                        points[corner] = transform_point(
                            K_TRANSFORM, { position[0uz], position[1uz], position[2uz] });
                        const auto K_MATCH = std::find_if(
                            K_CORNERS.begin(), K_CORNERS.end(), [&](const Vec3d& expected) {
                                return points[corner].m_x == expected.m_x &&
                                       points[corner].m_y == expected.m_y &&
                                       points[corner].m_z == expected.m_z;
                            });
                        if (K_MATCH == K_CORNERS.end()) {
                            throw std::runtime_error(
                                "Collision wall triangle leaves its occupied cell face");
                        }
                        corner_ids[side][corner] =
                            static_cast<std::size_t>(K_MATCH - K_CORNERS.begin());
                    }
                    auto unique = corner_ids[side];
                    std::sort(unique.begin(), unique.end());
                    if (unique[0uz] == unique[1uz] || unique[1uz] == unique[2uz] ||
                        dot(cross(points[1uz] - points[0uz], points[2uz] - points[0uz]),
                            K_OUTWARD) <= 0) {
                        throw std::runtime_error(
                            "Collision wall has invalid winding or degeneracy");
                    }
                    support->m_wall_triangles.push_back(
                        { K_ID, chunk.m_generation, chunk.m_pose_revision, K_CHUNK_HASH, K_INDEX });
                }
                std::array<std::size_t, 2uz> shared {};
                std::size_t shared_count {};
                for (const auto K_CORNER_ID : corner_ids[0uz]) {
                    if (std::find(corner_ids[1uz].begin(), corner_ids[1uz].end(), K_CORNER_ID) !=
                        corner_ids[1uz].end()) {
                        if (shared_count < shared.size()) {
                            shared[shared_count] = K_CORNER_ID;
                        }
                        ++shared_count;
                    }
                }
                if (shared_count != 2uz || shared[0uz] + shared[1uz] != 3uz) {
                    throw std::runtime_error("Collision wall triangles do not cover the full face");
                }
            }
        }
        if (K_V4) {
            const auto& riser = K_DOCUMENT.at("step").at("riser");
            fields(riser, { "chunk_id", "generation", "pose_revision", "chunk_sha256", "triangles" });
            const ChunkId K_ID { integer(riser.at("chunk_id")) };
            const auto K_FOUND = source.m_chunks.find(K_ID);
            if (K_FOUND == source.m_chunks.end() || !K_FOUND->second || !K_FOUND->second->m_mesh ||
                !K_FOUND->second->m_geometry_valid) {
                throw std::runtime_error("Synthetic step riser requires current mesh geometry");
            }
            const auto& chunk = *K_FOUND->second;
            const auto K_CHUNK_HASH = scene_chunk_content_hash(chunk);
            if (integer(riser.at("generation")) != chunk.m_generation ||
                integer(riser.at("pose_revision")) != chunk.m_pose_revision.m_value ||
                riser.at("chunk_sha256").get<std::string>() != K_CHUNK_HASH) {
                return fail(ErrorCode::WRONG_BASE, "Synthetic step riser source differs");
            }
            const auto& indices = riser.at("triangles");
            if (!indices.is_array() || indices.size() != 2uz ||
                support->m_floor_triangles.size() + support->m_wall_triangles.size() > 254uz) {
                throw std::runtime_error("Synthetic step riser needs two selected triangles");
            }
            const double K_X { support->m_step_boundary_x };
            const double K_Y { support->m_origin.m_y };
            const double K_Z { support->m_origin.m_z };
            const double K_Z_END { K_Z + static_cast<double>(support->m_dimensions[2uz]) *
                                            support->m_cell_size };
            const std::array<Vec3d, 4uz> K_CORNERS { { { K_X, K_Y, K_Z },
                                                     { K_X, K_Y + support->m_step_height, K_Z },
                                                     { K_X, K_Y, K_Z_END },
                                                     { K_X, K_Y + support->m_step_height, K_Z_END } } };
            std::array<std::array<std::size_t, 3uz>, 2uz> corner_ids {};
            const auto K_TRANSFORM = scene_world_from_chunk(source, chunk);
            for (std::size_t side { 0uz }; side < 2uz; ++side) {
                const auto K_INDEX = integer(indices.at(side));
                if (K_INDEX >= chunk.m_mesh->m_triangles.size() ||
                    !seen.emplace(K_ID, K_INDEX).second) {
                    throw std::runtime_error("Invalid/duplicate synthetic step riser triangle");
                }
                std::array<Vec3d, 3uz> points {};
                for (std::size_t corner { 0uz }; corner < 3uz; ++corner) {
                    const auto K_VERTEX =
                        chunk.m_mesh->m_triangles.at(static_cast<std::size_t>(K_INDEX))[corner];
                    const auto& p = chunk.m_mesh->m_vertices.at(K_VERTEX).m_position;
                    points[corner] = transform_point(K_TRANSFORM, { p[0uz], p[1uz], p[2uz] });
                    const auto K_MATCH = std::find_if(
                        K_CORNERS.begin(), K_CORNERS.end(), [&](const Vec3d& expected) {
                            return points[corner].m_x == expected.m_x &&
                                   points[corner].m_y == expected.m_y &&
                                   points[corner].m_z == expected.m_z;
                        });
                    if (K_MATCH == K_CORNERS.end()) {
                        throw std::runtime_error("Synthetic step riser leaves selected edge");
                    }
                    corner_ids[side][corner] =
                        static_cast<std::size_t>(K_MATCH - K_CORNERS.begin());
                }
                auto unique = corner_ids[side];
                std::sort(unique.begin(), unique.end());
                if (unique[0uz] == unique[1uz] || unique[1uz] == unique[2uz] ||
                    dot(cross(points[1uz] - points[0uz], points[2uz] - points[0uz]), { -1, 0, 0 }) <=
                        0) {
                    throw std::runtime_error("Synthetic step riser winding is invalid");
                }
                support->m_riser_triangles.push_back(
                    { K_ID, chunk.m_generation, chunk.m_pose_revision, K_CHUNK_HASH, K_INDEX });
            }
            std::size_t shared_count {};
            std::size_t shared_sum {};
            for (const auto K_CORNER : corner_ids[0uz]) {
                if (std::find(corner_ids[1uz].begin(), corner_ids[1uz].end(), K_CORNER) !=
                    corner_ids[1uz].end()) {
                    ++shared_count;
                    shared_sum += K_CORNER;
                }
            }
            if (shared_count != 2uz || shared_sum != 3uz) {
                throw std::runtime_error("Synthetic step riser triangles leave a gap");
            }
        }
        const auto K_NX = support->m_dimensions[0uz], K_NZ = support->m_dimensions[2uz];
        const bool K_WALKABLE { dot(support->m_floor_gradient, support->m_floor_gradient) <= 1 };
        support->m_floors.assign(K_NX * K_NZ, 0);
        for (std::size_t z { 0uz }; z < K_NZ; ++z) {
            if (stop.stop_requested()) {
                return fail(ErrorCode::CANCELLED, "Collision floor admission cancelled");
            }
            for (std::size_t x { 0uz }; x < K_NX; ++x) {
                if (!K_WALKABLE) {
                    continue;
                }
                const auto K_X = static_cast<std::int64_t>(x), K_Z = static_cast<std::int64_t>(z);
                for (const auto& [K_TRIANGLE, K_HEIGHT] : triangles) {
                    if (K_V4 &&
                        K_HEIGHT != support->floor_height(
                                        { support->m_origin.m_x +
                                             (static_cast<double>(x) + .5) * support->m_cell_size,
                                         0,
                                         support->m_origin.m_z + (static_cast<double>(z) + .5) *
                                                                     support->m_cell_size })) {
                        continue;
                    }
                    if (inside(K_TRIANGLE, { K_X, K_Z }) && inside(K_TRIANGLE, { K_X + 1, K_Z }) &&
                        inside(K_TRIANGLE, { K_X, K_Z + 1 }) &&
                        inside(K_TRIANGLE, { K_X + 1, K_Z + 1 })) {
                        support->m_floors[x + K_NX * z] = 1;
                        break;
                    }
                }
            }
        }
        if (K_V4) {
            bool lower {}, upper {}, paired_landing {};
            for (std::size_t z { 0uz }; z < K_NZ; ++z) {
                for (std::size_t x { 0uz }; x < K_NX; ++x) {
                    if (support->m_floors[x + K_NX * z]) {
                        (x < support->m_step_boundary_cell ? lower : upper) = true;
                    }
                }
                paired_landing =
                    paired_landing ||
                    (support->m_floors[support->m_step_boundary_cell - 1uz + K_NX * z] &&
                     support->m_floors[support->m_step_boundary_cell + K_NX * z]);
            }
            if (!lower || !upper || !paired_landing) {
                throw std::runtime_error("Synthetic step lacks paired selected landing support");
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

Result<void> CollisionSupport::validate_source(const SceneState& source,
                                               const std::stop_token& stop) const {
    if (stop.stop_requested()) {
        return fail(ErrorCode::CANCELLED, "Collision source validation cancelled");
    }
    if (source.m_id != m_scene || source.m_epoch != m_epoch) {
        return fail(ErrorCode::WRONG_EPOCH, "Collision context changed; stop and resync");
    }
    if (source.m_revision != m_revision) {
        return fail(ErrorCode::WRONG_BASE, "Collision scene revision changed; stop and resync");
    }
    const auto K_VALID = validate_scene(source);
    if (!K_VALID) {
        return std::unexpected(K_VALID.error());
    }
    if (scene_content_hash(source) != m_source_sha256) {
        return fail(ErrorCode::WRONG_BASE, "Collision source content changed; stop and resync");
    }
    return {};
}

Result<ClearanceProbe> probe_collision_support(const CollisionSupport& support,
                                               const SceneState& source,
                                               const CapsuleSweep& sweep,
                                               const std::stop_token& stop) {
    const auto K_VALID = support.validate_source(source, stop);
    if (!K_VALID) {
        return std::unexpected(K_VALID.error());
    }
    if (!finite(sweep.m_start_feet) || !finite(sweep.m_end_feet) ||
        !std::isfinite(sweep.m_radius) || !std::isfinite(sweep.m_height) || sweep.m_radius <= 0 ||
        sweep.m_height <= 0 || sweep.m_radius > sweep.m_height / 2) {
        return fail(ErrorCode::INVALID_ARGUMENT, "Invalid diagnostic capsule dimensions or sweep");
    }
    if (support.m_format_version == 4U) {
        // The V4 source proof only admits selected geometry. A straight feet
        // segment cannot certify the vertical lift around a step riser.
        return ClearanceProbe { ClearanceState::UNSUPPORTED_FLOOR, 0uz };
    }
    const bool K_V3 { support.m_format_version == 3U };
    double lower_height {}, upper_height { sweep.m_height };
    if (K_V3) {
        const auto K_GRADIENT = support.m_floor_gradient;
        const double K_SLOPE_SQUARED { dot(K_GRADIENT, K_GRADIENT) };
        const double K_EXTRA { sweep.m_radius * (std::sqrt(1 + K_SLOPE_SQUARED) - 1) };
        const double K_START_HEIGHT { sweep.m_start_feet.m_y -
                                     support.floor_height(sweep.m_start_feet) };
        const double K_END_HEIGHT { sweep.m_end_feet.m_y - support.floor_height(sweep.m_end_feet) };
        lower_height = std::min(K_START_HEIGHT, K_END_HEIGHT) - K_EXTRA;
        upper_height = std::max(K_START_HEIGHT, K_END_HEIGHT) + sweep.m_height + K_EXTRA;
        if (K_SLOPE_SQUARED > 1 || lower_height < 0 ||
            std::max(K_START_HEIGHT, K_END_HEIGHT) - K_EXTRA > .01) {
            return ClearanceProbe { ClearanceState::UNSUPPORTED_FLOOR, 0uz };
        }
    } else if (sweep.m_start_feet.m_y != support.m_origin.m_y ||
               sweep.m_end_feet.m_y != support.m_origin.m_y) {
        return ClearanceProbe { ClearanceState::UNSUPPORTED_FLOOR, 0uz };
    }
    const auto K_DOWN = -std::numeric_limits<double>::infinity();
    const auto K_UP = std::numeric_limits<double>::infinity();
    const std::array<double, 3uz> K_MINIMUM {
        std::nextafter(std::min(sweep.m_start_feet.m_x, sweep.m_end_feet.m_x) - sweep.m_radius,
                       K_DOWN),
        support.m_origin.m_y + lower_height,
        std::nextafter(std::min(sweep.m_start_feet.m_z, sweep.m_end_feet.m_z) - sweep.m_radius,
                       K_DOWN) };
    const std::array<double, 3uz> K_MAXIMUM {
        std::nextafter(std::max(sweep.m_start_feet.m_x, sweep.m_end_feet.m_x) + sweep.m_radius,
                       K_UP),
        std::nextafter(support.m_origin.m_y + upper_height, K_UP),
        std::nextafter(std::max(sweep.m_start_feet.m_z, sweep.m_end_feet.m_z) + sweep.m_radius,
                       K_UP) };
    const std::array<double, 3uz> K_ORIGIN {
        support.m_origin.m_x, support.m_origin.m_y, support.m_origin.m_z };
    std::array<std::size_t, 3uz> first {}, last {};
    for (std::size_t axis { 0uz }; axis < 3uz; ++axis) {
        const double K_LOWER {
            axis == 1uz
                ? (K_V3 ? std::max(0.0, std::nextafter(lower_height / support.m_cell_size, K_DOWN))
                        : 0.)
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
