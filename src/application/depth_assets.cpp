#include "world_explorer/application/depth_assets.hpp"

#include "world_explorer/core/asset_files.hpp"
#include "world_explorer/map_store/pose_asset.hpp"

#include <nlohmann/json.hpp>

#include <exception>
#include <new>

namespace world_explorer {

Result<DepthImageAsset> load_pose_bound_depth(const DepthAdmissionRequest& request,
                                              DepthAssetLimits limits,
                                              const std::stop_token& stop) {
    if (stop.stop_requested()) {
        return fail(ErrorCode::CANCELLED, "Depth admission cancelled");
    }
    if (request.m_expected.m_dependency.m_pose_asset_sha256 != request.m_pose_sha256) {
        return fail(ErrorCode::RESYNC_REQUIRED, "Depth request pins a different pose asset");
    }
    const auto K_POSES =
        read_pose_asset(request.m_canonical_poses, request.m_pose_sha256, {}, stop);
    if (!K_POSES) {
        return std::unexpected(K_POSES.error());
    }
    auto depth = read_depth_asset(
        request.m_depth_file, request.m_depth_sha256, request.m_expected, limits, stop);
    if (!depth) {
        return std::unexpected(depth.error());
    }
    const auto& map = *K_POSES->m_snapshot;
    const auto& dependency = depth->m_metadata.m_dependency;
    if (dependency.m_epoch != map.m_epoch) {
        return fail(ErrorCode::WRONG_EPOCH, "Depth no longer belongs to the current pose epoch");
    }
    if (dependency.m_pose_revision != map.m_revision) {
        return fail(ErrorCode::WRONG_BASE, "Depth no longer belongs to the current pose revision");
    }
    const auto K_OBSERVATION = map.m_observations.find(dependency.m_frame);
    const auto K_POSE = map.m_poses.find(dependency.m_frame);
    if (K_OBSERVATION == map.m_observations.end() || K_POSE == map.m_poses.end() ||
        map.m_invalidated_frames.contains(dependency.m_frame) ||
        (K_POSE->second.m_tracking != TrackingState::TRACKING &&
         K_POSE->second.m_tracking != TrackingState::FINISHED)) {
        return fail(ErrorCode::RESYNC_REQUIRED, "Depth frame is missing or invalidated");
    }
    if (K_OBSERVATION->second.m_calibration != dependency.m_calibration ||
        K_OBSERVATION->second.m_image_sha256 != dependency.m_observation_image_sha256) {
        return fail(ErrorCode::INVALID_DATA,
                    "Depth source observation differs from the pose asset");
    }
    return depth;
}

Result<std::string> inspect_depth_file(const std::filesystem::path& path,
                                       const std::string& expected_sha256,
                                       DepthAssetLimits limits,
                                       const std::stop_token& stop) {
    try {
        const auto K_BYTES = asset_files::read_bytes(path, limits.m_encoded_bytes.m_bytes, stop);
        if (!K_BYTES) {
            return std::unexpected(K_BYTES.error());
        }
        const auto K_ASSET = decode_depth_asset(*K_BYTES, expected_sha256, limits, stop);
        if (!K_ASSET) {
            return std::unexpected(K_ASSET.error());
        }
        std::size_t valid_pixels {};
        for (std::size_t i { 0uz }; i < K_ASSET->m_valid.size(); ++i) {
            if (i % 4096 == 0 && stop.stop_requested()) {
                return fail(ErrorCode::CANCELLED, "Depth inspection cancelled");
            }
            valid_pixels += K_ASSET->m_valid[i];
        }
        const auto& metadata = K_ASSET->m_metadata;
        const auto& dependency = metadata.m_dependency;
        const nlohmann::json K_SUMMARY {
            { "schema_version", 1 },
            { "scope", "depth format inspection; pose context and geometry quality unverified" },
            { "producer",
             metadata.m_producer == DepthProducerKind::SYNTHETIC_DIAGNOSTIC ? "SYNTHETIC_DIAGNOSTIC"
                                                                            : "MULTIVIEW_STEREO" },
            { "depth_convention", "AXIAL_CAMERA_Z" },
            { "units", "ARBITRARY_POSE_CAMERA_UNITS" },
            { "pixel_centers", "integer" },
            { "camera_axes", "+x_right,+y_down,+z_forward" },
            { "width", metadata.m_width },
            { "height", metadata.m_height },
            { "intrinsics",
             { metadata.m_camera.m_fx,
              metadata.m_camera.m_fy,
              metadata.m_camera.m_cx,
              metadata.m_camera.m_cy } },
            { "valid_pixels", valid_pixels },
            { "unknown_pixels", K_ASSET->m_valid.size() - valid_pixels },
            { "confidence", "RECIPE_DEFINED_RELATIVE_WEIGHT_NOT_PROBABILITY" },
            { "uncertainty",
             K_ASSET->m_standard_deviation ? "CAMERA_UNIT_STANDARD_DEVIATION" : "UNAVAILABLE" },
            { "world_epoch", dependency.m_epoch.m_value },
            { "pose_revision", dependency.m_pose_revision.m_value },
            { "frame", dependency.m_frame.m_value },
            { "calibration", dependency.m_calibration.m_value },
            { "pose_asset_sha256", dependency.m_pose_asset_sha256 },
            { "observation_image_sha256", dependency.m_observation_image_sha256 },
            { "calibrated_image_sha256", metadata.m_calibrated_image_sha256 },
            { "recipe_sha256", metadata.m_recipe_sha256 },
            { "asset_sha256", expected_sha256 } };
        return K_SUMMARY.dump(2);
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Depth inspection allocation failed");
    } catch (const std::exception& exception) {
        return fail(ErrorCode::INVALID_DATA,
                    std::string("Depth inspection failed: ") + exception.what());
    }
}

}  // namespace world_explorer
