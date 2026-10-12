#include "world_explorer/map_store/map_store.hpp"
#include "map_codec.hpp"
#include "map_files.hpp"
#include "map_persistence.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/map_store/pose_asset.hpp"
#include <bit>
#include <limits>

namespace world_explorer {
namespace {
result<std::string> transaction_hash(const map_transaction& transaction,
                                     const std::optional<std::string>& initial_asset,
                                     const std::stop_token& stop) {
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
    add(initial_asset.has_value());
    if (initial_asset)
        for (char value : *initial_asset)
            bytes.push_back(static_cast<std::byte>(value));
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
        if (stop.stop_requested())
            return fail(error_code::CANCELLED, "Map transaction cancelled");
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
map_store::~map_store() = default;
result<std::unique_ptr<map_store>> map_store::open_persistent(persistent_map_config config,
                                                              const std::stop_token& stop) {
    return map_persistence::open(std::move(config), stop);
}
result<map_revision_id> map_store::commit(const map_transaction& transaction,
                                          const std::stop_token& stop) {
    std::lock_guard lock(mutex_);
    try {
        return commit_locked(transaction, stop);
    } catch (const std::bad_alloc&) {
        return fail(error_code::RESOURCE_EXHAUSTED, "Map transaction allocation failed");
    } catch (const std::exception& e) {
        return fail(error_code::IO_ERROR, std::string("Map commit failed: ") + e.what());
    }
}
result<map_revision_id> map_store::commit_locked(const map_transaction& transaction,
                                                 const std::stop_token& stop,
                                                 std::optional<std::string> initial_asset) {
    if (stop.stop_requested())
        return fail(error_code::CANCELLED, "Map transaction cancelled");
    if (persistence_ && persistence_->requires_reopen())
        return fail(error_code::RESYNC_REQUIRED, "Persistent map requires close/reopen");
    if (!limits_.remembered_operations || limits_.remembered_operations > 65536)
        return fail(error_code::INVALID_ARGUMENT, "Map retry window must be 1..65536 receipts");
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
    const auto digest = transaction_hash(transaction, initial_asset, stop);
    if (!digest)
        return std::unexpected(digest.error());
    for (const auto& prior : operations_)
        if (prior.operation == transaction.operation) {
            if (prior.digest == *digest)
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
    if (initial_asset) {
        if (current_->revision.value || current_->initial_pose_asset_sha256 ||
            !map_codec::digest_valid(*initial_asset))
            return fail(error_code::INVALID_ARGUMENT,
                        "Initial pose dependency requires a fresh map");
        next->initial_pose_asset_sha256 = std::move(initial_asset);
    }
    next->invalidated_frames.clear();
    for (const auto& calibration : transaction.calibrations) {
        if (!valid(calibration))
            return fail(error_code::INVALID_DATA, "invalid camera calibration");
        if (next->calibrations.contains(calibration.id))
            return fail(error_code::INVALID_DATA, "calibration IDs are immutable; create a new ID");
        next->calibrations.emplace(calibration.id, calibration);
    }
    for (const auto& observation : transaction.observations) {
        if (stop.stop_requested())
            return fail(error_code::CANCELLED, "Map transaction cancelled");
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
        if (stop.stop_requested())
            return fail(error_code::CANCELLED, "Map transaction cancelled");
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
    const auto valid =
        map_codec::validate_state(*next, {{std::size_t{256} * 1024 * 1024}, limits_}, stop);
    if (!valid)
        return std::unexpected(valid.error());
    auto operations = operations_;
    operations.push_back({transaction.operation, *digest, transaction.new_revision});
    while (operations.size() > limits_.remembered_operations)
        operations.pop_front();
    if (persistence_) {
        const auto published = persistence_->publish(*next, operations, stop);
        if (!published)
            return std::unexpected(published.error());
    }
    operations_.swap(operations);
    current_ = std::move(next);
    return transaction.new_revision;
}
result<void> map_store::reset(world_epoch epoch, const std::stop_token& stop) {
    std::lock_guard lock(mutex_);
    try {
        if (stop.stop_requested())
            return fail(error_code::CANCELLED, "Map reset cancelled");
        if (persistence_ && persistence_->requires_reopen())
            return fail(error_code::RESYNC_REQUIRED, "Persistent map requires close/reopen");
        if (epoch.value <= current_->epoch.value)
            return fail(error_code::WRONG_EPOCH, "reset requires newer map epoch");
        auto next = std::make_shared<map_state>();
        next->epoch = epoch;
        std::deque<remembered> operations;
        if (persistence_) {
            const auto published = persistence_->publish(*next, operations, stop);
            if (!published)
                return std::unexpected(published.error());
        }
        current_ = std::move(next);
        operations_.swap(operations);
        return {};
    } catch (const std::bad_alloc&) {
        return fail(error_code::RESOURCE_EXHAUSTED, "Map reset allocation failed");
    } catch (const std::exception& e) {
        return fail(error_code::IO_ERROR, std::string("Map reset failed: ") + e.what());
    }
}
result<map_revision_id> map_store::import_pose_asset(const std::filesystem::path& path,
                                                     const std::string& hash,
                                                     operation_id operation,
                                                     const std::stop_token& stop) {
    const auto bytes = map_files::read_bytes(path, std::size_t{256} * 1024 * 1024, stop);
    if (!bytes)
        return std::unexpected(bytes.error());
    return import_encoded_pose_asset(*bytes, hash, operation, stop);
}
result<map_revision_id> map_store::import_pose_asset(const canonical_pose_asset& asset,
                                                     operation_id operation,
                                                     const std::stop_token& stop) {
    try {
        const auto bytes = encode_pose_asset(asset, {}, stop);
        if (!bytes)
            return std::unexpected(bytes.error());
        return import_encoded_pose_asset(*bytes, sha256(*bytes), operation, stop);
    } catch (const std::bad_alloc&) {
        return fail(error_code::RESOURCE_EXHAUSTED, "Map import allocation failed");
    } catch (const std::exception& e) {
        return fail(error_code::IO_ERROR, std::string("Map import failed: ") + e.what());
    }
}
result<map_revision_id> map_store::import_encoded_pose_asset(std::span<const std::byte> bytes,
                                                             const std::string& hash,
                                                             operation_id operation,
                                                             const std::stop_token& stop) {
    std::lock_guard lock(mutex_);
    try {
        if (!persistence_)
            return fail(error_code::UNSUPPORTED_OPERATION,
                        "Canonical dependency import requires a persistent map");
        if (persistence_->requires_reopen())
            return fail(error_code::RESYNC_REQUIRED, "Persistent map requires close/reopen");
        const auto loaded = decode_pose_asset(bytes, hash, {}, stop);
        if (!loaded)
            return std::unexpected(loaded.error());
        const auto& map = *loaded->snapshot;
        if (map.epoch.value || map.revision.value != 1 || !map.invalidated_frames.empty())
            return fail(error_code::UNSUPPORTED_OPERATION,
                        "Initial import requires epoch zero/revision one canonical poses");
        map_transaction transaction;
        transaction.operation = operation;
        transaction.new_revision = {1};
        for (const auto& [id, record] : map.calibrations) {
            (void)id;
            transaction.calibrations.push_back(record);
        }
        for (const auto& [id, record] : map.observations) {
            (void)id;
            transaction.observations.push_back(record);
        }
        for (const auto& [id, pose] : map.poses) {
            (void)id;
            transaction.poses.push_back(pose);
        }
        for (const auto& [id, transform] : map.world_from_submap)
            transaction.alignments.push_back({id, {1}, transform});
        const auto installed = persistence_->install_pose_asset(bytes, hash, stop);
        if (!installed)
            return std::unexpected(installed.error());
        return commit_locked(transaction, stop, hash);
    } catch (const std::bad_alloc&) {
        return fail(error_code::RESOURCE_EXHAUSTED, "Map import allocation failed");
    } catch (const std::exception& e) {
        return fail(error_code::IO_ERROR, std::string("Map import failed: ") + e.what());
    }
}
} // namespace world_explorer
