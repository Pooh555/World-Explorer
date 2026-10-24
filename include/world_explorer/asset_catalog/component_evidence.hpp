#pragma once

#include "world_explorer/asset_catalog/recipe.hpp"

namespace world_explorer {

// Selected by the caller outside the evidence receipt, never learned from it.
// This binds a review record/reviewer, not a legal or execution authorization.
struct AssetReviewSelection {
    AssetRecipeRole m_role;
    std::string m_component_sha256;
    std::string m_review_sha256;
    std::string m_reviewer;
};

struct AssetComponentEvidence {
    AssetRecipeRole m_role;
    std::string m_component_sha256;
    std::size_t m_file_count {};
    std::uint64_t m_archive_bytes {};
    std::uint64_t m_installed_bytes {};
    bool m_review_selected {};
};

// Owned diagnostic result; never an installation or worker-execution token.
struct AssetRecipeEvidenceInspection {
    AssetRecipeInspection m_recipe;
    std::string m_evidence_receipt_sha256;
    std::vector<AssetComponentEvidence> m_components;
};

// Both receipts must have been independently selected and admitted. Component
// manifests <=256 KiB, reviews/config/prompt/terms <=64 KiB; <=1024 files/component,
// <=4096 file entries total. All payloads fit the existing receipt byte limits.
// Installed filesystem contents are not checked. Missing/partial/unknown
// evidence rejects without changing either snapshot or any source files.
Result<AssetRecipeEvidenceInspection>
inspect_asset_recipe_evidence(const AdmittedAssetReceipt& recipe_receipt,
                              AssetProductId product,
                              const AdmittedAssetReceipt& evidence_receipt,
                              const AssetDependencyContext& expected,
                              const AssetRecipeHost& host,
                              const std::vector<AssetReviewSelection>& selected_reviews,
                              const std::stop_token& stop = {});

}  // namespace world_explorer
