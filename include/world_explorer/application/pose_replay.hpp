#ifndef WORLD_EXPLORER_APPLICATION_POSE_REPLAY_HPP
#define WORLD_EXPLORER_APPLICATION_POSE_REPLAY_HPP
#include "world_explorer/application/commands.hpp"
#include "world_explorer/map_store/map_store.hpp"

namespace world_explorer {
struct pose_replay_request {
    std::filesystem::path pose_job, video, decode_stage;
};
// Shared inspection command: checked completed poses + decoded-frame leases ->
// bounded estimator replay -> one atomic immutable map snapshot. No fresh solve,
// fitting, disk publication or changes to the existing job/scene.
result<map_snapshot> replay_completed_pose_map(const pose_replay_request& request,
                                               const std::stop_token& stop,
                                               const log_callback& log = {});
} // namespace world_explorer
#endif
