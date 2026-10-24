#pragma once

#include "world_explorer/asset_catalog/dependency_receipt.hpp"

#include <array>

namespace world_explorer {

enum class AssetEvidenceClass : std::uint8_t {
    UNKNOWN,
    OBSERVED,
    INFERRED,
    GENERATED,
    MANUALLY_EDITED
};
enum class AssetRegionKind : std::uint8_t { GEOMETRY_FACES, MATERIAL_TEXELS };

struct AssetRegionDomain {
    AssetRegionKind m_kind;
    std::uint64_t m_elements {};
    std::uint64_t m_width {}, m_height {};
    auto operator<=>(const AssetRegionDomain&) const = default;
};

struct AssetSourceViewId {
    CaptureId m_capture;
    FrameId m_frame;
    auto operator<=>(const AssetSourceViewId&) const = default;
};

// Independently selected metadata identities. Image bytes belong to an upstream
// product; matching these declarations does not validate calibration/pose math.
struct AssetSourceViewRef {
    AssetSourceViewId m_view;
    CalibrationId m_calibration;
    AssetRevisionRef m_image;
    std::string m_calibration_sha256, m_pose_sha256, m_image_grid_sha256;
    auto operator<=>(const AssetSourceViewRef&) const = default;
};

struct AssetRegionSpan {
    std::uint64_t m_first {}, m_count {};
    AssetEvidenceClass m_class;
    std::optional<double> m_confidence;
    std::vector<AssetRevisionRef> m_parents;
    std::vector<AssetSourceViewId> m_support;
    bool m_manual_ancestor {};
};

// Selected outside the sidecar: exact target/domain and complete source table.
struct AssetRegionSelection {
    AssetRevisionRef m_target;
    AssetRegionDomain m_domain;
    std::vector<AssetSourceViewRef> m_sources;
};

struct AssetRegionProvenance {
    AssetDependencyContext m_context;
    AssetRevisionRef m_target;
    std::string m_recipe_sha256;
    AssetRegionDomain m_domain;
    std::vector<AssetSourceViewRef> m_sources;
    std::vector<AssetRegionSpan> m_spans;
    bool m_manual_ancestor {};
};

struct AssetRegionInspection {
    AssetRegionProvenance m_record;
    std::string m_receipt_sha256, m_graph_sha256, m_provenance_sha256;
    // Indexed by AssetEvidenceClass; these are declared element counts, not area.
    std::array<std::uint64_t, 5> m_coverage {};
    std::uint64_t m_manual_ancestor_elements {};
};

// Strict v1 sidecar content from an owned receipt. Sidecar depends only on its
// target and declares no own manual action. <=4 MiB, <=16,384 contiguous spans,
// <=16,777,216 elements, <=64 views, <=65,536 parent/support references total.
// Validates declarations/ancestry, not real measurement, trusted actions or
// surface/texel correspondences. No writes, worker execution or gate approval.
Result<AssetRegionInspection>
inspect_asset_region_provenance(const AdmittedAssetReceipt& receipt,
                                AssetProductId provenance_product,
                                const AssetRegionSelection& selection,
                                const AssetDependencyContext& expected,
                                const std::stop_token& stop = {});

}  // namespace world_explorer
