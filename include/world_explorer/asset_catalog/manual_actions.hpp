#pragma once

#include "world_explorer/asset_catalog/dependency_receipt.hpp"
#include "world_explorer/core/immutable_records.hpp"

namespace world_explorer {

enum class ManualActionKind : std::uint8_t {
    SELECT_INPUT,
    SELECT_RECIPE,
    CAPTURE_MEASUREMENT,
    ANNOTATE_EVALUATION,
    SELECT_OBJECT,
    EDIT_MASK,
    SELECT_CANDIDATE,
    ALIGN_OUTPUT,
    EDIT_GEOMETRY,
    EDIT_MATERIAL,
    APPROVE_OUTPUT
};

enum class ManualActionScope : std::uint8_t { SETUP, EVALUATION_ONLY, OUTPUT_ASSISTANCE };

// Caller-declared actor/time/detail, not authenticated identity or measured labor.
// Scope is derived from kind; declared edit kinds cannot select setup scope.
struct ManualActionRequest {
    ManualActionId m_action;
    ManualActionKind m_kind;
    std::string m_actor;
    std::uint64_t m_started_unix_ms {};
    std::uint64_t m_elapsed_ms {};
    std::uint64_t m_active_ms {};
    std::vector<AssetRevisionRef> m_targets;
    std::string m_detail;
};

struct ManualActionRecord {
    ManualActionRequest m_request;
    ManualActionScope m_scope;
    OperationId m_operation;
    std::uint64_t m_base_generation {};
    AssetDependencyContext m_context;
    std::string m_receipt_sha256;
    std::string m_graph_sha256;
    std::string m_sha256;
};

struct ManualActionAccounting {
    std::uint64_t m_setup_active_ms {};
    std::uint64_t m_evaluation_active_ms {};
    std::uint64_t m_assistance_active_ms {};
    std::size_t m_setup_count {};
    std::size_t m_evaluation_count {};
    std::size_t m_assistance_count {};
};

class AdmittedManualActionJournal {
  public:
    const RecordCommit& commit() const;
    const std::filesystem::path& source_directory() const;
    const std::vector<ManualActionRecord>& records() const;
    const ManualActionAccounting& accounting() const;

  private:
    struct Implementation;
    explicit AdmittedManualActionJournal(std::shared_ptr<const Implementation> state);
    std::shared_ptr<const Implementation> m_state;
    friend Result<AdmittedManualActionJournal> read_manual_action_journal(
        const std::filesystem::path&, const RecordCommit&, const std::stop_token&);
};

// <=1024 actions/journal, one action/operation/generation; <=16 exact targets,
// <=64-byte actor token, <=4096-byte UTF-8 detail, <=16 KiB canonical record.
// Elapsed <=7 days, active <=elapsed; timestamps/time/identity are declarations.
// Owned immutable intent/blob publication, manifest last; exact retries only.
Result<RecordCommit> append_manual_action(const ManualActionRequest& request,
                                          const AdmittedAssetReceipt& receipt,
                                          const std::filesystem::path& directory,
                                          OperationId operation,
                                          std::uint64_t base_generation,
                                          const AssetDependencyContext& expected,
                                          const std::stop_token& stop = {},
                                          const std::function<void(std::size_t)>& observed = {});

// Independent commit pin; owns all decoded records after source deletion.
Result<AdmittedManualActionJournal>
read_manual_action_journal(const std::filesystem::path& directory,
                           const RecordCommit& selected,
                           const std::stop_token& stop = {});

// Only output-assistance records can supply inference manual ancestry. Exact
// original receipt/context/target required. This does not authorize a scene edit,
// prove action completion/authenticity, or classify a run as automatic success.
Result<std::string> inference_manual_action(const AdmittedManualActionJournal& journal,
                                            ManualActionId action,
                                            const AdmittedAssetReceipt& receipt,
                                            const AssetRevisionRef& target,
                                            const AssetDependencyContext& expected,
                                            const std::stop_token& stop = {});

}  // namespace world_explorer
