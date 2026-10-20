#include "world_explorer/collision/triangle_cook.hpp"

#include "world_explorer/core/hash.hpp"
#include "world_explorer/scene_store/world_frame.hpp"

#include <algorithm>
#include <bit>
#include <stdexcept>

namespace world_explorer {
namespace {

void append_u64(std::vector<std::byte>& bytes, std::uint64_t value) {
    for (std::size_t index { 0uz }; index < 8uz; ++index) {
        bytes.push_back(static_cast<std::byte>((value >> (8uz * index)) & 255uz));
    }
}

void append_text(std::vector<std::byte>& bytes, std::string_view value) {
    const auto K_DATA = std::as_bytes(std::span(value.data(), value.size()));
    bytes.insert(bytes.end(), K_DATA.begin(), K_DATA.end());
}

void append_coordinate(std::vector<std::byte>& bytes, double value) {
    // Equivalent positive and negative zero coordinates have one encoding.
    append_u64(bytes, std::bit_cast<std::uint64_t>(value == 0 ? 0.0 : value));
}

}  // namespace

Result<std::shared_ptr<const CanonicalCollisionMesh>>
cook_synthetic_collision(const SceneSnapshot& source,
                         std::shared_ptr<const CollisionSupport> support,
                         const std::stop_token& stop) {
    if (!source || !support) {
        return fail(ErrorCode::INVALID_ARGUMENT, "Collision cook requires source and support");
    }
    const auto K_VALID = support->validate_source(*source, stop);
    if (!K_VALID) {
        return std::unexpected(K_VALID.error());
    }
    if (!source->m_world_frame || !source->m_world_frame->m_synthetic) {
        return fail(ErrorCode::UNSUPPORTED_OPERATION,
                    "Canonical collision cook requires synthetic alignment");
    }
    try {
        auto cooked =
            std::make_shared<CanonicalCollisionMesh>(CanonicalCollisionMesh::AdmissionToken {});
        cooked->m_scene = source->m_id;
        cooked->m_epoch = source->m_epoch;
        cooked->m_revision = source->m_revision;
        cooked->m_support = std::move(support);
        cooked->m_source_sha256 = cooked->m_support->source_sha256();
        auto selected =
            std::vector<CollisionFloorTriangleRef> {cooked->m_support->floor_triangles().begin(),
                                                    cooked->m_support->floor_triangles().end()};
        std::sort(selected.begin(), selected.end(), [](const auto& a, const auto& b) {
            return a.m_chunk_id < b.m_chunk_id ||
                   (a.m_chunk_id == b.m_chunk_id && a.m_triangle_index < b.m_triangle_index);
        });
        if (selected.empty() || selected.size() > 256uz) {
            return fail(ErrorCode::INVALID_DATA, "Collision floor triangle count is invalid");
        }
        cooked->m_triangles.reserve(selected.size());
        auto& bytes = cooked->m_canonical_bytes;
        bytes.reserve(256uz + selected.size() * 104uz);
        append_text(bytes, "WE_COLLISION_TRIANGLES_V1");
        append_u64(bytes, cooked->m_parameters.m_format_version);
        append_u64(bytes, cooked->m_parameters.m_weld_vertices);
        append_u64(bytes, cooked->m_parameters.m_double_sided);
        append_u64(bytes, cooked->m_scene.m_value);
        append_u64(bytes, cooked->m_epoch.m_value);
        append_u64(bytes, cooked->m_revision.m_value);
        append_text(bytes, cooked->m_source_sha256);
        append_text(bytes, cooked->m_support->document_sha256());
        append_u64(bytes, selected.size());
        for (const auto& ref : selected) {
            if (stop.stop_requested()) {
                return fail(ErrorCode::CANCELLED, "Collision triangle cook cancelled");
            }
            const auto K_FOUND = source->m_chunks.find(ref.m_chunk_id);
            if (K_FOUND == source->m_chunks.end() || !K_FOUND->second) {
                return fail(ErrorCode::WRONG_BASE, "Collision floor geometry disappeared");
            }
            const auto& chunk = *K_FOUND->second;
            if (!chunk.m_mesh || !chunk.m_geometry_valid) {
                return fail(ErrorCode::WRONG_BASE, "Collision floor geometry disappeared");
            }
            const auto& mesh = *chunk.m_mesh;
            if (chunk.m_generation != ref.m_generation ||
                chunk.m_pose_revision != ref.m_pose_revision ||
                scene_chunk_content_hash(chunk) != ref.m_chunk_sha256 ||
                ref.m_triangle_index >= mesh.m_triangles.size()) {
                return fail(ErrorCode::WRONG_BASE, "Collision floor dependency changed");
            }
            CollisionCookTriangle triangle;
            triangle.m_chunk_id = ref.m_chunk_id;
            triangle.m_generation = ref.m_generation;
            triangle.m_pose_revision = ref.m_pose_revision;
            triangle.m_triangle_index = ref.m_triangle_index;
            const auto K_WORLD = scene_world_from_chunk(*source, chunk);
            for (std::size_t corner { 0uz }; corner < 3uz; ++corner) {
                const auto K_INDEX =
                    mesh.m_triangles.at(static_cast<std::size_t>(ref.m_triangle_index))[corner];
                const auto& position = mesh.m_vertices.at(K_INDEX).m_position;
                auto point =
                    transform_point(K_WORLD, {position[0uz], position[1uz], position[2uz]});
                if (!finite(point)) {
                    return fail(ErrorCode::INVALID_DATA, "Collision triangle is nonfinite");
                }
                if (point.m_x == 0) {
                    point.m_x = 0;
                }
                if (point.m_y == 0) {
                    point.m_y = 0;
                }
                if (point.m_z == 0) {
                    point.m_z = 0;
                }
                triangle.m_world_vertices[corner] = point;
            }
            const auto K_NORMAL =
                cross(triangle.m_world_vertices[1uz] - triangle.m_world_vertices[0uz],
                      triangle.m_world_vertices[2uz] - triangle.m_world_vertices[0uz]);
            if (!finite(K_NORMAL) || dot(K_NORMAL, K_NORMAL) <= 0) {
                return fail(ErrorCode::INVALID_DATA, "Collision triangle is degenerate");
            }
            append_u64(bytes, ref.m_chunk_id.m_value);
            append_u64(bytes, ref.m_generation);
            append_u64(bytes, ref.m_pose_revision.m_value);
            append_u64(bytes, ref.m_triangle_index);
            for (const auto& point : triangle.m_world_vertices) {
                append_coordinate(bytes, point.m_x);
                append_coordinate(bytes, point.m_y);
                append_coordinate(bytes, point.m_z);
            }
            cooked->m_triangles.push_back(triangle);
        }
        cooked->m_digest = sha256(std::span<const std::byte>(bytes.data(), bytes.size()));
        return std::shared_ptr<const CanonicalCollisionMesh> {std::move(cooked)};
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Collision cook allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA,
                    std::string("Invalid collision cook: ") + error.what());
    }
}

Result<void> CanonicalCollisionMesh::validate_source(const SceneState& current,
                                                     const std::stop_token& stop) const {
    if (current.m_id != m_scene || current.m_epoch != m_epoch) {
        return fail(ErrorCode::WRONG_EPOCH, "Collision cook scene or epoch changed; resync");
    }
    if (current.m_revision != m_revision) {
        return fail(ErrorCode::WRONG_BASE, "Collision cook revision changed; resync");
    }
    const auto K_VALID = m_support->validate_source(current, stop);
    if (!K_VALID) {
        return std::unexpected(K_VALID.error());
    }
    if (m_source_sha256 != m_support->source_sha256()) {
        return fail(ErrorCode::WRONG_BASE, "Collision cook source binding changed");
    }
    return {};
}

Result<ClearanceProbe> CanonicalCollisionMesh::probe(const SceneState& current,
                                                     const CapsuleSweep& sweep,
                                                     const std::stop_token& stop) const {
    const auto K_VALID = validate_source(current, stop);
    if (!K_VALID) {
        return std::unexpected(K_VALID.error());
    }
    return probe_collision_support(*m_support, current, sweep, stop);
}

}  // namespace world_explorer
