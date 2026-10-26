#pragma once

#include "world_explorer/application/geometry_product.hpp"
#include "world_explorer/asset_catalog/asset_definition.hpp"

#include <map>

namespace world_explorer {

enum class MaterialChannelRole : std::uint8_t {
    CAPTURED_RGB,
    BASE_COLOR,
    METALLIC,
    ROUGHNESS,
    NORMAL,
    OCCLUSION,
    EMISSION
};
enum class MaterialFitRole : std::uint8_t { LIGHT, EXPOSURE, WHITE_BALANCE };
enum class MaterialFilter : std::uint8_t { NEAREST, LINEAR };
enum class MaterialWrap : std::uint8_t { CLAMP_TO_EDGE, REPEAT };

struct MaterialChannelSelection {
    AssetRevisionRef m_product, m_provenance;
    AssetRegionSelection m_region;
};

struct MaterialFitSelection {
    MaterialFitRole m_role;
    AssetRevisionRef m_product;
    std::vector<ObjectViewSelection> m_views;
};

struct MaterialProductSelection {
    AssetRevisionRef m_product;
    AssetMaterialVariantId m_variant;
    AssetLightingMode m_lighting;
    std::uint32_t m_width {}, m_height {};
    MaterialFilter m_filter;
    MaterialWrap m_wrap;
    std::map<MaterialChannelRole, MaterialChannelSelection> m_channels;
    std::vector<MaterialFitSelection> m_fit_inputs;
};

struct MaterialChannel {
    std::uint32_t m_components {};
    // Row-major little-endian float32 source, normalized into owned native floats.
    std::vector<float> m_values;
    AssetRegionInspection m_provenance;
};

struct MaterialFitInput {
    MaterialFitSelection m_selection;
    // LIGHT / WHITE_BALANCE: three linear RGB values; EXPOSURE: one gain.
    std::vector<double> m_values;
};

struct MaterialProductRequest {
    std::optional<MaterialProductSelection> m_selection;
    std::optional<AdmittedGeometryProduct> m_geometry;
    std::optional<AdmittedCaptureSelection> m_capture;
    std::optional<AdmittedAssetReceipt> m_receipt;
    AssetDependencyContext m_expected;
};

// Owned material payload/provenance admission, never fitted-light authenticity,
// physically valid albedo/relighting, a renderer/asset-adoption or AP pass token.
class AdmittedMaterialProduct {
  public:
    AdmittedMaterialProduct(const AdmittedMaterialProduct&) = default;
    AdmittedMaterialProduct& operator=(const AdmittedMaterialProduct&) = default;
    const MaterialProductSelection& selection() const;
    const AssetRevisionRef& geometry_product() const;
    const std::map<MaterialChannelRole, MaterialChannel>& channels() const;
    const std::vector<MaterialFitInput>& fit_inputs() const;
    const std::string& receipt_sha256() const;
    const AssetDependencyContext& context() const;
    bool synthetic_source() const;
    bool manual_ancestor() const;

  private:
    struct Implementation;
    explicit AdmittedMaterialProduct(std::shared_ptr<const Implementation> state);
    std::shared_ptr<const Implementation> m_state;
    friend Result<AdmittedMaterialProduct> inspect_material_product(const MaterialProductRequest&,
                                                                    const std::stop_token&);
    friend Result<RecordCommit> publish_material_product(const AdmittedMaterialProduct&,
                                                         const AdmittedAssetReceipt&,
                                                         const std::filesystem::path&,
                                                         OperationId,
                                                         std::uint64_t,
                                                         const AssetDependencyContext&,
                                                         const std::stop_token&,
                                                         const std::function<void(std::size_t)>&);
};

// Root/fit records <=64 KiB each; grid 1..256 per axis; six PBR channels or one
// captured RGB channel. WEMATX1 binary channels <=786496 bytes each, provenance
// <=256 KiB each. Complete owned publication batch <=8 MiB.
Result<AdmittedMaterialProduct> inspect_material_product(const MaterialProductRequest& request,
                                                         const std::stop_token& stop = {});
Result<RecordCommit>
publish_material_product(const AdmittedMaterialProduct& material,
                         const AdmittedAssetReceipt& receipt,
                         const std::filesystem::path& directory,
                         OperationId operation,
                         std::uint64_t base_generation,
                         const AssetDependencyContext& expected,
                         const std::stop_token& stop = {},
                         const std::function<void(std::size_t)>& observed = {});
Result<AdmittedMaterialProduct> read_material_product(const std::filesystem::path& directory,
                                                      const RecordCommit& commit,
                                                      const MaterialProductRequest& request,
                                                      const std::stop_token& stop = {});

}  // namespace world_explorer
