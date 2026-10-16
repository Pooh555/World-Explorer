#include "world_explorer/application/depth_assets.hpp"
#include "world_explorer/core/asset_files.hpp"
#include "world_explorer/map_store/pose_asset.hpp"
#include <exception>
#include <new>
#include <nlohmann/json.hpp>

namespace world_explorer {
result<depth_image_asset> load_pose_bound_depth(const depth_admission_request& request,
                                                depth_asset_limits limits,
                                                const std::stop_token& stop) {
    if (stop.stop_requested())
        return fail(error_code::CANCELLED, "Depth admission cancelled");
    if (request.expected.dependency.pose_asset_sha256 != request.pose_sha256)
        return fail(error_code::RESYNC_REQUIRED, "Depth request pins a different pose asset");
    const auto poses = read_pose_asset(request.canonical_poses, request.pose_sha256, {}, stop);
    if (!poses)
        return std::unexpected(poses.error());
    auto depth =
        read_depth_asset(request.depth_file, request.depth_sha256, request.expected, limits, stop);
    if (!depth)
        return std::unexpected(depth.error());
    const auto& map = *poses->snapshot;
    const auto& dependency = depth->metadata.dependency;
    if (dependency.epoch != map.epoch)
        return fail(error_code::WRONG_EPOCH, "Depth no longer belongs to the current pose epoch");
    if (dependency.pose_revision != map.revision)
        return fail(error_code::WRONG_BASE, "Depth no longer belongs to the current pose revision");
    const auto observation = map.observations.find(dependency.frame);
    const auto pose = map.poses.find(dependency.frame);
    if (observation == map.observations.end() || pose == map.poses.end() ||
        map.invalidated_frames.contains(dependency.frame) ||
        (pose->second.tracking != tracking_state::TRACKING &&
         pose->second.tracking != tracking_state::FINISHED))
        return fail(error_code::RESYNC_REQUIRED, "Depth frame is missing or invalidated");
    if (observation->second.calibration != dependency.calibration ||
        observation->second.image_sha256 != dependency.observation_image_sha256)
        return fail(error_code::INVALID_DATA,
                    "Depth source observation differs from the pose asset");
    return depth;
}
result<std::string> inspect_depth_file(const std::filesystem::path& path,
                                       const std::string& expected_sha256,
                                       depth_asset_limits limits, const std::stop_token& stop) {
    try {
        const auto bytes = asset_files::read_bytes(path, limits.encoded_bytes.bytes, stop);
        if (!bytes)
            return std::unexpected(bytes.error());
        const auto asset = decode_depth_asset(*bytes, expected_sha256, limits, stop);
        if (!asset)
            return std::unexpected(asset.error());
        std::size_t valid_pixels{};
        for (std::size_t i = 0; i < asset->valid.size(); ++i) {
            if (i % 4096 == 0 && stop.stop_requested())
                return fail(error_code::CANCELLED, "Depth inspection cancelled");
            valid_pixels += asset->valid[i];
        }
        const auto& metadata = asset->metadata;
        const auto& dependency = metadata.dependency;
        const nlohmann::json summary{
            {"schema_version", 1},
            {"scope", "depth format inspection; pose context and geometry quality unverified"},
            {"producer", metadata.producer == depth_producer_kind::SYNTHETIC_DIAGNOSTIC
                             ? "SYNTHETIC_DIAGNOSTIC"
                             : "MULTIVIEW_STEREO"},
            {"depth_convention", "AXIAL_CAMERA_Z"},
            {"units", "ARBITRARY_POSE_CAMERA_UNITS"},
            {"pixel_centers", "integer"},
            {"camera_axes", "+x_right,+y_down,+z_forward"},
            {"width", metadata.width},
            {"height", metadata.height},
            {"intrinsics",
             {metadata.camera.fx, metadata.camera.fy, metadata.camera.cx, metadata.camera.cy}},
            {"valid_pixels", valid_pixels},
            {"unknown_pixels", asset->valid.size() - valid_pixels},
            {"confidence", "RECIPE_DEFINED_RELATIVE_WEIGHT_NOT_PROBABILITY"},
            {"uncertainty",
             asset->standard_deviation ? "CAMERA_UNIT_STANDARD_DEVIATION" : "UNAVAILABLE"},
            {"world_epoch", dependency.epoch.value},
            {"pose_revision", dependency.pose_revision.value},
            {"frame", dependency.frame.value},
            {"calibration", dependency.calibration.value},
            {"pose_asset_sha256", dependency.pose_asset_sha256},
            {"observation_image_sha256", dependency.observation_image_sha256},
            {"calibrated_image_sha256", metadata.calibrated_image_sha256},
            {"recipe_sha256", metadata.recipe_sha256},
            {"asset_sha256", expected_sha256}};
        return summary.dump(2);
    } catch (const std::bad_alloc&) {
        return fail(error_code::RESOURCE_EXHAUSTED, "Depth inspection allocation failed");
    } catch (const std::exception& exception) {
        return fail(error_code::INVALID_DATA,
                    std::string("Depth inspection failed: ") + exception.what());
    }
}
} // namespace world_explorer
