#pragma once

#include "world_explorer/asset_catalog/asset_definition.hpp"

namespace world_explorer {

struct AssetInstanceLineage {
    AssetInstanceId m_instance;
    AssetRevisionRef m_product;
};

struct AssetInstance {
    AssetInstanceId m_instance;
    AssetDefinitionRef m_definition;
    SimilarityTransform m_world_from_asset;
    AssetMaterialVariantId m_material_variant;
    bool m_visible { true };
    std::optional<AssetRevisionRef> m_room, m_support;
    std::vector<AssetInstanceLineage> m_lineage;
    bool m_manual_ancestor {};
};

struct AssetInstanceSet {
    std::uint32_t m_schema_version { 1 };
    SceneId m_scene;
    SceneRevisionId m_revision;
    AssetDependencyContext m_context;
    AssetUnits m_units;
    std::optional<AssetRevisionRef> m_manual_action;
    std::vector<AssetInstance> m_instances;
    bool m_manual_ancestor {};
};

class AdmittedAssetInstances {
  public:
    AdmittedAssetInstances(const AdmittedAssetInstances&) = default;
    AdmittedAssetInstances& operator=(const AdmittedAssetInstances&) = default;
    const AssetInstanceSet& instances() const;
    const std::vector<AdmittedAssetDefinition>& definitions() const;
    const std::string& canonical_json() const;
    const std::string& sha256() const;

  private:
    struct Implementation;
    explicit AdmittedAssetInstances(std::shared_ptr<const Implementation> implementation);
    std::shared_ptr<const Implementation> m_impl;
    friend Result<AdmittedAssetInstances>
    admit_asset_instances(const AssetInstanceSet&,
                          const std::vector<AdmittedAssetDefinition>&,
                          const AdmittedAssetReceipt&,
                          const AssetDependencyContext&,
                          const std::stop_token&);
};

// Typed metadata only: <=64 definitions, <=4096 instances, <=16 lineage refs per
// instance / <=16384 total; all handles must use this exact admitted receipt.
// Uniform positive scales only, single unit frame, no runtime publication.
Result<AdmittedAssetInstances>
admit_asset_instances(const AssetInstanceSet& instances,
                      const std::vector<AdmittedAssetDefinition>& definitions,
                      const AdmittedAssetReceipt& receipt,
                      const AssetDependencyContext& expected,
                      const std::stop_token& stop = {});

}  // namespace world_explorer
