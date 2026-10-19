#include "pose_pipeline.hpp"

#include "world_explorer/core/hash.hpp"

namespace world_explorer::pose_pipeline {

Result<std::vector<PoseEvent>>
consume(FrameSource& source, PoseEstimator& estimator, const std::stop_token& stop) {
    std::stop_callback forward(stop, [&] {
        estimator.request_stop();
    });
    std::vector<PoseEvent> collected;
    bool finished {};
    const auto K_DRAIN = [&]() -> Result<std::size_t> {
        auto events = estimator.drain_events({ 2 });
        if (!events) {
            return std::unexpected(events.error());
        }
        if (events->size() > 2 || collected.size() + events->size() > 128) {
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "Selected-frame pose event budget exceeded");
        }
        for (auto& event : *events) {
            if (std::holds_alternative<EstimatorFinished>(event)) {
                finished = true;
            }
            collected.push_back(std::move(event));
        }
        return events->size();
    };
    std::uint64_t operation {};
    CaptureId capture;
    while (true) {
        auto event = source.read_next(stop);
        if (!event) {
            return std::unexpected(event.error());
        }
        if (std::holds_alternative<SourceEnd>(*event)) {
            break;
        }
        if (const auto* reset = std::get_if<SourceReset>(&*event)) {
            if (capture.m_value || reset->m_epoch.m_value != 0 || !reset->m_capture.m_value) {
                return fail(ErrorCode::INVALID_DATA,
                            "Unexpected reset during selected-frame poses");
            }
            capture = reset->m_capture;
            continue;
        }
        auto* frame = std::get_if<FramePacket>(&*event);
        if (!frame || !capture.m_value || frame->m_capture != capture || operation >= 120) {
            return fail(ErrorCode::INVALID_DATA,
                        "Unexpected source event during selected-frame poses");
        }
        FrameObservation observation { { ++operation }, std::move(*frame) };
        while (true) {
            const auto K_ADMITTED = estimator.submit(observation);
            if (K_ADMITTED) {
                break;
            }
            if (K_ADMITTED.error().m_code != ErrorCode::WOULD_BLOCK) {
                return std::unexpected(K_ADMITTED.error());
            }
            const auto K_COUNT = K_DRAIN();
            if (!K_COUNT) {
                return std::unexpected(K_COUNT.error());
            }
            if (*K_COUNT == 0) {
                return fail(ErrorCode::BACKEND_FAILURE, "Pose admission made no progress");
            }
        }
    }
    const auto K_CLOSED = estimator.finish();
    if (!K_CLOSED) {
        return std::unexpected(K_CLOSED.error());
    }
    while (!finished) {
        const auto K_COUNT = K_DRAIN();
        if (!K_COUNT) {
            return std::unexpected(K_COUNT.error());
        }
        if (*K_COUNT == 0) {
            return fail(ErrorCode::BACKEND_FAILURE, "Pose estimator missing terminal event");
        }
    }
    if (stop.stop_requested()) {
        return fail(ErrorCode::CANCELLED, "Pose processing cancelled before map commit");
    }
    return collected;
}

Result<MapSnapshot> publish(const CompletedPoseDataset& dataset,
                            const std::vector<PoseEvent>& events,
                            const std::stop_token& stop) {
    MapTransaction transaction;
    transaction.m_epoch = dataset.m_epoch;
    transaction.m_new_revision = dataset.m_revision;
    transaction.m_operation = { 1 };
    transaction.m_calibrations = dataset.m_calibrations;
    transaction.m_observations = dataset.m_observations;
    for (const auto& event : events) {
        if (const auto* pose = std::get_if<PoseEstimate>(&event)) {
            transaction.m_poses.push_back(*pose);
        }
        if (const auto* alignment = std::get_if<MapAlignment>(&event)) {
            transaction.m_alignments.push_back(*alignment);
        }
    }
    if (transaction.m_poses.size() != dataset.m_poses.size()) {
        return fail(ErrorCode::INVALID_DATA, "Pose event inventory differs from dataset");
    }
    MapStore maps;
    const auto K_COMMITTED = maps.commit(transaction, stop);
    if (!K_COMMITTED) {
        return std::unexpected(K_COMMITTED.error());
    }
    return maps.acquire_snapshot();
}

Result<CanonicalPoseAsset> canonical(const CompletedPoseDataset& dataset,
                                     const std::stop_token& stop) {
    std::vector<PoseEvent> events;
    events.emplace_back(MapAlignment { dataset.m_submap, dataset.m_revision, {} });
    for (const auto& pose : dataset.m_poses) {
        events.emplace_back(pose);
    }
    const auto K_SNAPSHOT = publish(dataset, events, stop);
    if (!K_SNAPSHOT) {
        return std::unexpected(K_SNAPSHOT.error());
    }
    return CanonicalPoseAsset { *K_SNAPSHOT,
                               { dataset.m_source_sha256,
                                dataset.m_decode_completion_sha256,
                                dataset.m_pose_completion_sha256,
                                dataset.m_journal_sha256 } };
}

Result<PreparedPoseMap> persist(const CanonicalPoseAsset& asset,
                                const std::filesystem::path& directory,
                                const std::stop_token& stop) {
    if (directory.empty()) {
        return fail(ErrorCode::INVALID_ARGUMENT, "Pose preparation requires a map directory");
    }
    const auto K_BYTES = encode_pose_asset(asset, {}, stop);
    if (!K_BYTES) {
        return std::unexpected(K_BYTES.error());
    }
    const auto K_HASH = sha256(*K_BYTES);
    auto store =
        MapStore::open_persistent({ .m_directory = directory, .m_create_if_missing = true }, stop);
    if (!store) {
        return std::unexpected(store.error());
    }
    const auto K_PRIOR = (*store)->acquire_snapshot();
    if (K_PRIOR->m_epoch.m_value || K_PRIOR->m_revision.m_value > 1 ||
        (K_PRIOR->m_revision.m_value == 1 && K_PRIOR->m_initial_pose_asset_sha256 != K_HASH)) {
        return fail(
            ErrorCode::RESYNC_REQUIRED,
            "Pose map has a different dependency or revision; select a fresh map directory");
    }
    if (K_PRIOR->m_revision.m_value == 1) {
        const auto K_RETAINED = encode_pose_asset({ K_PRIOR, asset.m_provenance }, {}, stop);
        if (!K_RETAINED) {
            return std::unexpected(K_RETAINED.error());
        }
        if (*K_RETAINED != *K_BYTES) {
            return fail(ErrorCode::RESYNC_REQUIRED,
                        "Retained pose map differs from its initial solve");
        }
    }
    const auto K_IMPORTED = (*store)->import_pose_asset(asset, { 1 }, stop);
    if (!K_IMPORTED) {
        return std::unexpected(K_IMPORTED.error());
    }
    return PreparedPoseMap { std::move(*store), asset.m_provenance, K_HASH };
}

}  // namespace world_explorer::pose_pipeline
