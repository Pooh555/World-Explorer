#include "world_explorer/application/pose_solve.hpp"
#include "pose_pipeline.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/reconstruction_adapters/decoded_frame_source.hpp"
#include <new>

namespace world_explorer {
result<map_snapshot> solve_selected_pose_map(pose_solve_request request,
                                             const std::stop_token& stop, const log_callback& log) {
    try {
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
        const auto snapshot = pose_pipeline::publish(**dataset, *events);
        if (snapshot && log)
            log("Fresh COLMAP poses published in memory. Scale arbitrary; geometry unavailable.\n");
        return snapshot;
    } catch (const std::bad_alloc&) {
        return fail(error_code::RESOURCE_EXHAUSTED, "Batch pose command allocation failed");
    } catch (const std::exception& exception) {
        return fail(error_code::INVALID_DATA, exception.what());
    }
}
} // namespace world_explorer
