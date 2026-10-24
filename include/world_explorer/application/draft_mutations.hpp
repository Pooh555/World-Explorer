#pragma once

#include "world_explorer/asset_catalog/draft_mutations.hpp"

namespace world_explorer {

struct ReplaceDraftMetadataCommand {
    DraftMetadataMutation m_mutation;
    std::filesystem::path m_directory;
};

// Shared application command; owned draft publication, no scene mutation/worker.
Result<RecordCommit> replace_draft_metadata(const ReplaceDraftMetadataCommand& command,
                                            const AdmittedAssetReceipt& before,
                                            const AdmittedAssetReceipt& after,
                                            const AdmittedManualActionJournal& journal,
                                            const AssetDependencyContext& expected,
                                            const std::stop_token& stop = {},
                                            const DraftMutationObserver& observed = {});

}  // namespace world_explorer
