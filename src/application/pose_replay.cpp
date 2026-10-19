#include "world_explorer/application/pose_replay.hpp"

#include "pose_pipeline.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/reconstruction_adapters/completed_poses.hpp"
#include "world_explorer/reconstruction_adapters/decoded_frame_source.hpp"

#include <new>

namespace world_explorer {

Result<MapSnapshot> replay_completed_pose_map(const PoseReplayRequest& request,
                                              const std::stop_token& stop,
                                              const LogCallback& log) {
    try {
        if (log) {
            log("Completed-pose replay: verifying existing assets; no solver or fitting run.\n");
        }
        const auto K_DIGEST = sha256_file(request.m_video, 16ULL * 1024 * 1024 * 1024, stop);
        if (!K_DIGEST) {
            return std::unexpected(K_DIGEST.error());
        }
        const auto K_DATASET =
            load_completed_poses({ request.m_pose_job, *K_DIGEST, request.m_decode_stage }, stop);
        if (!K_DATASET) {
            return std::unexpected(K_DATASET.error());
        }
        const auto K_SOURCE = open_decoded_frame_source(
            { .m_stage_directory = (*K_DATASET)->m_decode_directory, .m_source_sha256 = *K_DIGEST },
            stop);
        if (!K_SOURCE) {
            return std::unexpected(K_SOURCE.error());
        }
        auto estimator = make_completed_pose_replay(*K_DATASET);
        if (!estimator) {
            return std::unexpected(estimator.error());
        }
        const auto K_STARTED = (*estimator)->start({ { 64uz * 1024 * 1024 }, 1 });
        if (!K_STARTED) {
            return std::unexpected(K_STARTED.error());
        }
        const auto K_EVENTS = pose_pipeline::consume(**K_SOURCE, **estimator, stop);
        if (!K_EVENTS) {
            return std::unexpected(K_EVENTS.error());
        }
        const auto K_SNAPSHOT = pose_pipeline::publish(**K_DATASET, *K_EVENTS);
        if (!K_SNAPSHOT) {
            return std::unexpected(K_SNAPSHOT.error());
        }
        if (log) {
            log("Published in-memory map snapshot from completed-pose replay. Scale arbitrary; "
                "geometry unavailable.\n");
        }
        return K_SNAPSHOT;
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Pose replay command allocation failed");
    } catch (const std::exception& exception) {
        return fail(ErrorCode::INVALID_DATA,
                    std::string("Pose replay command failed: ") + exception.what());
    }
}

}  // namespace world_explorer
