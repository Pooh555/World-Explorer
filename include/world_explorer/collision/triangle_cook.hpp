#pragma once

#include "world_explorer/scene_store/collision_support.hpp"

#include <cstddef>
#include <memory>
#include <span>
#include <stop_token>
#include <vector>

namespace world_explorer {

// Fixed canonical input for a later collision backend. No welding, generated
// faces, measured scale or walking permission is implied by this recipe.
struct CollisionCookParameters {
    std::uint32_t m_format_version { 1 };
    bool m_weld_vertices {};
    bool m_double_sided {};
};

struct CollisionCookTriangle {
    ChunkId m_chunk_id;
    std::uint64_t m_generation {};
    SceneRevisionId m_pose_revision;
    std::uint64_t m_triangle_index {};
    std::array<Vec3d, 3uz> m_world_vertices {};
};

// Source-bound immutable synthetic triangle input and explicit support ledger.
// The byte stream is portable and deterministic; it is not a Jolt cache.
class CanonicalCollisionMesh {
    struct AdmissionToken { };

  public:
    explicit CanonicalCollisionMesh(AdmissionToken) { }

    CanonicalCollisionMesh(const CanonicalCollisionMesh&) = delete;
    CanonicalCollisionMesh& operator=(const CanonicalCollisionMesh&) = delete;

    CollisionCookParameters parameters() const {
        return m_parameters;
    }

    std::span<const CollisionCookTriangle> triangles() const {
        return m_triangles;
    }

    std::size_t floor_triangle_count() const {
        return m_floor_triangle_count;
    }

    std::size_t wall_triangle_count() const {
        return m_triangles.size() - m_floor_triangle_count - m_riser_triangle_count;
    }

    std::size_t riser_triangle_count() const {
        return m_riser_triangle_count;
    }

    std::span<const std::byte> canonical_bytes() const {
        return m_canonical_bytes;
    }

    const std::string& digest() const {
        return m_digest;
    }

    const std::string& source_sha256() const {
        return m_source_sha256;
    }

    const std::string& support_sha256() const {
        return m_support->document_sha256();
    }

    double cell_size() const {
        return m_support->cell_size();
    }

    Vec3d floor_gradient() const {
        return m_support->floor_gradient();
    }

    double floor_height(Vec3d point) const {
        return m_support->floor_height(point);
    }

    Result<void> validate_source(const SceneState& current, const std::stop_token& stop = {}) const;

    // Diagnostic envelope probe only; the support ledger owns unknown blocking.
    Result<ClearanceProbe> probe(const SceneState& current,
                                 const CapsuleSweep& sweep,
                                 const std::stop_token& stop = {}) const;
    Result<ClearanceProbe> probe_transition(const SceneState& current,
                                            const CapsuleSweep& sweep,
                                            const std::stop_token& stop = {}) const;
    Result<ClearanceProbe> probe_motion(const SceneState& current,
                                        const CapsuleSweep& sweep,
                                        const std::stop_token& stop = {}) const;

  private:
    friend Result<std::shared_ptr<const CanonicalCollisionMesh>> cook_synthetic_collision(
        const SceneSnapshot&, std::shared_ptr<const CollisionSupport>, const std::stop_token&);

    SceneId m_scene;
    WorldEpoch m_epoch;
    SceneRevisionId m_revision;
    CollisionCookParameters m_parameters;
    std::shared_ptr<const CollisionSupport> m_support;
    std::vector<CollisionCookTriangle> m_triangles;
    std::size_t m_floor_triangle_count {};
    std::size_t m_riser_triangle_count {};
    std::vector<std::byte> m_canonical_bytes;
    std::string m_source_sha256, m_digest;
};

Result<std::shared_ptr<const CanonicalCollisionMesh>>
cook_synthetic_collision(const SceneSnapshot& source,
                         std::shared_ptr<const CollisionSupport> support,
                         const std::stop_token& stop = {});

}  // namespace world_explorer
