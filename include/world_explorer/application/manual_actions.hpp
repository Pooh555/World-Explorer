#pragma once

#include "world_explorer/asset_catalog/manual_actions.hpp"

namespace world_explorer {

struct RecordManualActionCommand {
    ManualActionRequest m_request;
    std::filesystem::path m_journal;
    OperationId m_operation;
    std::uint64_t m_base_generation {};
};

// Shared native command boundary; records a declared action only. No output
// mutation, human authentication, evaluation-label transfer or worker launch.
Result<RecordCommit> record_manual_action(const RecordManualActionCommand& command,
                                          const AdmittedAssetReceipt& receipt,
                                          const AssetDependencyContext& expected,
                                          const std::stop_token& stop = {},
                                          const std::function<void(std::size_t)>& observed = {});

}  // namespace world_explorer
