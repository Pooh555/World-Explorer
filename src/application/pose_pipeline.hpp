#ifndef WORLD_EXPLORER_APPLICATION_POSE_PIPELINE_HPP
#define WORLD_EXPLORER_APPLICATION_POSE_PIPELINE_HPP
#include "world_explorer/application/pose_preparation.hpp"
#include "world_explorer/reconstruction_adapters/completed_poses.hpp"

namespace world_explorer::pose_pipeline {

Result<std::vector<PoseEvent>>
consume(FrameSource& source, PoseEstimator& estimator, const std::stop_token& stop);
Result<MapSnapshot> publish(const CompletedPoseDataset& dataset,
                            const std::vector<PoseEvent>& events,
                            const std::stop_token& stop = {});
Result<CanonicalPoseAsset> canonical(const CompletedPoseDataset& dataset,
                                     const std::stop_token& stop);
Result<PreparedPoseMap> persist(const CanonicalPoseAsset& asset,
                                const std::filesystem::path& directory,
                                const std::stop_token& stop);

}  // namespace world_explorer::pose_pipeline
#endif
