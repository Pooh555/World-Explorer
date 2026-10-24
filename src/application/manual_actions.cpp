#include "world_explorer/application/manual_actions.hpp"

namespace world_explorer {

Result<RecordCommit> record_manual_action(const RecordManualActionCommand& command,
                                          const AdmittedAssetReceipt& receipt,
                                          const AssetDependencyContext& expected,
                                          const std::stop_token& stop,
                                          const std::function<void(std::size_t)>& observed) {
    return append_manual_action(command.m_request,
                                receipt,
                                command.m_journal,
                                command.m_operation,
                                command.m_base_generation,
                                expected,
                                stop,
                                observed);
}

}  // namespace world_explorer
