#ifndef WORLD_EXPLORER_APPLICATION_POSE_EXPORT_HPP
#define WORLD_EXPLORER_APPLICATION_POSE_EXPORT_HPP
#include "world_explorer/application/pose_replay.hpp"
#include "world_explorer/map_store/pose_asset.hpp"

namespace world_explorer {

struct PoseExportRequest {
    PoseReplayRequest m_completed;
    std::filesystem::path m_destination;
};

// Verifies the retained job and source, imports poses and publishes canonical
// interchange. No solver, pixel decoding or optimizer executes.
Result<std::string> export_completed_pose_asset(const PoseExportRequest& request,
                                                const std::stop_token& stop,
                                                const LogCallback& log = {});

}  // namespace world_explorer
#endif
