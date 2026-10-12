#ifndef WORLD_EXPLORER_APPLICATION_POSE_PIPELINE_HPP
#define WORLD_EXPLORER_APPLICATION_POSE_PIPELINE_HPP
#include "world_explorer/application/pose_preparation.hpp"
#include "world_explorer/reconstruction_adapters/completed_poses.hpp"

namespace world_explorer::pose_pipeline {
result<std::vector<pose_event>> consume(frame_source& source, pose_estimator& estimator,
                                        const std::stop_token& stop);
result<map_snapshot> publish(const completed_pose_dataset& dataset,
                             const std::vector<pose_event>& events,
                             const std::stop_token& stop = {});
result<canonical_pose_asset> canonical(const completed_pose_dataset& dataset,
                                       const std::stop_token& stop);
result<prepared_pose_map> persist(const canonical_pose_asset& asset,
                                  const std::filesystem::path& directory,
                                  const std::stop_token& stop);
} // namespace world_explorer::pose_pipeline
#endif
