#pragma once

#include "world_explorer/asset_catalog/component_evidence.hpp"

namespace world_explorer {

struct AssetComponentRoot {
    AssetRecipeRole m_role;
    std::string m_component_sha256;
    std::filesystem::path m_directory;
};

struct AssetInstalledInspection {
    AssetRecipeEvidenceInspection m_evidence;
    std::vector<AssetComponentRoot> m_roots;
    std::uint64_t m_installed_bytes {};
    std::size_t m_file_count {};
};

// Read-only observation, not an execution token. Revalidates snapshot evidence;
// requires exactly one explicit absolute, disjoint root per CODE/ENVIRONMENT/MODEL.
// Closed portable regular-file trees only, no links or extra entries. Two full
// scans: <=64 MiB declared/hash budget per pass, <=8192 entries/pass,
// <=16 relative path segments. Growth detection may read one extra 64 KiB chunk.
// Concurrent hostile path replacement is outside this cooperative-filesystem
// contract. Recheck before use; no lock/import/install/network/write occurs.
Result<AssetInstalledInspection>
inspect_asset_installed_components(const AdmittedAssetReceipt& recipe_receipt,
                                   AssetProductId product,
                                   const AdmittedAssetReceipt& evidence_receipt,
                                   const AssetDependencyContext& expected,
                                   const AssetRecipeHost& host,
                                   const std::vector<AssetReviewSelection>& selected_reviews,
                                   const std::vector<AssetComponentRoot>& selected_roots,
                                   const std::stop_token& stop = {});

}  // namespace world_explorer
