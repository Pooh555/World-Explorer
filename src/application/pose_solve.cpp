#include "world_explorer/application/pose_solve.hpp"

#include "pose_pipeline.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/reconstruction_adapters/decoded_frame_source.hpp"

#include <new>

namespace world_explorer {

Result<MapSnapshot> solve_selected_pose_map(PoseSolveRequest request,
                                            const std::stop_token& stop,
                                            const LogCallback& log) {
    try {
        auto map_directory = request.m_batch.m_job_directory;
        map_directory += ".map";
        if (std::filesystem::exists(std::filesystem::symlink_status(map_directory))) {
            return fail(ErrorCode::INVALID_ARGUMENT,
                        "Batch solve requires a fresh NEW_JOB.map directory; prepare retained "
                        "poses to resume publication");
        }
        const auto K_DIGEST = sha256_file(request.m_video, 16ULL * 1024 * 1024 * 1024, stop);
        if (!K_DIGEST) {
            return std::unexpected(K_DIGEST.error());
        }
        request.m_batch.m_source_sha256 = *K_DIGEST;
        request.m_batch.m_log = log;
        auto estimator = make_batch_pose_estimator(request.m_batch, stop);
        if (!estimator) {
            return std::unexpected(estimator.error());
        }
        auto source = open_decoded_frame_source(
            { .m_stage_directory = request.m_batch.m_decode_stage, .m_source_sha256 = *K_DIGEST },
            stop);
        if (!source) {
            return std::unexpected(source.error());
        }
        const auto K_STARTED = (*estimator)
                                   ->start({ { 64uz * 1024 * 1024 },
                                            (*estimator)->capabilities().m_observation_capacity });
        if (!K_STARTED) {
            return std::unexpected(K_STARTED.error());
        }
        if (log) {
            log("Fresh COLMAP batch: journaling accepted RGB observations locally.\n");
        }
        const auto K_EVENTS = pose_pipeline::consume(**source, **estimator, stop);
        if (!K_EVENTS) {
            return std::unexpected(K_EVENTS.error());
        }
        const auto K_DATASET = (*estimator)->acquire_dataset();
        if (!K_DATASET) {
            return std::unexpected(K_DATASET.error());
        }
        const auto K_SNAPSHOT = pose_pipeline::publish(**K_DATASET, *K_EVENTS, stop);
        if (!K_SNAPSHOT) {
            return std::unexpected(K_SNAPSHOT.error());
        }
        const auto& data = **K_DATASET;
        const auto K_PREPARED = pose_pipeline::persist({ *K_SNAPSHOT,
                                                        { data.m_source_sha256,
                                                         data.m_decode_completion_sha256,
                                                         data.m_pose_completion_sha256,
                                                         data.m_journal_sha256 } },
                                                       map_directory,
                                                       stop);
        if (!K_PREPARED) {
            return std::unexpected(K_PREPARED.error());
        }
        if (log) {
            log("Fresh COLMAP poses published durably at NEW_JOB.map. Scale arbitrary; "
                "geometry unavailable.\n");
        }
        return K_PREPARED->m_store->acquire_snapshot();
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Batch pose command allocation failed");
    } catch (const std::exception& exception) {
        return fail(ErrorCode::INVALID_DATA, exception.what());
    }
}

}  // namespace world_explorer
