#pragma once

#include "world_explorer/application/pose_replay.hpp"
#include "world_explorer/map_store/pose_asset.hpp"

namespace world_explorer {

struct PosePreparationRequest {
    PoseReplayRequest m_completed;
    std::filesystem::path m_map_directory;
};

struct PreparedPoseMap {
    // Holds the exclusive persistent writer for the dependent operation's lifetime.
    std::unique_ptr<MapStore> m_store;
    PoseAssetProvenance m_provenance;
    std::string m_asset_sha256;
};

// Checked retained outputs -> canonical asset -> durable initial map. Reopening
// retries operation 1 and requires the identical epoch/revision/dependency.
// No solver, pixel decoding or optimizer runs; no existing job is modified.
Result<PreparedPoseMap> prepare_completed_pose_map(const PosePreparationRequest& request,
                                                   const std::stop_token& stop,
                                                   const LogCallback& log = {});

}  // namespace world_explorer
