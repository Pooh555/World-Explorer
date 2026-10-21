#pragma once

#include "world_explorer/scene_store/scene.hpp"

#include <span>
#include <stop_token>
#include <string_view>

namespace world_explorer {

// Explicit diagnostic evidence only. Neither allocated TSDF voxels nor drawable
// triangles imply free space. There is no measured walking capability here.
enum class SpaceState : std::uint8_t { UNKNOWN, FREE, OCCUPIED };
enum class ClearanceState : std::uint8_t { CLEAR_DIAGNOSTIC, UNKNOWN, OCCUPIED, UNSUPPORTED_FLOOR };

struct CapsuleSweep {
    Vec3d m_start_feet, m_end_feet;
    double m_radius {}, m_height {};
};

struct ClearanceProbe {
    ClearanceState m_state { ClearanceState::UNKNOWN };
    std::size_t m_checked_cells {};
};

struct CollisionTriangleRef {
    ChunkId m_chunk_id;
    std::uint64_t m_generation {};
    SceneRevisionId m_pose_revision;
    std::string m_chunk_sha256;
    std::uint64_t m_triangle_index {};
};

using CollisionFloorTriangleRef = CollisionTriangleRef;

constexpr std::size_t K_COLLISION_SUPPORT_DOCUMENT_BYTES { 1024uz * 1024uz };

// Immutable, admitted synthetic support. X varies fastest, then Y, then Z.
// Floor flags use X then Z and require one source triangle to cover a full tile.
class CollisionSupport {
    struct AdmissionToken { };

  public:
    explicit CollisionSupport(AdmissionToken) { }

    CollisionSupport(const CollisionSupport&) = delete;
    CollisionSupport& operator=(const CollisionSupport&) = delete;

    Vec3d origin() const {
        return m_origin;
    }

    double cell_size() const {
        return m_cell_size;
    }

    Vec3d floor_gradient() const {
        return m_floor_gradient;
    }

    double floor_height(Vec3d point) const {
        if (m_format_version == 4U && point.m_x >= m_step_boundary_x) {
            return m_origin.m_y + m_step_height;
        }
        return m_origin.m_y + m_floor_gradient.m_x * (point.m_x - m_origin.m_x) +
               m_floor_gradient.m_z * (point.m_z - m_origin.m_z);
    }

    std::array<std::size_t, 3uz> dimensions() const {
        return m_dimensions;
    }

    std::span<const SpaceState> cells() const {
        return m_cells;
    }

    std::span<const std::uint8_t> floors() const {
        return m_floors;
    }

    std::span<const CollisionFloorTriangleRef> floor_triangles() const {
        return m_floor_triangles;
    }

    std::span<const CollisionTriangleRef> wall_triangles() const {
        return m_wall_triangles;
    }

    std::span<const CollisionTriangleRef> riser_triangles() const {
        return m_riser_triangles;
    }

    std::uint32_t format_version() const {
        return m_format_version;
    }

    const std::string& source_sha256() const {
        return m_source_sha256;
    }

    const std::string& document_sha256() const {
        return m_document_sha256;
    }

    Result<void> validate_source(const SceneState& source, const std::stop_token& stop = {}) const;

  private:
    friend Result<std::shared_ptr<const CollisionSupport>> admit_collision_support(
        const SceneState&, std::string_view, std::string_view, const std::stop_token&);
    friend Result<ClearanceProbe> probe_collision_support(const CollisionSupport&,
                                                          const SceneState&,
                                                          const CapsuleSweep&,
                                                          const std::stop_token&);

    SceneId m_scene;
    WorldEpoch m_epoch;
    SceneRevisionId m_revision;
    Vec3d m_origin;
    Vec3d m_floor_gradient;
    double m_step_boundary_x {}, m_step_height {};
    std::size_t m_step_boundary_cell {};
    double m_cell_size {};
    std::array<std::size_t, 3uz> m_dimensions {};
    std::vector<SpaceState> m_cells;
    std::vector<std::uint8_t> m_floors;
    std::vector<CollisionFloorTriangleRef> m_floor_triangles;
    std::vector<CollisionTriangleRef> m_wall_triangles;
    std::vector<CollisionTriangleRef> m_riser_triangles;
    std::uint32_t m_format_version { 1 };
    std::string m_source_sha256, m_document_sha256;
};

Result<std::shared_ptr<const CollisionSupport>>
admit_collision_support(const SceneState& source,
                        std::string_view document,
                        std::string_view sha256,
                        const std::stop_token& stop = {});

// Checks the continuous capsule envelope: a world AABB for V1/V2, or a
// plane-relative prism-grid box for V3. Conservative at diagonals and seams.
// Diagnostic clearance only; this function is not a character controller.
Result<ClearanceProbe> probe_collision_support(const CollisionSupport& support,
                                               const SceneState& source,
                                               const CapsuleSweep& sweep,
                                               const std::stop_token& stop = {});
std::string_view clearance_label(ClearanceState state);

}  // namespace world_explorer
