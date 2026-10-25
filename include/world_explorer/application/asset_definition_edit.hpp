#pragma once

#include "world_explorer/application/draft_mutations.hpp"
#include "world_explorer/asset_catalog/definition_io.hpp"
#include "world_explorer/asset_catalog/static_payloads.hpp"

namespace world_explorer {

struct AssetDefinitionEditSelection {
    AssetDefinitionRef m_definition;
    std::vector<std::byte> m_bytes;
    std::vector<AssetProvenanceSelection> m_provenance;
};

struct EditAssetDefinitionCommand {
    ReplaceDraftMetadataCommand m_draft;
    AssetDefinitionEditSelection m_before, m_after;
};

class AdmittedAssetEdit {
  public:
    AdmittedAssetEdit(const AdmittedAssetEdit&) = default;
    AdmittedAssetEdit& operator=(const AdmittedAssetEdit&) = default;
    const AdmittedAssetDefinition& definition() const;
    const StaticAssetPayloads& payloads() const;
    const RecordCommit& commit() const;
    const std::string& result_sha256() const;

  private:
    struct Implementation;
    explicit AdmittedAssetEdit(std::shared_ptr<const Implementation> state);
    std::shared_ptr<const Implementation> m_state;
    friend Result<AdmittedAssetEdit> read_asset_definition_edit(const EditAssetDefinitionCommand&,
                                                                const AdmittedAssetReceipt&,
                                                                const AdmittedAssetReceipt&,
                                                                const AdmittedManualActionJournal&,
                                                                const RecordCommit&,
                                                                const AssetDependencyContext&,
                                                                const std::stop_token&);
};

// Both definitions, regions and actual supported payloads are admitted before
// any journal write. Successful attempt status is historical draft publication;
// typed output admission still requires the independent current draft commit.
Result<DraftAttemptSnapshot>
attempt_asset_definition_edit(const EditAssetDefinitionCommand& command,
                              const DraftMetadataAttempt& attempt,
                              const AdmittedAssetReceipt& before,
                              const AdmittedAssetReceipt& after,
                              const AdmittedManualActionJournal& journal,
                              const AssetDependencyContext& expected,
                              const std::stop_token& stop = {},
                              const DraftMutationObserver& mutation_observed = {},
                              const DraftAttemptObserver& observed = {});

Result<AdmittedAssetEdit> read_asset_definition_edit(const EditAssetDefinitionCommand& command,
                                                     const AdmittedAssetReceipt& before,
                                                     const AdmittedAssetReceipt& after,
                                                     const AdmittedManualActionJournal& journal,
                                                     const RecordCommit& selected,
                                                     const AssetDependencyContext& expected,
                                                     const std::stop_token& stop = {});

}  // namespace world_explorer
