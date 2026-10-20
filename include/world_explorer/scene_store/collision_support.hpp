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

constexpr std::size_t K_COLLISION_SUPPORT_DOCUMENT_BYTES { 1024uz * 1024uz };

// Immutable, admitted synthetic support. X varies fastest, then Y, then Z.
// Floor flags use X then Z and require one source triangle to cover a full tile.
class CollisionSupport {
    struct AdmissionToken {};

  public:
    explicit CollisionSupport(AdmissionToken) {}

    CollisionSupport(const CollisionSupport&) = delete;
    CollisionSupport& operator=(const CollisionSupport&) = delete;

    Vec3d origin() const {
        return m_origin;
    }

    double cell_size() const {
        return m_cell_size;
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

    const std::string& source_sha256() const {
        return m_source_sha256;
    }

    const std::string& document_sha256() const {
        return m_document_sha256;
    }

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
    double m_cell_size {};
    std::array<std::size_t, 3uz> m_dimensions {};
    std::vector<SpaceState> m_cells;
    std::vector<std::uint8_t> m_floors;
    std::string m_source_sha256, m_document_sha256;
};

Result<std::shared_ptr<const CollisionSupport>>
admit_collision_support(const SceneState& source,
                        std::string_view document,
                        std::string_view sha256,
                        const std::stop_token& stop = {});

// Checks every touched cell in the continuous swept AABB enclosing a vertical
// capsule. Deliberately conservative at diagonals, seams and cell boundaries.
// Flat floor only; this is a diagnostic clearance probe, not a character controller.
Result<ClearanceProbe> probe_collision_support(const CollisionSupport& support,
                                               const SceneState& source,
                                               const CapsuleSweep& sweep,
                                               const std::stop_token& stop = {});
std::string_view clearance_label(ClearanceState state);

}  // namespace world_explorer
