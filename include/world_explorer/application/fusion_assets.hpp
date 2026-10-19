#ifndef WORLD_EXPLORER_APPLICATION_FUSION_ASSETS_HPP
#define WORLD_EXPLORER_APPLICATION_FUSION_ASSETS_HPP

#include "world_explorer/scene_store/fusion_asset.hpp"

#include <filesystem>

namespace world_explorer {

struct FusionAdmissionRequest {
    std::filesystem::path m_asset, m_poses;
    std::string m_asset_sha256, m_pose_sha256;
    std::string m_manifest_sha256, m_receipt_sha256, m_recipe_sha256;
};

// Read-only admission against an explicitly selected current canonical pose asset.
// No scene/collision publication; arbitrary units and all missing regions unknown.
Result<ObservedFusionAsset> load_pose_bound_fusion(const FusionAdmissionRequest& request,
                                                   FusionAssetLimits limits = {},
                                                   const std::stop_token& stop = {});
Result<std::string> inspect_fusion_asset(const ObservedFusionAsset& asset);
// Full component/support denominators; no cleanup or quality promotion.
Result<std::string> inspect_fusion_components(const ObservedFusionAsset& asset,
                                              const std::stop_token& stop = {});

}  // namespace world_explorer
#endif
