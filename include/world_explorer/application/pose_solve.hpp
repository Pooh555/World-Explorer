#ifndef WORLD_EXPLORER_APPLICATION_POSE_SOLVE_HPP
#define WORLD_EXPLORER_APPLICATION_POSE_SOLVE_HPP
#include "world_explorer/application/commands.hpp"
#include "world_explorer/map_store/map_store.hpp"
#include "world_explorer/reconstruction_adapters/batch_poses.hpp"

namespace world_explorer {
struct pose_solve_request {
    std::filesystem::path video;
    batch_pose_config batch;
};
// Selected-frame native admission -> isolated CPU batch solve -> checked map.
// Source digest is computed here; output must be fresh. Publishes canonical
// poses and an initial durable map at sibling NEW_JOB.map. No appearance fitting.
result<map_snapshot> solve_selected_pose_map(pose_solve_request request,
                                             const std::stop_token& stop,
                                             const log_callback& log = {});
} // namespace world_explorer
#endif
