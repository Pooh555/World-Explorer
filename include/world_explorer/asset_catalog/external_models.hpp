#pragma once

#include "world_explorer/asset_catalog/component_evidence.hpp"

namespace world_explorer {

struct AssetExternalModelFile {
    std::string m_relative_path, m_sha256;
    std::uint64_t m_bytes {};
    std::filesystem::path m_file;
};

struct AssetExternalModelSelection {
    std::string m_component_sha256;
    std::vector<AssetExternalModelFile> m_files;
};

struct AssetExternalModelLimits {
    // Independently selected, positive; <=4 GiB/file and <=8 GiB/pass.
    std::uint64_t m_file_bytes {}, m_total_bytes {};
};

struct AssetExternalModelComponent {
    std::string m_component_sha256, m_terms_sha256, m_review_sha256, m_reviewer;
    bool m_review_selected {};
    std::vector<AssetExternalModelFile> m_files;
};

struct AssetExternalModelObservation {
    std::uint32_t m_schema_version { 2 };
    AssetRecipeInspection m_recipe;
    AssetExternalModelLimits m_limits;
    std::string m_evidence_receipt_sha256;
    std::vector<AssetExternalModelComponent> m_models;
    std::uint64_t m_observed_bytes {}, m_hashed_bytes {};
};

struct AssetExternalModelProgress {
    std::size_t m_pass {}, m_file_index {};
    std::uint64_t m_hashed_bytes {};
};

// Schema-2 EXTERNAL_MODEL_FILES manifests remain in the small owned receipt;
// external payloads do not. Exactly all MODEL components, no archives, <=16
// models /64 files each /256 files total, canonical absolute single-link files.
// Native paths <=4096 generic bytes /64 segments, with no embedded NUL.
// Two streamed passes plus metadata rechecks; cooperative filesystem only.
// Never clears global component/installation/rights/runtime/limit blockers.
// This observation owns no model bytes, lock, installer or execution authority.
// Progress runs after each checked file, outside hashing; exceptions fail the
// call. Caller callback effects are caller-owned and are not rolled back.
Result<AssetExternalModelObservation> inspect_asset_external_models(
    const AdmittedAssetReceipt& recipe_receipt,
    AssetProductId product,
    const AdmittedAssetReceipt& evidence_receipt,
    const AssetDependencyContext& expected,
    const AssetRecipeHost& host,
    const std::vector<AssetReviewSelection>& selected_reviews,
    const std::vector<AssetExternalModelSelection>& selected_models,
    const AssetExternalModelLimits& limits,
    const std::stop_token& stop = {},
    const std::function<void(const AssetExternalModelProgress&)>& progress = {});

}  // namespace world_explorer
