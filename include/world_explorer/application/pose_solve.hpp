#pragma once

#include "world_explorer/application/commands.hpp"
#include "world_explorer/map_store/map_store.hpp"
#include "world_explorer/reconstruction_adapters/batch_poses.hpp"

namespace world_explorer {

struct PoseSolveRequest {
    std::filesystem::path m_video;
    BatchPoseConfig m_batch;
};

// Selected-frame native admission -> isolated CPU batch solve -> checked map.
// Source digest is computed here; matching owned batch inputs may explicitly
// resume. The sibling map output must be fresh. Publishes canonical
// poses and an initial durable map at sibling NEW_JOB.map. No appearance fitting.
Result<MapSnapshot> solve_selected_pose_map(PoseSolveRequest request,
                                            const std::stop_token& stop,
                                            const LogCallback& log = {});

}  // namespace world_explorer
