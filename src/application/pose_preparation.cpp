#include "world_explorer/application/pose_preparation.hpp"

#include "pose_pipeline.hpp"
#include "world_explorer/core/hash.hpp"

#include <new>

namespace world_explorer {

Result<PreparedPoseMap> prepare_completed_pose_map(const PosePreparationRequest& request,
                                                   const std::stop_token& stop,
                                                   const LogCallback& log) {
    try {
        if (request.m_map_directory.empty())
            return fail(ErrorCode::INVALID_ARGUMENT, "Pose preparation requires a map directory");
        const auto K_MAP = std::filesystem::weakly_canonical(request.m_map_directory);
        for (const auto& input : {request.m_completed.m_pose_job, request.m_completed.m_video}) {
            const auto K_SELECTED = std::filesystem::weakly_canonical(input);
            const auto K_RELATIVE = K_SELECTED.lexically_relative(K_MAP);
            const auto K_REVERSE = K_MAP.lexically_relative(K_SELECTED);
            if (K_MAP == K_SELECTED || (!K_RELATIVE.empty() && *K_RELATIVE.begin() != "..") ||
                (!K_REVERSE.empty() && *K_REVERSE.begin() != ".."))
                return fail(ErrorCode::INVALID_ARGUMENT, "Pose map overlaps its inputs");
        }
        if (log) log("Preparing durable poses from retained outputs; no solve or fit.\n");
        const auto K_DIGEST =
            sha256_file(request.m_completed.m_video, 16ULL * 1024 * 1024 * 1024, stop);
        if (!K_DIGEST) return std::unexpected(K_DIGEST.error());
        const auto K_LOADED = load_completed_poses(
            {request.m_completed.m_pose_job, *K_DIGEST, request.m_completed.m_decode_stage}, stop);
        if (!K_LOADED) return std::unexpected(K_LOADED.error());
        const auto K_ASSET = pose_pipeline::canonical(**K_LOADED, stop);
        if (!K_ASSET) return std::unexpected(K_ASSET.error());
        const auto K_FINAL_DIGEST =
            sha256_file(request.m_completed.m_video, 16ULL * 1024 * 1024 * 1024, stop);
        if (!K_FINAL_DIGEST) return std::unexpected(K_FINAL_DIGEST.error());
        if (*K_FINAL_DIGEST != *K_DIGEST)
            return fail(ErrorCode::INVALID_DATA, "Source changed during pose preparation");
        return pose_pipeline::persist(*K_ASSET, request.m_map_directory, stop);
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Pose preparation allocation failed");
    } catch (const std::exception& e) {
        return fail(ErrorCode::INVALID_DATA, std::string("Pose preparation failed: ") + e.what());
    }
}

}  // namespace world_explorer
