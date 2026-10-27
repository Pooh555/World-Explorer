#pragma once

#include "world_explorer/asset_catalog/material_channel.hpp"
#include "world_explorer/asset_catalog/region_provenance.hpp"
#include "world_explorer/core/math.hpp"

#include <memory>

namespace world_explorer {

enum class AssetKind : std::uint8_t { PROP, STRUCTURAL };
enum class AssetLightingMode : std::uint8_t { CAPTURED_APPEARANCE, ESTIMATED_PBR, GENERATED_PBR };
enum class AssetUnitStatus : std::uint8_t { ARBITRARY, DECLARED_METRIC };
enum class AssetPivotPolicy : std::uint8_t {
    SUPPORT_BASE_CENTER,
    ATTACHMENT_ORIGIN,
    STRUCTURAL_FRAME,
    OVERRIDE
};
enum class AssetColliderMode : std::uint8_t { UNVERIFIED, SYNTHETIC_GAMEPLAY };

struct AssetUnits {
    AssetUnitStatus m_status { AssetUnitStatus::ARBITRARY };
    std::optional<double> m_meters_per_unit;
    std::optional<AssetRevisionRef> m_measurement;
    auto operator<=>(const AssetUnits&) const = default;
};

struct AssetDefinitionRef {
    AssetId m_asset;
    std::uint64_t m_revision {};
    std::string m_sha256;
    auto operator<=>(const AssetDefinitionRef&) const = default;
};

struct AssetMaterialChannelBinding {
    MaterialChannelRole m_role;
    AssetRevisionRef m_product, m_provenance;
};

struct AssetMaterialBinding {
    AssetMaterialVariantId m_variant;
    AssetRevisionRef m_product;
    AssetLightingMode m_lighting;
    // Schema 2 uses the original per-channel provenance, without a root summary.
    std::vector<AssetMaterialChannelBinding> m_channels {};
};

struct AssetColliderBinding {
    AssetRevisionRef m_product;
    AssetColliderMode m_mode;
};

struct AssetPivot {
    AssetPivotPolicy m_policy;
    SimilarityTransform m_asset_from_geometry;
    bool m_orientation_ambiguous {};
    std::optional<AssetRevisionRef> m_override_action;
};

// No placement or capture lights. Bounds are in the declared pivoted asset frame.
struct AssetDefinition {
    std::uint32_t m_schema_version { 1 };
    AssetId m_asset;
    std::uint64_t m_revision {};
    AssetDependencyContext m_context;
    AssetKind m_kind;
    std::vector<AssetRevisionRef> m_sources;
    AssetRevisionRef m_geometry;
    std::vector<AssetMaterialBinding> m_materials;
    std::vector<AssetRevisionRef> m_lods;
    std::optional<AssetColliderBinding> m_collider;
    AssetUnits m_units;
    Bounds3d m_bounds;
    AssetPivot m_pivot;
    std::vector<AssetRevisionRef> m_provenance;
    AssetRevisionRef m_rights;
    bool m_manual_ancestor {};
};

// Independently chosen outside the definition; never learned from its sidecars.
struct AssetProvenanceSelection {
    AssetRevisionRef m_product;
    AssetRegionSelection m_region;
};

struct AssetProvenanceCoverage {
    AssetRevisionRef m_product;
    std::array<std::uint64_t, 5> m_coverage {};
    std::uint64_t m_manual_ancestor_elements {};
};

// Immutable metadata admission, not rights, measured-scale or PBR qualification.
class AdmittedAssetDefinition {
  public:
    AdmittedAssetDefinition(const AdmittedAssetDefinition&) = default;
    AdmittedAssetDefinition& operator=(const AdmittedAssetDefinition&) = default;
    const AssetDefinition& definition() const;
    const AssetDefinitionRef& reference() const;
    const std::string& receipt_sha256() const;
    const std::string& canonical_json() const;
    const std::vector<AssetProvenanceCoverage>& provenance() const;

  private:
    struct Implementation;
    explicit AdmittedAssetDefinition(std::shared_ptr<const Implementation> implementation);
    std::shared_ptr<const Implementation> m_impl;
    friend Result<AdmittedAssetDefinition>
    admit_asset_definition(const AssetDefinition&,
                           const AdmittedAssetReceipt&,
                           const std::vector<AssetProvenanceSelection>&,
                           const AssetDependencyContext&,
                           const std::stop_token&);
};

// Typed v1 input -> checked immutable metadata and canonical JSON/hash. No JSON
// decoder, catalog publication, payload interpretation or runtime scene update.
Result<AdmittedAssetDefinition>
admit_asset_definition(const AssetDefinition& definition,
                       const AdmittedAssetReceipt& receipt,
                       const std::vector<AssetProvenanceSelection>& provenance,
                       const AssetDependencyContext& expected,
                       const std::stop_token& stop = {});

}  // namespace world_explorer
