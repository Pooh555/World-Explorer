#ifndef WORLD_EXPLORER_APPLICATION_DEPTH_ASSETS_HPP
#define WORLD_EXPLORER_APPLICATION_DEPTH_ASSETS_HPP
#include "world_explorer/scene_store/depth_asset.hpp"
namespace world_explorer {
struct depth_admission_request {
    std::filesystem::path depth_file, canonical_poses;
    std::string depth_sha256, pose_sha256;
    // The expected undistorted domain/recipe must come from checked dense input
    // preparation, independently of the untrusted depth file being loaded.
    depth_asset_metadata expected;
};
// Checks both immutable files and current pose/observation dependencies. Does not
// update a scene or qualify the supplied dense domain, metric scale or collision.
result<depth_image_asset> load_pose_bound_depth(const depth_admission_request& request,
                                                depth_asset_limits limits = {},
                                                const std::stop_token& stop = {});
// Checked diagnostic summary only; no pose-context admission or geometry readiness.
result<std::string> inspect_depth_file(const std::filesystem::path& path,
                                       const std::string& expected_sha256,
                                       depth_asset_limits limits = {},
                                       const std::stop_token& stop = {});
} // namespace world_explorer
#endif
