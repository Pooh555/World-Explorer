#pragma once

#include "world_explorer/asset_catalog/draft_mutations.hpp"

namespace world_explorer {

struct DraftMetadataAttempt {
    OperationId m_attempt;
    std::uint64_t m_base_generation {};
    std::filesystem::path m_directory;
};

enum class DraftAttemptPhase : std::uint8_t { PENDING, COMMITTED, FAILED, CANCELLED };
enum class DraftAttemptProgress : std::uint8_t { INTENT_DURABLE, OUTCOME_RECORD_WRITTEN };
using DraftAttemptObserver = std::function<void(DraftAttemptProgress)>;

struct DraftAttemptSnapshot {
    DraftAttemptPhase m_phase;
    RecordCommit m_commit;
    std::string m_intent_sha256, m_mutation_intent_sha256, m_mutation_result_sha256;
    std::optional<ErrorCode> m_error;
};

// A separate, disjoint owned journal. At most 512 serialized two-phase attempts;
// positive attempt <=2^63-1 reserves core operations 2*n-1,2*n; even base <=1022.
// A returned FAILED/CANCELLED value means the terminal outcome was journaled,
// not successful mutation. An outer error means no terminal outcome is assured.
// Finalization uses a fresh token after started work observes cancellation.
Result<DraftAttemptSnapshot>
run_draft_metadata_attempt(const DraftMetadataAttempt& attempt,
                           const DraftMetadataMutation& mutation,
                           const AdmittedAssetReceipt& before,
                           const AdmittedAssetReceipt& after,
                           const AdmittedManualActionJournal& journal,
                           const std::filesystem::path& draft_directory,
                           const AssetDependencyContext& expected,
                           const std::stop_token& stop = {},
                           const DraftMutationObserver& mutation_observed = {},
                           const DraftAttemptObserver& observed = {});

// Historical outcome only, not a current draft commit or typed adoption token.
Result<DraftAttemptSnapshot>
read_draft_metadata_attempt(const DraftMetadataAttempt& attempt,
                            const DraftMetadataMutation& mutation,
                            const AdmittedAssetReceipt& before,
                            const AdmittedAssetReceipt& after,
                            const AdmittedManualActionJournal& journal,
                            const std::filesystem::path& draft_directory,
                            const RecordCommit& selected,
                            const AssetDependencyContext& expected,
                            const std::stop_token& stop = {});

}  // namespace world_explorer
