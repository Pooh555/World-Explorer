#pragma once

#include "world_explorer/asset_catalog/region_provenance.hpp"
#include "world_explorer/map_store/capture_selection.hpp"
#include "world_explorer/scene_store/scene.hpp"

#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

namespace world_explorer {

struct GeometryNativeSource {
    SceneId m_scene;
    WorldEpoch m_epoch;
    SceneRevisionId m_revision;
    std::string m_scene_sha256;
    ChunkId m_chunk;
    std::uint64_t m_generation {};
    std::string m_chunk_sha256;
    auto operator<=>(const GeometryNativeSource&) const = default;
};

struct GeometryProductSelection {
    AssetRevisionRef m_product, m_source;
    GeometryNativeSource m_native;
    std::vector<ObjectViewSelection> m_views;
    double m_max_deviation_units {};
};

struct GeometryAnchor {
    std::uint32_t m_triangle {};
    std::array<double, 3> m_weights {};
};

struct GeometryFaceSupport {
    AssetEvidenceClass m_class;
    std::optional<std::array<GeometryAnchor, 3>> m_anchors;
    std::vector<AssetSourceViewId> m_views;
    std::optional<double> m_uncertainty_units;
    double m_deviation_units {};
};

struct GeometryRuntimeMesh {
    std::vector<Vec3d> m_positions, m_normals;
    // Both empty when UV/tangent production is explicitly absent.
    std::vector<std::array<double, 2>> m_uv;
    std::vector<std::array<double, 4>> m_tangents;
    std::vector<std::array<std::uint32_t, 3>> m_triangles;
    std::vector<AssetMaterialVariantId> m_groups;
};

struct GeometryProductRequest {
    std::optional<GeometryProductSelection> m_selection;
    std::optional<AdmittedCaptureSelection> m_capture;
    std::optional<AdmittedAssetReceipt> m_receipt;
    SceneSnapshot m_source;
    AssetDependencyContext m_expected;
};

// Owned geometry/correspondence admission. No worker, physical-support or AP/Q/N
// approval, measured units, asset-definition adoption or renderer transaction.
class AdmittedGeometryProduct {
  public:
    AdmittedGeometryProduct(const AdmittedGeometryProduct&) = default;
    AdmittedGeometryProduct& operator=(const AdmittedGeometryProduct&) = default;
    const GeometryProductSelection& selection() const;
    const GeometryRuntimeMesh& mesh() const;
    const SceneMesh& high_mesh() const;
    const SimilarityTransform& world_from_geometry() const;
    const std::vector<GeometryFaceSupport>& support() const;
    const std::array<std::uint64_t, 5>& coverage() const;
    const std::string& receipt_sha256() const;
    const AssetRevisionRef& capture_product() const;
    const AssetDependencyContext& context() const;
    bool synthetic_source() const;
    bool manual_ancestor() const;
    bool closed_surface() const;

  private:
    struct Implementation;
    explicit AdmittedGeometryProduct(std::shared_ptr<const Implementation> state);
    std::shared_ptr<const Implementation> m_state;
    friend Result<AdmittedGeometryProduct> inspect_geometry_product(const GeometryProductRequest&,
                                                                    const std::stop_token&);
    friend Result<RecordCommit> publish_geometry_product(const AdmittedGeometryProduct&,
                                                         const AdmittedAssetReceipt&,
                                                         const std::filesystem::path&,
                                                         OperationId,
                                                         std::uint64_t,
                                                         const AssetDependencyContext&,
                                                         const std::stop_token&,
                                                         const std::function<void(std::size_t)>&);
};

// Root <=4 MiB / 8192 vertices and faces; source binding <=16 KiB; 1..16
// inference views. Native source <=32 MiB /64 chunks, untextured arbitrary units.
// Missing independently selected prerequisites refuse before interpreting data.
Result<AdmittedGeometryProduct> inspect_geometry_product(const GeometryProductRequest& request,
                                                         const std::stop_token& stop = {});
Result<RecordCommit>
publish_geometry_product(const AdmittedGeometryProduct& product,
                         const AdmittedAssetReceipt& receipt,
                         const std::filesystem::path& directory,
                         OperationId operation,
                         std::uint64_t base_generation,
                         const AssetDependencyContext& expected,
                         const std::stop_token& stop = {},
                         const std::function<void(std::size_t)>& observed = {});
Result<AdmittedGeometryProduct> read_geometry_product(const std::filesystem::path& directory,
                                                      const RecordCommit& commit,
                                                      const GeometryProductRequest& request,
                                                      const std::stop_token& stop = {});

}  // namespace world_explorer
