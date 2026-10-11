#include "world_explorer/application/pose_replay.hpp"
#include "pose_pipeline.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/reconstruction_adapters/completed_poses.hpp"
#include "world_explorer/reconstruction_adapters/decoded_frame_source.hpp"
#include <new>

namespace world_explorer {
result<map_snapshot> replay_completed_pose_map(const pose_replay_request& request,
                                               const std::stop_token& stop,
                                               const log_callback& log) {
    try {
        if (log)
            log("Completed-pose replay: verifying existing assets; no solver or fitting run.\n");
        const auto digest = sha256_file(request.video, 16ULL * 1024 * 1024 * 1024, stop);
        if (!digest)
            return std::unexpected(digest.error());
        const auto dataset =
            load_completed_poses({request.pose_job, *digest, request.decode_stage}, stop);
        if (!dataset)
            return std::unexpected(dataset.error());
        const auto source = open_decoded_frame_source(
            {.stage_directory = (*dataset)->decode_directory, .source_sha256 = *digest}, stop);
        if (!source)
            return std::unexpected(source.error());
        auto estimator = make_completed_pose_replay(*dataset);
        if (!estimator)
            return std::unexpected(estimator.error());
        const auto started = (*estimator)->start({{std::size_t{64} * 1024 * 1024}, 1});
        if (!started)
            return std::unexpected(started.error());
        const auto events = pose_pipeline::consume(**source, **estimator, stop);
        if (!events)
            return std::unexpected(events.error());
        const auto snapshot = pose_pipeline::publish(**dataset, *events);
        if (!snapshot)
            return std::unexpected(snapshot.error());
        if (log)
            log("Published in-memory map snapshot from completed-pose replay. Scale arbitrary; "
                "geometry unavailable.\n");
        return snapshot;
    } catch (const std::bad_alloc&) {
        return fail(error_code::RESOURCE_EXHAUSTED, "Pose replay command allocation failed");
    } catch (const std::exception& exception) {
        return fail(error_code::INVALID_DATA,
                    std::string("Pose replay command failed: ") + exception.what());
    }
}
} // namespace world_explorer
