#include "world_explorer/application/fusion_assets.hpp"

#include "world_explorer/core/asset_files.hpp"
#include "world_explorer/map_store/pose_asset.hpp"

#include <algorithm>
#include <cmath>
#include <new>

#include <nlohmann/json.hpp>

namespace world_explorer {

Result<ObservedFusionAsset> load_pose_bound_fusion(const FusionAdmissionRequest& request,
                                                   FusionAssetLimits limits,
                                                   const std::stop_token& stop) {
    try {
        const auto K_POSES = read_pose_asset(request.m_poses, request.m_pose_sha256, {}, stop);
        if (!K_POSES) return std::unexpected(K_POSES.error());
        const auto& map = *K_POSES->m_snapshot;
        const auto K_BYTES = asset_files::read_bytes(
            request.m_asset,
            std::min(limits.m_encoded_bytes.m_bytes, 256uz * 1024uz * 1024uz),
            stop);
        if (!K_BYTES) return std::unexpected(K_BYTES.error());
        const FusionContext K_CONTEXT {request.m_manifest_sha256,
                                       request.m_receipt_sha256,
                                       request.m_recipe_sha256,
                                       request.m_pose_sha256,
                                       map.m_epoch,
                                       map.m_revision};
        auto asset = decode_fusion_asset(*K_BYTES, request.m_asset_sha256, K_CONTEXT, limits, stop);
        if (!asset) return std::unexpected(asset.error());
        const auto K_RECEIPT = nlohmann::json::parse(asset->m_receipt_json);
        for (const auto& frame : K_RECEIPT.at("frames")) {
            if (stop.stop_requested())
                return fail(ErrorCode::CANCELLED, "Fusion pose check cancelled");
            const FrameId K_ID { frame.at("frame").get<std::uint64_t>() };
            const auto K_POSE = map.m_poses.find(K_ID);
            const auto K_OBSERVATION = map.m_observations.find(K_ID);
            if (K_POSE == map.m_poses.end() || K_OBSERVATION == map.m_observations.end() ||
                map.m_invalidated_frames.contains(K_ID) ||
                (K_POSE->second.m_tracking != TrackingState::TRACKING &&
                 K_POSE->second.m_tracking != TrackingState::FINISHED))
                return fail(ErrorCode::RESYNC_REQUIRED,
                            "Fusion source pose missing or invalidated");
            if (!frame.at("calibration").is_number_unsigned() ||
                frame.at("calibration") != K_OBSERVATION->second.m_calibration.m_value ||
                frame.at("observation_image_sha256") != K_OBSERVATION->second.m_image_sha256)
                return fail(ErrorCode::INVALID_DATA, "Fusion source observation differs");
            const auto K_ALIGNMENT = map.m_world_from_submap.find(K_POSE->second.m_submap);
            if (K_ALIGNMENT == map.m_world_from_submap.end())
                return fail(ErrorCode::RESYNC_REQUIRED, "Fusion source alignment missing");
            const auto& encoded = frame.at("world_from_camera");
            const auto K_Q = encoded.at("rotation_wxyz").get<std::array<double, 4>>();
            const auto K_T = encoded.at("translation").get<std::array<double, 3>>();
            const SimilarityTransform K_TRANSFORM {{ K_Q[0], K_Q[1], K_Q[2], K_Q[3] },
                                                   { K_T[0], K_T[1], K_T[2] },
                                                   encoded.at("scale").get<double>()};
            if (!valid(K_TRANSFORM) || K_TRANSFORM.m_scale != 1)
                return fail(ErrorCode::INVALID_DATA, "Invalid fusion world camera transform");
            for (const auto K_POINT :
                 {Vec3d {}, Vec3d { 1, 0, 0 }, Vec3d { 0, 1, 0 }, Vec3d { 0, 0, 1 }}) {
                const auto K_EXPECTED = transform_point(K_TRANSFORM, K_POINT);
                const auto K_ACTUAL =
                    transform_point(K_ALIGNMENT->second,
                                    transform_point(K_POSE->second.m_submap_from_camera, K_POINT));
                const auto K_CLOSE = [](double left, double right) {
                    return std::isfinite(left) && std::isfinite(right) &&
                           std::abs(left - right) <=
                               1e-8 * std::max({ 1.0, std::abs(left), std::abs(right) });
                };
                if (!K_CLOSE(K_ACTUAL.m_x, K_EXPECTED.m_x) ||
                    !K_CLOSE(K_ACTUAL.m_y, K_EXPECTED.m_y) ||
                    !K_CLOSE(K_ACTUAL.m_z, K_EXPECTED.m_z))
                    return fail(ErrorCode::RESYNC_REQUIRED, "Fusion world camera pose differs");
            }
        }
        return asset;
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Fusion pose admission allocation failed");
    } catch (const std::exception& exception) {
        return fail(ErrorCode::INVALID_DATA,
                    std::string("Fusion pose admission failed: ") + exception.what());
    }
}

Result<std::string> inspect_fusion_asset(const ObservedFusionAsset& asset) {
    try {
        const auto K_MANIFEST = nlohmann::json::parse(asset.m_manifest_json);
        const nlohmann::json K_SUMMARY {
            { "schema_version", 1 },
            { "asset_sha256", asset.m_asset_sha256 },
            { "scope", "pose-bound partial observed geometry; quality unverified" },
            { "units", "ARBITRARY_POSE_CAMERA_UNITS" },
            { "walking_enabled", false },
            { "world_epoch", asset.m_context.m_epoch.m_value },
            { "pose_revision", asset.m_context.m_pose_revision.m_value },
            { "pose_asset_sha256", asset.m_context.m_pose_sha256 },
            { "manifest_sha256", asset.m_context.m_manifest_sha256 },
            { "receipt_sha256", asset.m_context.m_receipt_sha256 },
            { "recipe_sha256", asset.m_context.m_recipe_sha256 },
            { "extent", asset.m_extent },
            { "voxel_size", asset.m_voxel_size },
            { "truncation", asset.m_truncation },
            { "observed_voxels", asset.m_voxels.size() },
            { "unknown_allocated_voxels", asset.m_unknown_allocated_voxels },
            { "vertices", asset.m_vertices.size() },
            { "triangles", asset.m_triangles.size() },
            { "front_band_voxels", K_MANIFEST.at("front_band_voxels") },
            { "sigma_available_voxels", K_MANIFEST.at("sigma_available_voxels") },
            { "free_space", K_MANIFEST.at("free_space") },
            { "unknown", K_MANIFEST.at("unknown") },
            { "uncertainty", K_MANIFEST.at("uncertainty") },
            { "coverage", K_MANIFEST.at("coverage") }};
        return K_SUMMARY.dump(2);
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Fusion inspection allocation failed");
    } catch (const std::exception& exception) {
        return fail(ErrorCode::INVALID_DATA,
                    std::string("Fusion inspection failed: ") + exception.what());
    }
}

}  // namespace world_explorer
