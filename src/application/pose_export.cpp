#include "world_explorer/application/pose_export.hpp"
#include "pose_pipeline.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/reconstruction_adapters/completed_poses.hpp"
#include <new>

namespace world_explorer {
result<std::string> export_completed_pose_asset(const pose_export_request& request,
                                                const std::stop_token& stop,
                                                const log_callback& log) {
    try {
        if (log)
            log("Verifying retained poses for canonical export; no solve or fit.\n");
        const auto digest = sha256_file(request.completed.video, 16ULL * 1024 * 1024 * 1024, stop);
        if (!digest)
            return std::unexpected(digest.error());
        const auto loaded = load_completed_poses(
            {request.completed.pose_job, *digest, request.completed.decode_stage}, stop);
        if (!loaded)
            return std::unexpected(loaded.error());
        const auto& dataset = **loaded;
        std::vector<pose_event> events;
        events.emplace_back(map_alignment{dataset.submap, dataset.revision, {}});
        for (const auto& pose : dataset.poses)
            events.emplace_back(pose);
        const auto snapshot = pose_pipeline::publish(dataset, events);
        if (!snapshot)
            return std::unexpected(snapshot.error());
        return write_pose_asset(request.destination,
                                {*snapshot,
                                 {dataset.source_sha256, dataset.decode_completion_sha256,
                                  dataset.pose_completion_sha256, dataset.journal_sha256}},
                                {}, stop);
    } catch (const std::bad_alloc&) {
        return fail(error_code::RESOURCE_EXHAUSTED, "Pose export allocation failed");
    } catch (const std::exception& e) {
        return fail(error_code::INVALID_DATA, std::string("Pose export failed: ") + e.what());
    }
}
} // namespace world_explorer
