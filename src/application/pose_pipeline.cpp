#include "pose_pipeline.hpp"
#include "world_explorer/core/hash.hpp"

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
                             const std::vector<pose_event>& events, const std::stop_token& stop) {
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
    const auto committed = maps.commit(transaction, stop);
    if (!committed)
        return std::unexpected(committed.error());
    return maps.acquire_snapshot();
}
result<canonical_pose_asset> canonical(const completed_pose_dataset& dataset,
                                       const std::stop_token& stop) {
    std::vector<pose_event> events;
    events.emplace_back(map_alignment{dataset.submap, dataset.revision, {}});
    for (const auto& pose : dataset.poses)
        events.emplace_back(pose);
    const auto snapshot = publish(dataset, events, stop);
    if (!snapshot)
        return std::unexpected(snapshot.error());
    return canonical_pose_asset{*snapshot,
                                {dataset.source_sha256, dataset.decode_completion_sha256,
                                 dataset.pose_completion_sha256, dataset.journal_sha256}};
}
result<prepared_pose_map> persist(const canonical_pose_asset& asset,
                                  const std::filesystem::path& directory,
                                  const std::stop_token& stop) {
    if (directory.empty())
        return fail(error_code::INVALID_ARGUMENT, "Pose preparation requires a map directory");
    const auto bytes = encode_pose_asset(asset, {}, stop);
    if (!bytes)
        return std::unexpected(bytes.error());
    const auto hash = sha256(*bytes);
    auto store =
        map_store::open_persistent({.directory = directory, .create_if_missing = true}, stop);
    if (!store)
        return std::unexpected(store.error());
    const auto prior = (*store)->acquire_snapshot();
    if (prior->epoch.value || prior->revision.value > 1 ||
        (prior->revision.value == 1 && prior->initial_pose_asset_sha256 != hash))
        return fail(
            error_code::RESYNC_REQUIRED,
            "Pose map has a different dependency or revision; select a fresh map directory");
    if (prior->revision.value == 1) {
        const auto retained = encode_pose_asset({prior, asset.provenance}, {}, stop);
        if (!retained)
            return std::unexpected(retained.error());
        if (*retained != *bytes)
            return fail(error_code::RESYNC_REQUIRED,
                        "Retained pose map differs from its initial solve");
    }
    const auto imported = (*store)->import_pose_asset(asset, {1}, stop);
    if (!imported)
        return std::unexpected(imported.error());
    return prepared_pose_map{std::move(*store), asset.provenance, hash};
}
} // namespace world_explorer::pose_pipeline
