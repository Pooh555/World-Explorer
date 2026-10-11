#include "world_explorer/map_store/map_store.hpp"
#include "world_explorer/core/hash.hpp"
#include <bit>
#include <limits>

namespace world_explorer {
namespace {
std::string transaction_hash(const map_transaction& transaction) {
    std::vector<std::byte> bytes;
    const auto add = [&](std::uint64_t value) {
        for (int i = 0; i < 8; ++i) {
            bytes.push_back(static_cast<std::byte>(value & 255ULL));
            value >>= 8U;
        }
    };
    const auto transform = [&](const similarity_transform& value) {
        for (double v :
             {value.rotation.w, value.rotation.x, value.rotation.y, value.rotation.z,
              value.translation.x, value.translation.y, value.translation.z, value.scale})
            add(std::bit_cast<std::uint64_t>(v));
    };
    add(transaction.epoch.value);
    add(transaction.base_revision.value);
    add(transaction.new_revision.value);
    add(transaction.calibrations.size());
    for (const auto& calibration : transaction.calibrations) {
        add(calibration.id.value);
        add(calibration.width);
        add(calibration.height);
        for (double value : {calibration.camera.fx, calibration.camera.fy, calibration.camera.cx,
                             calibration.camera.cy})
            add(std::bit_cast<std::uint64_t>(value));
        add(static_cast<std::uint64_t>(calibration.model));
        for (double value : calibration.distortion)
            add(std::bit_cast<std::uint64_t>(value));
        for (double value : calibration.image_transform)
            add(std::bit_cast<std::uint64_t>(value));
    }
    add(transaction.observations.size());
    for (const auto& observation : transaction.observations) {
        add(observation.id.value);
        add(observation.capture.value);
        add(observation.calibration.value);
        add(static_cast<std::uint64_t>(observation.timestamp.pts));
        add(static_cast<std::uint64_t>(observation.timestamp.time_base_numerator));
        add(static_cast<std::uint64_t>(observation.timestamp.time_base_denominator));
        add(static_cast<std::uint64_t>(observation.capture_time.count()));
        add(observation.image_sha256.size());
        for (char value : observation.image_sha256)
            bytes.push_back(static_cast<std::byte>(value));
    }
    add(transaction.poses.size());
    for (const auto& pose : transaction.poses) {
        add(pose.frame.value);
        add(pose.submap.value);
        transform(pose.submap_from_camera);
        add(static_cast<std::uint64_t>(pose.capture_time.count()));
        add(pose.revision.value);
        add(static_cast<std::uint64_t>(pose.tracking));
        add(pose.covariance.has_value());
        if (pose.covariance)
            for (double value : *pose.covariance)
                add(std::bit_cast<std::uint64_t>(value));
    }
    add(transaction.alignments.size());
    for (const auto& alignment : transaction.alignments) {
        add(alignment.submap.value);
        add(alignment.revision.value);
        transform(alignment.world_from_submap);
    }
    add(transaction.deleted_frames.size());
    for (auto frame : transaction.deleted_frames)
        add(frame.value);
    return sha256(bytes);
}
} // namespace
map_store::map_store(map_store_limits limits)
    : limits_(limits), current_(std::make_shared<map_state>()) {}
map_snapshot map_store::acquire_snapshot() const {
    std::lock_guard lock(mutex_);
    return current_;
}
result<map_revision_id> map_store::commit(const map_transaction& transaction) {
    std::lock_guard lock(mutex_);
    if (transaction.operation.value == 0)
        return fail(error_code::INVALID_ARGUMENT, "map operation ID is required");
    if (transaction.observations.size() > limits_.max_frames ||
        transaction.poses.size() > limits_.max_frames ||
        transaction.deleted_frames.size() > limits_.max_frames ||
        transaction.calibrations.size() > limits_.max_calibrations ||
        transaction.alignments.size() > limits_.max_submaps)
        return fail(error_code::RESOURCE_EXHAUSTED, "map transaction exceeds budget");
    for (const auto& observation : transaction.observations)
        if (observation.image_sha256.size() != 64 ||
            observation.image_sha256.find_first_not_of("0123456789abcdef") != std::string::npos)
            return fail(error_code::INVALID_DATA, "observation needs SHA-256 image provenance");
    const auto digest = transaction_hash(transaction);
    for (const auto& prior : operations_)
        if (prior.operation == transaction.operation) {
            if (prior.digest == digest)
                return prior.revision;
            return fail(error_code::DUPLICATE_OPERATION,
                        "map operation ID reused with different content");
        }
    if (transaction.epoch != current_->epoch)
        return fail(error_code::WRONG_EPOCH, "map epoch mismatch");
    if (transaction.base_revision != current_->revision)
        return fail(error_code::WRONG_BASE, "map base mismatch; request snapshot");
    if (transaction.base_revision.value == std::numeric_limits<std::uint64_t>::max() ||
        transaction.new_revision.value != transaction.base_revision.value + 1)
        return fail(error_code::INVALID_ARGUMENT, "map revision must advance exactly once");
    auto next = std::make_shared<map_state>(*current_);
    next->invalidated_frames.clear();
    for (const auto& calibration : transaction.calibrations) {
        if (!valid(calibration))
            return fail(error_code::INVALID_DATA, "invalid camera calibration");
        if (next->calibrations.contains(calibration.id))
            return fail(error_code::INVALID_DATA, "calibration IDs are immutable; create a new ID");
        next->calibrations.emplace(calibration.id, calibration);
    }
    for (const auto& observation : transaction.observations) {
        if (observation.id.value == 0 || observation.capture.value == 0 ||
            !next->calibrations.contains(observation.calibration) ||
            observation.timestamp.time_base_numerator <= 0 ||
            observation.timestamp.time_base_denominator <= 0 ||
            observation.capture_time.count() < 0)
            return fail(error_code::INVALID_DATA,
                        "invalid observation identity, calibration or timestamp");
        if (next->observations.contains(observation.id))
            return fail(error_code::INVALID_DATA, "observation ID already exists");
        next->observations.emplace(observation.id, observation);
    }
    std::set<submap_id> aligned;
    for (const auto& alignment : transaction.alignments) {
        if (alignment.submap.value == 0 || alignment.revision != transaction.new_revision ||
            !valid(alignment.world_from_submap) || !aligned.insert(alignment.submap).second)
            return fail(error_code::INVALID_DATA, "invalid or duplicate map alignment");
        if (next->world_from_submap.contains(alignment.submap))
            for (const auto& [frame, pose] : next->poses)
                if (pose.submap == alignment.submap)
                    next->invalidated_frames.insert(frame);
        next->world_from_submap[alignment.submap] = alignment.world_from_submap;
    }
    std::set<frame_id> changed;
    for (const auto& pose : transaction.poses) {
        if (!next->observations.contains(pose.frame) ||
            !next->world_from_submap.contains(pose.submap) || !valid(pose.submap_from_camera) ||
            std::abs(pose.submap_from_camera.scale - 1) > 1e-12 ||
            pose.revision != transaction.new_revision || !changed.insert(pose.frame).second)
            return fail(error_code::INVALID_DATA, "invalid pose dependency, transform or revision");
        if (pose.capture_time != next->observations.at(pose.frame).capture_time)
            return fail(error_code::INVALID_DATA, "Pose capture time differs from its observation");
        if (pose.covariance)
            for (double value : *pose.covariance)
                if (!std::isfinite(value))
                    return fail(error_code::INVALID_DATA, "nonfinite pose covariance");
        if (next->poses.contains(pose.frame))
            next->invalidated_frames.insert(pose.frame);
        next->poses[pose.frame] = pose;
    }
    for (auto frame : transaction.deleted_frames) {
        if (!changed.insert(frame).second || !next->observations.erase(frame))
            return fail(error_code::NOT_FOUND, "deleted observation missing or changed twice");
        next->poses.erase(frame);
        next->invalidated_frames.insert(frame);
    }
    if (next->observations.size() > limits_.max_frames ||
        next->calibrations.size() > limits_.max_calibrations ||
        next->world_from_submap.size() > limits_.max_submaps)
        return fail(error_code::RESOURCE_EXHAUSTED, "map storage budget exceeded");
    next->revision = transaction.new_revision;
    operations_.push_back({transaction.operation, digest, transaction.new_revision});
    current_ = std::move(next);
    while (operations_.size() > limits_.remembered_operations)
        operations_.pop_front();
    return transaction.new_revision;
}
result<void> map_store::reset(world_epoch epoch) {
    std::lock_guard lock(mutex_);
    if (epoch.value <= current_->epoch.value)
        return fail(error_code::WRONG_EPOCH, "reset requires newer map epoch");
    auto next = std::make_shared<map_state>();
    next->epoch = epoch;
    current_ = std::move(next);
    operations_.clear();
    return {};
}
} // namespace world_explorer
