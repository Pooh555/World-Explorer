#ifndef WORLD_EXPLORER_APPLICATION_POSE_PREPARATION_HPP
#define WORLD_EXPLORER_APPLICATION_POSE_PREPARATION_HPP
#include "world_explorer/application/pose_replay.hpp"
#include "world_explorer/map_store/pose_asset.hpp"

namespace world_explorer {
struct pose_preparation_request {
    pose_replay_request completed;
    std::filesystem::path map_directory;
};
struct prepared_pose_map {
    // Holds the exclusive persistent writer for the dependent operation's lifetime.
    std::unique_ptr<map_store> store;
    pose_asset_provenance provenance;
    std::string asset_sha256;
};
// Checked retained outputs -> canonical asset -> durable initial map. Reopening
// retries operation 1 and requires the identical epoch/revision/dependency.
// No solver, pixel decoding or optimizer runs; no existing job is modified.
result<prepared_pose_map> prepare_completed_pose_map(const pose_preparation_request& request,
                                                     const std::stop_token& stop,
                                                     const log_callback& log = {});
} // namespace world_explorer
#endif
