#pragma once

#include "world_explorer/asset_catalog/dependency_graph.hpp"
#include "world_explorer/core/math.hpp"

#include <array>
#include <memory>
#include <span>

namespace world_explorer {

struct NativeColliderMesh {
    std::vector<Vec3d> m_positions;
    std::vector<std::array<std::uint32_t, 3>> m_triangles;
    std::vector<std::uint32_t> m_source_faces;
};

struct ColliderInputBinding {
    AssetDependencyContext m_context;
    AssetRevisionRef m_collider, m_base;
    std::string m_receipt_sha256, m_canonical_sha256;
};

// Owned, bounded replay of portable static gameplay triangle input. The selected
// hashes are integrity bindings, not receipt/geometry/rights or walking authority.
class AdmittedColliderInput {
  public:
    AdmittedColliderInput(const AdmittedColliderInput&) = default;
    AdmittedColliderInput& operator=(const AdmittedColliderInput&) = default;
    const ColliderInputBinding& binding() const;
    const NativeColliderMesh& mesh() const;
    std::span<const std::byte> canonical_bytes() const;
    Result<void> validate_binding(const ColliderInputBinding& current,
                                  const std::stop_token& stop = {}) const;

  private:
    struct Implementation;
    explicit AdmittedColliderInput(std::shared_ptr<const Implementation> state);
    std::shared_ptr<const Implementation> m_state;
    friend Result<AdmittedColliderInput> decode_native_collider_input(std::span<const std::byte>,
                                                                      const ColliderInputBinding&,
                                                                      const std::stop_token&);
};

// Existing WE_NATIVE_COLLIDER_TRIANGLES_V1; <=1 MiB /8192 vertices and faces.
// Exact independent header/hash selection, finite arrays, indices, no unused
// vertices, degenerate/duplicate faces, trailing bytes or unsupported flags.
Result<AdmittedColliderInput> decode_native_collider_input(std::span<const std::byte> bytes,
                                                           const ColliderInputBinding& selected,
                                                           const std::stop_token& stop = {});

}  // namespace world_explorer
