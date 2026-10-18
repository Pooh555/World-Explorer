#ifndef WORLD_EXPLORER_RECONSTRUCTION_ADAPTERS_COMPLETED_POSES_HPP
#define WORLD_EXPLORER_RECONSTRUCTION_ADAPTERS_COMPLETED_POSES_HPP
#include "world_explorer/reconstruction_api/map_records.hpp"
#include "world_explorer/reconstruction_api/pose_estimator.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace world_explorer {

struct CompletedPoseConfig {
    std::filesystem::path m_pose_job;
    std::string m_source_sha256;
    // Optional explicit immutable stage when the mutable decode pointer no
    // longer identifies these completed poses. Never search/fallback silently.
    std::filesystem::path m_decode_stage {};
};

struct CompletedPoseDataset {
    WorldEpoch m_epoch;
    MapRevisionId m_revision;
    SubmapId m_submap;
    ScaleStatus m_scale { ScaleStatus::ARBITRARY };
    std::string m_source_sha256, m_pose_completion_sha256, m_decode_completion_sha256;
    std::filesystem::path m_decode_directory;
    std::vector<CalibrationRecord> m_calibrations;
    std::vector<ObservationRecord> m_observations;
    std::vector<PoseEstimate> m_poses;
    // Adapter bridge to the undistorted training inventory; names never enter
    // canonical map records or runtime consumers.
    std::map<std::string, FrameId> m_training_frames;
    std::optional<std::string> m_journal_sha256;
};

// Checked import of the legacy COLMAP canonical JSON v1 profile. This does not
// execute a solver. Unknown camera/coordinate profiles fail explicitly.
Result<std::shared_ptr<const CompletedPoseDataset>>
load_completed_poses(const CompletedPoseConfig& config, const std::stop_token& stop = {});
// Batch replay of a checked completed dataset, never a fresh pose solve. Pixels
// are admitted by immutable frame identity/metadata; dataset owns encoded-image
// provenance. Caller must supply the declared source's pixels. No image fitting
// or tracking occurs here. Submit consumes leases only after successful admission.
Result<std::unique_ptr<PoseEstimator>>
make_completed_pose_replay(const std::shared_ptr<const CompletedPoseDataset>& dataset);

}  // namespace world_explorer
#endif
