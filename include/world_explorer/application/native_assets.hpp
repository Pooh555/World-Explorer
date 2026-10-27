#pragma once

#include "world_explorer/application/completion_product.hpp"
#include "world_explorer/application/material_product.hpp"
#include "world_explorer/application/native_lod.hpp"
#include "world_explorer/scene_store/asset_instances_io.hpp"

namespace world_explorer {

struct NativeAssetSelection {
    AssetDefinition m_definition;
    std::vector<AssetProvenanceSelection> m_provenance;
    std::optional<AdmittedGeometryProduct> m_geometry;
    std::vector<AdmittedMaterialProduct> m_materials;
    std::optional<AdmittedCompletionProduct> m_completion;
    // Independently selected original placement; other instances may be copies.
    AssetInstanceId m_source_instance;
    // Ordered like definition.m_lods; each certificate is relative to LOD0.
    std::vector<AdmittedNativeLod> m_lods {};
};

struct NativeAssetsRequest {
    std::optional<AdmittedAssetReceipt> m_receipt;
    std::vector<NativeAssetSelection> m_assets;
    AssetInstanceSet m_instances;
    AssetDependencyContext m_expected;
};

// Original admitted products remain owned alongside their exact metadata. This
// is draft adoption, not renderer publication, export or AP qualification.
class AdmittedNativeAssets {
  public:
    AdmittedNativeAssets(const AdmittedNativeAssets&) = default;
    AdmittedNativeAssets& operator=(const AdmittedNativeAssets&) = default;
    const AdmittedAssetInstances& instances() const;
    const std::vector<NativeAssetSelection>& sources() const;
    const std::string& receipt_sha256() const;
    bool synthetic_source() const;

  private:
    struct Implementation;
    explicit AdmittedNativeAssets(std::shared_ptr<const Implementation> state);
    std::shared_ptr<const Implementation> m_state;
    friend Result<AdmittedNativeAssets> inspect_native_assets(const NativeAssetsRequest&,
                                                              const std::stop_token&);
};

// <=16 definitions, <=16 materials/definition, <=64 instances, arbitrary units,
// exact certified LODs; no collider or room/support/lineage adoption.
// Every base/LOD face group and provenance partition is bound.
// Schema-1 aggregate material provenance must exactly match every channel's
// partition. Schema 2 binds each original channel and its own provenance sidecar.
Result<AdmittedNativeAssets> inspect_native_assets(const NativeAssetsRequest& request,
                                                   const std::stop_token& stop = {});
Result<RecordCommit> publish_native_assets(const AdmittedNativeAssets& assets,
                                           const AdmittedAssetReceipt& receipt,
                                           const std::filesystem::path& directory,
                                           OperationId operation,
                                           std::uint64_t base_generation,
                                           const AssetDependencyContext& expected,
                                           const std::stop_token& stop = {},
                                           const std::function<void(std::size_t)>& observed = {});
Result<AdmittedNativeAssets> read_native_assets(const std::filesystem::path& directory,
                                                const RecordCommit& commit,
                                                const NativeAssetsRequest& request,
                                                const std::stop_token& stop = {});

}  // namespace world_explorer
