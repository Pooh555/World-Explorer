#ifndef WORLD_EXPLORER_RECONSTRUCTION_ADAPTERS_COMPLETED_POSES_HPP
#define WORLD_EXPLORER_RECONSTRUCTION_ADAPTERS_COMPLETED_POSES_HPP
#include "world_explorer/reconstruction_api/map_records.hpp"
#include "world_explorer/reconstruction_api/pose_estimator.hpp"
#include <filesystem>
#include <memory>
#include <stop_token>
#include <string>
#include <vector>

namespace world_explorer {
struct completed_pose_config {
    std::filesystem::path pose_job;
    std::string source_sha256;
    // Optional explicit immutable stage when the mutable decode pointer no
    // longer identifies these completed poses. Never search/fallback silently.
    std::filesystem::path decode_stage{};
};
struct completed_pose_dataset {
    world_epoch epoch;
    map_revision_id revision;
    submap_id submap;
    scale_status scale{scale_status::ARBITRARY};
    std::string source_sha256, pose_completion_sha256, decode_completion_sha256;
    std::filesystem::path decode_directory;
    std::vector<calibration_record> calibrations;
    std::vector<observation_record> observations;
    std::vector<pose_estimate> poses;
};
// Checked import of the legacy COLMAP canonical JSON v1 profile. This does not
// execute a solver. Unknown camera/coordinate profiles fail explicitly.
result<std::shared_ptr<const completed_pose_dataset>>
load_completed_poses(const completed_pose_config& config, const std::stop_token& stop = {});
// Batch replay of a checked completed dataset, never a fresh pose solve. Pixels
// are admitted by immutable frame identity/metadata; dataset owns encoded-image
// provenance. Caller must supply the declared source's pixels. No image fitting
// or tracking occurs here. Submit consumes leases only after successful admission.
result<std::unique_ptr<pose_estimator>>
make_completed_pose_replay(const std::shared_ptr<const completed_pose_dataset>& dataset);
} // namespace world_explorer
#endif
