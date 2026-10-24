#pragma once

#include "world_explorer/asset_catalog/dependency_receipt.hpp"

namespace world_explorer {

enum class AssetRecipeRole : std::uint8_t { CODE, ENVIRONMENT, MODEL, INPUT };
enum class AssetRecipeRights : std::uint8_t { UNREVIEWED, DECLARED_PERMITTED, RESTRICTED };
enum class AssetRecipeDevice : std::uint8_t { CPU, CUDA };
enum class AssetRecipeBlocker : std::uint8_t {
    CONTRACTS_ONLY_HOST,
    UNKNOWN_INSTALLATION_SIZE,
    UNREVIEWED_LOCAL_RIGHTS,
    RESTRICTED_LOCAL_RIGHTS,
    DISTRIBUTION_NOT_CLEARED,
    HOST_CAPACITY_EXCEEDED,
    COMPONENT_BYTES_UNVERIFIED,
    INSTALLED_FILES_UNVERIFIED,
    REVIEW_NOT_SELECTED,
    RUNTIME_CLOSURE_UNVERIFIED,
    WORKER_LIMITS_UNVERIFIED
};

struct AssetRecipeComponent {
    AssetRecipeRole m_role;
    std::string m_sha256;
    std::optional<std::uint64_t> m_archive_bytes;
    std::optional<std::uint64_t> m_installed_bytes;
    std::optional<std::string> m_terms_sha256;
    std::optional<std::string> m_review_sha256;
    AssetRecipeRights m_local_use;
    AssetRecipeRights m_distribution;
};

struct AssetRecipeBudget {
    AssetRecipeDevice m_device;
    std::uint64_t m_host_ram_bytes {};
    std::uint64_t m_device_vram_bytes {};
    std::uint64_t m_scratch_bytes {};
    std::uint64_t m_wall_time_ms {};
    std::uint64_t m_cpu_threads {};
    std::uint64_t m_queued_jobs {};
    std::uint64_t m_batch_size {};
    std::uint64_t m_candidate_limit {};
    std::uint64_t m_width {};
    std::uint64_t m_height {};
};

// Caller-observed capacities, not device qualification. Zero means unavailable.
struct AssetRecipeHost {
    bool m_contracts_only { true };
    std::uint64_t m_host_ram_bytes {};
    std::uint64_t m_device_vram_bytes {};
    std::uint64_t m_free_disk_bytes {};
    std::uint64_t m_cpu_threads {};
    std::uint64_t m_queued_jobs {};
};

// Owned contract inspection; no provisioning/execution/rights approval token.
// Component payloads/installed inventories are declarations, not verified here.
struct AssetRecipeInspection {
    AssetProductId m_product;
    AssetDependencyContext m_context;
    std::string m_receipt_sha256;
    std::string m_graph_sha256;
    std::string m_recipe_sha256;
    std::string m_stage;
    std::string m_configuration_sha256;
    std::string m_prompt_sha256;
    std::uint64_t m_seed {};
    AssetRecipeBudget m_budget;
    std::vector<AssetRecipeComponent> m_components;
    std::optional<std::uint64_t> m_installation_bytes;
    std::optional<std::uint64_t> m_reserved_disk_bytes;
    std::vector<AssetRecipeBlocker> m_blockers;
};

// Exact recipe/context/dependency binding from an already owned receipt snapshot.
// <=64 KiB strict JSON, <=64 components (one code/environment, >=1 input,
// <=16 models). Missing costs and rights remain explicit blockers, never guesses.
Result<AssetRecipeInspection> inspect_asset_recipe(const AdmittedAssetReceipt& receipt,
                                                   AssetProductId product,
                                                   const AssetDependencyContext& expected,
                                                   const AssetRecipeHost& host,
                                                   const std::stop_token& stop = {});

}  // namespace world_explorer
