#include "world_explorer/application/pose_preparation.hpp"
#include "pose_pipeline.hpp"
#include "world_explorer/core/hash.hpp"
#include <new>

namespace world_explorer {
result<prepared_pose_map> prepare_completed_pose_map(const pose_preparation_request& request,
                                                     const std::stop_token& stop,
                                                     const log_callback& log) {
    try {
        if (request.map_directory.empty())
            return fail(error_code::INVALID_ARGUMENT, "Pose preparation requires a map directory");
        const auto map = std::filesystem::weakly_canonical(request.map_directory);
        for (const auto& input : {request.completed.pose_job, request.completed.video}) {
            const auto selected = std::filesystem::weakly_canonical(input);
            const auto relative = selected.lexically_relative(map);
            const auto reverse = map.lexically_relative(selected);
            if (map == selected || (!relative.empty() && *relative.begin() != "..") ||
                (!reverse.empty() && *reverse.begin() != ".."))
                return fail(error_code::INVALID_ARGUMENT, "Pose map overlaps its inputs");
        }
        if (log)
            log("Preparing durable poses from retained outputs; no solve or fit.\n");
        const auto digest = sha256_file(request.completed.video, 16ULL * 1024 * 1024 * 1024, stop);
        if (!digest)
            return std::unexpected(digest.error());
        const auto loaded = load_completed_poses(
            {request.completed.pose_job, *digest, request.completed.decode_stage}, stop);
        if (!loaded)
            return std::unexpected(loaded.error());
        const auto asset = pose_pipeline::canonical(**loaded, stop);
        if (!asset)
            return std::unexpected(asset.error());
        const auto final_digest =
            sha256_file(request.completed.video, 16ULL * 1024 * 1024 * 1024, stop);
        if (!final_digest)
            return std::unexpected(final_digest.error());
        if (*final_digest != *digest)
            return fail(error_code::INVALID_DATA, "Source changed during pose preparation");
        return pose_pipeline::persist(*asset, request.map_directory, stop);
    } catch (const std::bad_alloc&) {
        return fail(error_code::RESOURCE_EXHAUSTED, "Pose preparation allocation failed");
    } catch (const std::exception& e) {
        return fail(error_code::INVALID_DATA, std::string("Pose preparation failed: ") + e.what());
    }
}
} // namespace world_explorer
