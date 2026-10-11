#include "world_explorer/application/pose_replay.hpp"
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
        std::stop_callback forward(stop, [&] { (*estimator)->request_stop(); });
        const auto started = (*estimator)->start({{std::size_t{64} * 1024 * 1024}, 1});
        if (!started)
            return std::unexpected(started.error());
        map_transaction transaction;
        transaction.epoch = (*dataset)->epoch;
        transaction.new_revision = (*dataset)->revision;
        transaction.operation = {1};
        transaction.calibrations = (*dataset)->calibrations;
        transaction.observations = (*dataset)->observations;
        bool finished{};
        const auto drain = [&]() -> result<std::size_t> {
            const auto events = (*estimator)->drain_events({2});
            if (!events)
                return std::unexpected(events.error());
            for (const auto& event : *events) {
                if (const auto* pose = std::get_if<pose_estimate>(&event))
                    transaction.poses.push_back(*pose);
                if (const auto* alignment = std::get_if<map_alignment>(&event))
                    transaction.alignments.push_back(*alignment);
                if (std::holds_alternative<estimator_finished>(event))
                    finished = true;
            }
            return events->size();
        };
        std::uint64_t operation{};
        while (true) {
            auto event = (*source)->read_next(stop);
            if (!event)
                return std::unexpected(event.error());
            if (std::holds_alternative<source_end>(*event))
                break;
            if (const auto* reset = std::get_if<source_reset>(&*event)) {
                if (reset->epoch != (*dataset)->epoch ||
                    reset->capture != (*dataset)->observations.front().capture)
                    return fail(error_code::INVALID_DATA,
                                "Frame replay reset differs from pose dataset");
                continue;
            }
            auto* frame = std::get_if<frame_packet>(&*event);
            if (!frame)
                return fail(error_code::UNSUPPORTED_OPERATION,
                            "Unexpected frame-source event during pose replay");
            frame_observation observation{{++operation}, std::move(*frame)};
            while (true) {
                const auto admitted = (*estimator)->submit(observation);
                if (admitted)
                    break;
                if (admitted.error().code != error_code::WOULD_BLOCK)
                    return std::unexpected(admitted.error());
                const auto count = drain();
                if (!count)
                    return std::unexpected(count.error());
                if (*count == 0)
                    return fail(error_code::BACKEND_FAILURE,
                                "Pose replay made no progress under backpressure");
            }
        }
        const auto closed = (*estimator)->finish();
        if (!closed)
            return std::unexpected(closed.error());
        while (!finished) {
            const auto count = drain();
            if (!count)
                return std::unexpected(count.error());
            if (*count == 0)
                return fail(error_code::BACKEND_FAILURE, "Pose replay missing terminal event");
        }
        if (stop.stop_requested())
            return fail(error_code::CANCELLED, "Pose replay cancelled before map commit");
        if (transaction.poses.size() != (*dataset)->poses.size())
            return fail(error_code::INVALID_DATA, "Pose replay event inventory mismatch");
        map_store maps;
        const auto committed = maps.commit(transaction);
        if (!committed)
            return std::unexpected(committed.error());
        if (log)
            log("Published in-memory map snapshot from completed-pose replay. Scale arbitrary; "
                "geometry unavailable.\n");
        return maps.acquire_snapshot();
    } catch (const std::bad_alloc&) {
        return fail(error_code::RESOURCE_EXHAUSTED, "Pose replay command allocation failed");
    } catch (const std::exception& exception) {
        return fail(error_code::INVALID_DATA,
                    std::string("Pose replay command failed: ") + exception.what());
    }
}
} // namespace world_explorer
