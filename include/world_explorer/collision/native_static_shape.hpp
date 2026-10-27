#pragma once

#include "world_explorer/scene_store/native_collider_input.hpp"

#include <optional>
#include <span>

namespace world_explorer {

struct NativeShapePolicy {
    double m_max_local_vertex_error_units { 1e-4 };
    double m_max_world_ray_endpoint_error_units { 1e-4 };
};

struct NativeShapeReport {
    std::string m_backend, m_adapter_source_sha256, m_geometry_sha256, m_build_sha256;
    std::size_t m_triangle_count {};
    double m_local_vertex_error_units {};
    NativeShapePolicy m_policy;
};

struct NativeShapeTriangle {
    std::array<Vec3d, 3> m_vertices;
    std::uint32_t m_source_face {};
};

struct NativeShapeRayHit {
    double m_fraction {};
    Vec3d m_world_position;
    std::uint32_t m_source_face {};
};

struct NativeShapeRayResult {
    std::optional<NativeShapeRayHit> m_hit;
    double m_world_endpoint_error_units {}, m_world_surface_error_units {};
};

// Diagnostic static gameplay geometry only. Owns no body, instance placement,
// support ledger or navigation token. No conversion to the measured/M3 owner.
class NativeStaticCollision {
  public:
    ~NativeStaticCollision();
    NativeStaticCollision(const NativeStaticCollision&) = delete;
    NativeStaticCollision& operator=(const NativeStaticCollision&) = delete;
    const ColliderInputBinding& binding() const;
    const NativeShapeReport& report() const;
    std::span<const NativeShapeTriangle> backend_triangles() const;

    // Caller selects the current immutable instance frame on every call; there
    // is no cached placement or scene admission. Positive uniform scale only.
    // Returned point interpolates the original segment, not a clearance proof.
    Result<NativeShapeRayResult> ray_cast(const ColliderInputBinding& current,
                                          const SimilarityTransform& world_from_base,
                                          Vec3d start,
                                          Vec3d end,
                                          const std::stop_token& stop = {}) const;

  private:
    struct Implementation;
    explicit NativeStaticCollision(std::unique_ptr<Implementation> state);
    std::unique_ptr<Implementation> m_state;
    friend Result<std::shared_ptr<const NativeStaticCollision>>
    build_native_static_collision(const AdmittedColliderInput&,
                                  const ColliderInputBinding&,
                                  const NativeShapePolicy&,
                                  const std::stop_token&);
};

// Revalidates selected source pins. Checks float conversion, sanitizer and
// decoded compressed triangles against independently selected loss policy.
// Backend/source/config hashes identify this build; no serialized cache ABI.
Result<std::shared_ptr<const NativeStaticCollision>>
build_native_static_collision(const AdmittedColliderInput& input,
                              const ColliderInputBinding& current,
                              const NativeShapePolicy& policy = {},
                              const std::stop_token& stop = {});

}  // namespace world_explorer
