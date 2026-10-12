#include "world_explorer/application/pose_solve.hpp"
#include "pose_pipeline.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/reconstruction_adapters/decoded_frame_source.hpp"
#include <new>

namespace world_explorer {
result<map_snapshot> solve_selected_pose_map(pose_solve_request request,
                                             const std::stop_token& stop, const log_callback& log) {
    try {
        auto map_directory = request.batch.job_directory;
        map_directory += ".map";
        if (std::filesystem::exists(std::filesystem::symlink_status(map_directory)))
            return fail(error_code::INVALID_ARGUMENT,
                        "Batch solve requires a fresh NEW_JOB.map directory; prepare retained "
                        "poses to resume publication");
        const auto digest = sha256_file(request.video, 16ULL * 1024 * 1024 * 1024, stop);
        if (!digest)
            return std::unexpected(digest.error());
        request.batch.source_sha256 = *digest;
        request.batch.log = log;
        auto estimator = make_batch_pose_estimator(request.batch, stop);
        if (!estimator)
            return std::unexpected(estimator.error());
        auto source = open_decoded_frame_source(
            {.stage_directory = request.batch.decode_stage, .source_sha256 = *digest}, stop);
        if (!source)
            return std::unexpected(source.error());
        const auto started = (*estimator)
                                 ->start({{std::size_t{64} * 1024 * 1024},
                                          (*estimator)->capabilities().observation_capacity});
        if (!started)
            return std::unexpected(started.error());
        if (log)
            log("Fresh COLMAP batch: journaling accepted RGB observations locally.\n");
        const auto events = pose_pipeline::consume(**source, **estimator, stop);
        if (!events)
            return std::unexpected(events.error());
        const auto dataset = (*estimator)->acquire_dataset();
        if (!dataset)
            return std::unexpected(dataset.error());
        const auto snapshot = pose_pipeline::publish(**dataset, *events, stop);
        if (!snapshot)
            return std::unexpected(snapshot.error());
        const auto& data = **dataset;
        const auto prepared =
            pose_pipeline::persist({*snapshot,
                                    {data.source_sha256, data.decode_completion_sha256,
                                     data.pose_completion_sha256, data.journal_sha256}},
                                   map_directory, stop);
        if (!prepared)
            return std::unexpected(prepared.error());
        if (log)
            log("Fresh COLMAP poses published durably at NEW_JOB.map. Scale arbitrary; "
                "geometry unavailable.\n");
        return prepared->store->acquire_snapshot();
    } catch (const std::bad_alloc&) {
        return fail(error_code::RESOURCE_EXHAUSTED, "Batch pose command allocation failed");
    } catch (const std::exception& exception) {
        return fail(error_code::INVALID_DATA, exception.what());
    }
}
} // namespace world_explorer
