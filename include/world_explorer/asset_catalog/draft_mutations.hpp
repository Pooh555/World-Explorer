#pragma once

#include "world_explorer/asset_catalog/manual_actions.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace world_explorer {

struct DraftMetadataMutation {
    ManualActionId m_action;
    ManualActionKind m_kind;
    OperationId m_operation;
    std::uint64_t m_base_generation {};
    std::vector<AssetRevisionRef> m_targets;
    // Absent retains byte-identical v1; present uses explicit v2 intent.
    std::optional<std::string> m_admission_sha256 {};
};

struct DraftMetadataChange {
    AssetRevisionRef m_before, m_after;
};

enum class DraftMutationPhase : std::uint8_t { PREPARED, COMMITTED };
enum class DraftMutationProgress : std::uint8_t { INTENT_DURABLE, RESULT_RECORD_WRITTEN };
using DraftMutationObserver = std::function<void(DraftMutationProgress, std::size_t)>;

struct DraftMutationSnapshot {
    DraftMutationPhase m_phase;
    RecordCommit m_commit;
    std::string m_intent_sha256, m_result_sha256;
    std::vector<DraftMetadataChange> m_changes;
    std::map<AssetProductId, std::vector<std::byte>> m_output_bytes;
};

// Draft byte replacement only: EDIT_MASK/EDIT_GEOMETRY/EDIT_MATERIAL, same closed
// product IDs/dependency IDs/recipes, exact revision +1 and changed bytes for every
// target and transitive descendant (<=16 products, <=64 KiB/product). Unaffected
// nodes remain identical. Root action hashes and all affected manual ancestry
// are mandatory. Does not interpret or qualify mask/mesh/material payloads.
// One serialized owner directory: even base, at most 512 two-phase mutations,
// further bounded by the registry's 4096 keys/64 MiB. Positive operation <=2^63-1
// reserves core operation IDs 2*n-1 and 2*n exclusively in this directory.
Result<RecordCommit> publish_draft_metadata_mutation(const DraftMetadataMutation& mutation,
                                                     const AdmittedAssetReceipt& before,
                                                     const AdmittedAssetReceipt& after,
                                                     const AdmittedManualActionJournal& journal,
                                                     const std::filesystem::path& directory,
                                                     const AssetDependencyContext& expected,
                                                     const std::stop_token& stop = {},
                                                     const DraftMutationObserver& observed = {});

// Independent current commit pin and original selected inputs; validates the
// owned serial history and this exact mutation. PREPARED exposes no outputs;
// COMMITTED owns bytes after source removal. No runtime execution/adoption token.
Result<DraftMutationSnapshot>
read_draft_metadata_mutation(const DraftMetadataMutation& mutation,
                             const AdmittedAssetReceipt& before,
                             const AdmittedAssetReceipt& after,
                             const AdmittedManualActionJournal& journal,
                             const std::filesystem::path& directory,
                             const RecordCommit& selected,
                             const AssetDependencyContext& expected,
                             const std::stop_token& stop = {});

}  // namespace world_explorer
