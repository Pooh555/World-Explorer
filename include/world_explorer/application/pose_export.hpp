#ifndef WORLD_EXPLORER_APPLICATION_POSE_EXPORT_HPP
#define WORLD_EXPLORER_APPLICATION_POSE_EXPORT_HPP
#include "world_explorer/application/pose_replay.hpp"
#include "world_explorer/map_store/pose_asset.hpp"

namespace world_explorer {
struct pose_export_request {
    pose_replay_request completed;
    std::filesystem::path destination;
};
// Verifies the retained job and source, imports poses and publishes canonical
// interchange. No solver, pixel decoding or optimizer executes.
result<std::string> export_completed_pose_asset(const pose_export_request& request,
                                                const std::stop_token& stop,
                                                const log_callback& log = {});
} // namespace world_explorer
#endif
