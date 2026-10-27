#pragma once

#include "world_explorer/application/geometry_product.hpp"

#include <span>

namespace world_explorer {

struct NativeColliderPolicy {
    std::size_t m_max_vertices { 8192uz }, m_max_triangles { 8192uz };
    bool m_weld_vertices {}, m_double_sided {};
};

struct NativeColliderSelection {
    AssetRevisionRef m_product;
    NativeColliderPolicy m_policy;
};

struct NativeColliderRequest {
    std::optional<NativeColliderSelection> m_selection;
    std::optional<AdmittedGeometryProduct> m_base;
    std::optional<AdmittedAssetReceipt> m_receipt;
    AssetDependencyContext m_expected;
};

struct NativeColliderMesh {
    std::vector<Vec3d> m_positions;
    std::vector<std::array<std::uint32_t, 3>> m_triangles;
    std::vector<std::uint32_t> m_source_faces;
};

// Exact static structural surface in arbitrary base-geometry units. Portable
// cook input only: no backend shape, support ledger, convex props or walking.
class AdmittedNativeCollider {
  public:
    AdmittedNativeCollider(const AdmittedNativeCollider&) = default;
    AdmittedNativeCollider& operator=(const AdmittedNativeCollider&) = default;
    const NativeColliderSelection& selection() const;
    const AdmittedGeometryProduct& base() const;
    const NativeColliderMesh& mesh() const;
    std::span<const std::byte> canonical_bytes() const;
    const std::string& digest() const;
    const std::string& receipt_sha256() const;

  private:
    struct Implementation;
    explicit AdmittedNativeCollider(std::shared_ptr<const Implementation> state);
    std::shared_ptr<const Implementation> m_state;
    friend Result<AdmittedNativeCollider> inspect_native_collider(const NativeColliderRequest&,
                                                                  const std::stop_token&);
    friend Result<RecordCommit> publish_native_collider(const AdmittedNativeCollider&,
                                                        const AdmittedAssetReceipt&,
                                                        const std::filesystem::path&,
                                                        OperationId,
                                                        std::uint64_t,
                                                        const AssetDependencyContext&,
                                                        const std::stop_token&,
                                                        const std::function<void(std::size_t)>&);
};

// <=1 MiB root; independent counts <=8192 vertices/faces. Every original vertex
// and face appears exactly once, with original positions and oriented topology.
Result<AdmittedNativeCollider> inspect_native_collider(const NativeColliderRequest& request,
                                                       const std::stop_token& stop = {});
Result<RecordCommit> publish_native_collider(const AdmittedNativeCollider& collider,
                                             const AdmittedAssetReceipt& receipt,
                                             const std::filesystem::path& directory,
                                             OperationId operation,
                                             std::uint64_t base_generation,
                                             const AssetDependencyContext& expected,
                                             const std::stop_token& stop = {},
                                             const std::function<void(std::size_t)>& observed = {});
Result<AdmittedNativeCollider> read_native_collider(const std::filesystem::path& directory,
                                                    const RecordCommit& commit,
                                                    const NativeColliderRequest& request,
                                                    const std::stop_token& stop = {});

}  // namespace world_explorer
