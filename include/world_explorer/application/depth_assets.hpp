#pragma once

#include "world_explorer/scene_store/depth_asset.hpp"

namespace world_explorer {

struct DepthAdmissionRequest {
    std::filesystem::path m_depth_file, m_canonical_poses;
    std::string m_depth_sha256, m_pose_sha256;
    // The expected undistorted domain/recipe must come from checked dense input
    // preparation, independently of the untrusted depth file being loaded.
    DepthAssetMetadata m_expected;
};

// Checks both immutable files and current pose/observation dependencies. Does not
// update a scene or qualify the supplied dense domain, metric scale or collision.
Result<DepthImageAsset> load_pose_bound_depth(const DepthAdmissionRequest& request,
                                              DepthAssetLimits limits = {},
                                              const std::stop_token& stop = {});
// Checked diagnostic summary only; no pose-context admission or geometry readiness.
Result<std::string> inspect_depth_file(const std::filesystem::path& path,
                                       const std::string& expected_sha256,
                                       DepthAssetLimits limits = {},
                                       const std::stop_token& stop = {});

}  // namespace world_explorer
