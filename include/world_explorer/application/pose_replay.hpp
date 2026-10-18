#ifndef WORLD_EXPLORER_APPLICATION_POSE_REPLAY_HPP
#define WORLD_EXPLORER_APPLICATION_POSE_REPLAY_HPP
#include "world_explorer/application/commands.hpp"
#include "world_explorer/map_store/map_store.hpp"

namespace world_explorer {

struct PoseReplayRequest {
    std::filesystem::path m_pose_job, m_video, m_decode_stage;
};

// Shared inspection command: checked completed poses + decoded-frame leases ->
// bounded estimator replay -> one atomic immutable map snapshot. No fresh solve,
// fitting, disk publication or changes to the existing job/scene.
Result<MapSnapshot> replay_completed_pose_map(const PoseReplayRequest& request,
                                              const std::stop_token& stop,
                                              const LogCallback& log = {});

}  // namespace world_explorer
#endif
