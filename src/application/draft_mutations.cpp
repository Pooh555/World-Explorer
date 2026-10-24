#include "world_explorer/application/draft_mutations.hpp"

namespace world_explorer {

Result<RecordCommit> replace_draft_metadata(const ReplaceDraftMetadataCommand& command,
                                            const AdmittedAssetReceipt& before,
                                            const AdmittedAssetReceipt& after,
                                            const AdmittedManualActionJournal& journal,
                                            const AssetDependencyContext& expected,
                                            const std::stop_token& stop,
                                            const DraftMutationObserver& observed) {
    return publish_draft_metadata_mutation(
        command.m_mutation, before, after, journal, command.m_directory, expected, stop, observed);
}

}  // namespace world_explorer
