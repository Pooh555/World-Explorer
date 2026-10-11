#include "pose_pipeline.hpp"

namespace world_explorer::pose_pipeline {
result<std::vector<pose_event>> consume(frame_source& source, pose_estimator& estimator,
                                        const std::stop_token& stop) {
    std::stop_callback forward(stop, [&] { estimator.request_stop(); });
    std::vector<pose_event> collected;
    bool finished{};
    const auto drain = [&]() -> result<std::size_t> {
        auto events = estimator.drain_events({2});
        if (!events)
            return std::unexpected(events.error());
        if (events->size() > 2 || collected.size() + events->size() > 128)
            return fail(error_code::RESOURCE_EXHAUSTED,
                        "Selected-frame pose event budget exceeded");
        for (auto& event : *events) {
            if (std::holds_alternative<estimator_finished>(event))
                finished = true;
            collected.push_back(std::move(event));
        }
        return events->size();
    };
    std::uint64_t operation{};
    capture_id capture;
    while (true) {
        auto event = source.read_next(stop);
        if (!event)
            return std::unexpected(event.error());
        if (std::holds_alternative<source_end>(*event))
            break;
        if (const auto* reset = std::get_if<source_reset>(&*event)) {
            if (capture.value || reset->epoch.value != 0 || !reset->capture.value)
                return fail(error_code::INVALID_DATA,
                            "Unexpected reset during selected-frame poses");
            capture = reset->capture;
            continue;
        }
        auto* frame = std::get_if<frame_packet>(&*event);
        if (!frame || !capture.value || frame->capture != capture || operation >= 120)
            return fail(error_code::INVALID_DATA,
                        "Unexpected source event during selected-frame poses");
        frame_observation observation{{++operation}, std::move(*frame)};
        while (true) {
            const auto admitted = estimator.submit(observation);
            if (admitted)
                break;
            if (admitted.error().code != error_code::WOULD_BLOCK)
                return std::unexpected(admitted.error());
            const auto count = drain();
            if (!count)
                return std::unexpected(count.error());
            if (*count == 0)
                return fail(error_code::BACKEND_FAILURE, "Pose admission made no progress");
        }
    }
    const auto closed = estimator.finish();
    if (!closed)
        return std::unexpected(closed.error());
    while (!finished) {
        const auto count = drain();
        if (!count)
            return std::unexpected(count.error());
        if (*count == 0)
            return fail(error_code::BACKEND_FAILURE, "Pose estimator missing terminal event");
    }
    if (stop.stop_requested())
        return fail(error_code::CANCELLED, "Pose processing cancelled before map commit");
    return collected;
}
result<map_snapshot> publish(const completed_pose_dataset& dataset,
                             const std::vector<pose_event>& events) {
    map_transaction transaction;
    transaction.epoch = dataset.epoch;
    transaction.new_revision = dataset.revision;
    transaction.operation = {1};
    transaction.calibrations = dataset.calibrations;
    transaction.observations = dataset.observations;
    for (const auto& event : events) {
        if (const auto* pose = std::get_if<pose_estimate>(&event))
            transaction.poses.push_back(*pose);
        if (const auto* alignment = std::get_if<map_alignment>(&event))
            transaction.alignments.push_back(*alignment);
    }
    if (transaction.poses.size() != dataset.poses.size())
        return fail(error_code::INVALID_DATA, "Pose event inventory differs from dataset");
    map_store maps;
    const auto committed = maps.commit(transaction);
    if (!committed)
        return std::unexpected(committed.error());
    return maps.acquire_snapshot();
}
} // namespace world_explorer::pose_pipeline
