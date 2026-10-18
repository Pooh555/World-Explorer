#include "world_explorer/application/pose_export.hpp"

#include "pose_pipeline.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/reconstruction_adapters/completed_poses.hpp"

#include <new>

namespace world_explorer {

Result<std::string> export_completed_pose_asset(const PoseExportRequest& request,
                                                const std::stop_token& stop,
                                                const LogCallback& log) {
    try {
        if (log) log("Verifying retained poses for canonical export; no solve or fit.\n");
        const auto K_DIGEST =
            sha256_file(request.m_completed.m_video, 16ULL * 1024 * 1024 * 1024, stop);
        if (!K_DIGEST) return std::unexpected(K_DIGEST.error());
        const auto K_LOADED = load_completed_poses(
            {request.m_completed.m_pose_job, *K_DIGEST, request.m_completed.m_decode_stage}, stop);
        if (!K_LOADED) return std::unexpected(K_LOADED.error());
        const auto K_ASSET = pose_pipeline::canonical(**K_LOADED, stop);
        if (!K_ASSET) return std::unexpected(K_ASSET.error());
        return write_pose_asset(request.m_destination, *K_ASSET, {}, stop);
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Pose export allocation failed");
    } catch (const std::exception& e) {
        return fail(ErrorCode::INVALID_DATA, std::string("Pose export failed: ") + e.what());
    }
}

}  // namespace world_explorer
