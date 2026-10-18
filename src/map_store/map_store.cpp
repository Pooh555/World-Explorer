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

Result<std::string> transaction_hash(const MapTransaction& transaction,
                                     const std::optional<std::string>& initial_asset,
                                     const std::stop_token& stop) {
    std::vector<std::byte> bytes;
    const auto K_ADD = [&](std::uint64_t value) {
        for (int i { 0 }; i < 8; ++i) {
            bytes.push_back(static_cast<std::byte>(value & 255ULL));
            value >>= 8U;
        }
    };
    const auto K_TRANSFORM = [&](const SimilarityTransform& value) {
        for (double v : {value.m_rotation.m_w,
                         value.m_rotation.m_x,
                         value.m_rotation.m_y,
                         value.m_rotation.m_z,
                         value.m_translation.m_x,
                         value.m_translation.m_y,
                         value.m_translation.m_z,
                         value.m_scale})
            K_ADD(std::bit_cast<std::uint64_t>(v));
    };
    K_ADD(initial_asset.has_value());
    if (initial_asset)
        for (char value : *initial_asset) bytes.push_back(static_cast<std::byte>(value));
    K_ADD(transaction.m_epoch.m_value);
    K_ADD(transaction.m_base_revision.m_value);
    K_ADD(transaction.m_new_revision.m_value);
    K_ADD(transaction.m_calibrations.size());
    for (const auto& calibration : transaction.m_calibrations) {
        K_ADD(calibration.m_id.m_value);
        K_ADD(calibration.m_width);
        K_ADD(calibration.m_height);
        for (double value : {calibration.m_camera.m_fx,
                             calibration.m_camera.m_fy,
                             calibration.m_camera.m_cx,
                             calibration.m_camera.m_cy})
            K_ADD(std::bit_cast<std::uint64_t>(value));
        K_ADD(static_cast<std::uint64_t>(calibration.m_model));
        for (double value : calibration.m_distortion) K_ADD(std::bit_cast<std::uint64_t>(value));
        for (double value : calibration.m_image_transform)
            K_ADD(std::bit_cast<std::uint64_t>(value));
    }
    K_ADD(transaction.m_observations.size());
    for (const auto& observation : transaction.m_observations) {
        if (stop.stop_requested()) return fail(ErrorCode::CANCELLED, "Map transaction cancelled");
        K_ADD(observation.m_id.m_value);
        K_ADD(observation.m_capture.m_value);
        K_ADD(observation.m_calibration.m_value);
        K_ADD(static_cast<std::uint64_t>(observation.m_timestamp.m_pts));
        K_ADD(static_cast<std::uint64_t>(observation.m_timestamp.m_time_base_numerator));
        K_ADD(static_cast<std::uint64_t>(observation.m_timestamp.m_time_base_denominator));
        K_ADD(static_cast<std::uint64_t>(observation.m_capture_time.count()));
        K_ADD(observation.m_image_sha256.size());
        for (char value : observation.m_image_sha256)
            bytes.push_back(static_cast<std::byte>(value));
    }
    K_ADD(transaction.m_poses.size());
    for (const auto& pose : transaction.m_poses) {
        K_ADD(pose.m_frame.m_value);
        K_ADD(pose.m_submap.m_value);
        K_TRANSFORM(pose.m_submap_from_camera);
        K_ADD(static_cast<std::uint64_t>(pose.m_capture_time.count()));
        K_ADD(pose.m_revision.m_value);
        K_ADD(static_cast<std::uint64_t>(pose.m_tracking));
        K_ADD(pose.m_covariance.has_value());
        if (pose.m_covariance)
            for (double value : *pose.m_covariance) K_ADD(std::bit_cast<std::uint64_t>(value));
    }
    K_ADD(transaction.m_alignments.size());
    for (const auto& alignment : transaction.m_alignments) {
        K_ADD(alignment.m_submap.m_value);
        K_ADD(alignment.m_revision.m_value);
        K_TRANSFORM(alignment.m_world_from_submap);
    }
    K_ADD(transaction.m_deleted_frames.size());
    for (auto frame : transaction.m_deleted_frames) K_ADD(frame.m_value);
    return sha256(bytes);
}

}  // namespace

MapStore::MapStore(MapStoreLimits limits)
    : m_limits(limits), m_current(std::make_shared<MapState>()) {
}

MapSnapshot MapStore::acquire_snapshot() const {
    std::lock_guard lock(m_mutex);
    return m_current;
}

MapStore::~MapStore() = default;

Result<std::unique_ptr<MapStore>> MapStore::open_persistent(PersistentMapConfig config,
                                                            const std::stop_token& stop) {
    return MapPersistence::open(std::move(config), stop);
}

Result<MapRevisionId> MapStore::commit(const MapTransaction& transaction,
                                       const std::stop_token& stop) {
    std::lock_guard lock(m_mutex);
    try {
        return commit_locked(transaction, stop);
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Map transaction allocation failed");
    } catch (const std::exception& e) {
        return fail(ErrorCode::IO_ERROR, std::string("Map commit failed: ") + e.what());
    }
}

Result<MapRevisionId> MapStore::commit_locked(const MapTransaction& transaction,
                                              const std::stop_token& stop,
                                              std::optional<std::string> initial_asset) {
    if (stop.stop_requested()) return fail(ErrorCode::CANCELLED, "Map transaction cancelled");
    if (m_persistence && m_persistence->requires_reopen())
        return fail(ErrorCode::RESYNC_REQUIRED, "Persistent map requires close/reopen");
    if (!m_limits.m_remembered_operations || m_limits.m_remembered_operations > 65536)
        return fail(ErrorCode::INVALID_ARGUMENT, "Map retry window must be 1..65536 receipts");
    if (transaction.m_operation.m_value == 0)
        return fail(ErrorCode::INVALID_ARGUMENT, "map operation ID is required");
    if (transaction.m_observations.size() > m_limits.m_max_frames ||
        transaction.m_poses.size() > m_limits.m_max_frames ||
        transaction.m_deleted_frames.size() > m_limits.m_max_frames ||
        transaction.m_calibrations.size() > m_limits.m_max_calibrations ||
        transaction.m_alignments.size() > m_limits.m_max_submaps)
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "map transaction exceeds budget");
    for (const auto& observation : transaction.m_observations)
        if (observation.m_image_sha256.size() != 64 ||
            observation.m_image_sha256.find_first_not_of("0123456789abcdef") != std::string::npos)
            return fail(ErrorCode::INVALID_DATA, "observation needs SHA-256 image provenance");
    const auto K_DIGEST = transaction_hash(transaction, initial_asset, stop);
    if (!K_DIGEST) return std::unexpected(K_DIGEST.error());
    for (const auto& prior : m_operations)
        if (prior.m_operation == transaction.m_operation) {
            if (prior.m_digest == *K_DIGEST) return prior.m_revision;
            return fail(ErrorCode::DUPLICATE_OPERATION,
                        "map operation ID reused with different content");
        }
    if (transaction.m_epoch != m_current->m_epoch)
        return fail(ErrorCode::WRONG_EPOCH, "map epoch mismatch");
    if (transaction.m_base_revision != m_current->m_revision)
        return fail(ErrorCode::WRONG_BASE, "map base mismatch; request snapshot");
    if (transaction.m_base_revision.m_value == std::numeric_limits<std::uint64_t>::max() ||
        transaction.m_new_revision.m_value != transaction.m_base_revision.m_value + 1)
        return fail(ErrorCode::INVALID_ARGUMENT, "map revision must advance exactly once");
    auto next = std::make_shared<MapState>(*m_current);
    if (initial_asset) {
        if (m_current->m_revision.m_value || m_current->m_initial_pose_asset_sha256 ||
            !map_codec::digest_valid(*initial_asset))
            return fail(ErrorCode::INVALID_ARGUMENT,
                        "Initial pose dependency requires a fresh map");
        next->m_initial_pose_asset_sha256 = std::move(initial_asset);
    }
    next->m_invalidated_frames.clear();
    for (const auto& calibration : transaction.m_calibrations) {
        if (!valid(calibration)) return fail(ErrorCode::INVALID_DATA, "invalid camera calibration");
        if (next->m_calibrations.contains(calibration.m_id))
            return fail(ErrorCode::INVALID_DATA, "calibration IDs are immutable; create a new ID");
        next->m_calibrations.emplace(calibration.m_id, calibration);
    }
    for (const auto& observation : transaction.m_observations) {
        if (stop.stop_requested()) return fail(ErrorCode::CANCELLED, "Map transaction cancelled");
        if (observation.m_id.m_value == 0 || observation.m_capture.m_value == 0 ||
            !next->m_calibrations.contains(observation.m_calibration) ||
            observation.m_timestamp.m_time_base_numerator <= 0 ||
            observation.m_timestamp.m_time_base_denominator <= 0 ||
            observation.m_capture_time.count() < 0)
            return fail(ErrorCode::INVALID_DATA,
                        "invalid observation identity, calibration or timestamp");
        if (next->m_observations.contains(observation.m_id))
            return fail(ErrorCode::INVALID_DATA, "observation ID already exists");
        next->m_observations.emplace(observation.m_id, observation);
    }
    std::set<SubmapId> aligned;
    for (const auto& alignment : transaction.m_alignments) {
        if (alignment.m_submap.m_value == 0 || alignment.m_revision != transaction.m_new_revision ||
            !valid(alignment.m_world_from_submap) || !aligned.insert(alignment.m_submap).second)
            return fail(ErrorCode::INVALID_DATA, "invalid or duplicate map alignment");
        if (next->m_world_from_submap.contains(alignment.m_submap))
            for (const auto& [K_FRAME, K_POSE] : next->m_poses)
                if (K_POSE.m_submap == alignment.m_submap)
                    next->m_invalidated_frames.insert(K_FRAME);
        next->m_world_from_submap[alignment.m_submap] = alignment.m_world_from_submap;
    }
    std::set<FrameId> changed;
    for (const auto& pose : transaction.m_poses) {
        if (stop.stop_requested()) return fail(ErrorCode::CANCELLED, "Map transaction cancelled");
        if (!next->m_observations.contains(pose.m_frame) ||
            !next->m_world_from_submap.contains(pose.m_submap) ||
            !valid(pose.m_submap_from_camera) ||
            std::abs(pose.m_submap_from_camera.m_scale - 1) > 1e-12 ||
            pose.m_revision != transaction.m_new_revision || !changed.insert(pose.m_frame).second)
            return fail(ErrorCode::INVALID_DATA, "invalid pose dependency, transform or revision");
        if (pose.m_capture_time != next->m_observations.at(pose.m_frame).m_capture_time)
            return fail(ErrorCode::INVALID_DATA, "Pose capture time differs from its observation");
        if (pose.m_covariance)
            for (double value : *pose.m_covariance)
                if (!std::isfinite(value))
                    return fail(ErrorCode::INVALID_DATA, "nonfinite pose covariance");
        if (next->m_poses.contains(pose.m_frame)) next->m_invalidated_frames.insert(pose.m_frame);
        next->m_poses[pose.m_frame] = pose;
    }
    for (auto frame : transaction.m_deleted_frames) {
        if (!changed.insert(frame).second || !next->m_observations.erase(frame))
            return fail(ErrorCode::NOT_FOUND, "deleted observation missing or changed twice");
        next->m_poses.erase(frame);
        next->m_invalidated_frames.insert(frame);
    }
    if (next->m_observations.size() > m_limits.m_max_frames ||
        next->m_calibrations.size() > m_limits.m_max_calibrations ||
        next->m_world_from_submap.size() > m_limits.m_max_submaps)
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "map storage budget exceeded");
    next->m_revision = transaction.m_new_revision;
    const auto K_VALID = map_codec::validate_state(*next, {{256uz * 1024 * 1024}, m_limits}, stop);
    if (!K_VALID) return std::unexpected(K_VALID.error());
    auto operations = m_operations;
    operations.push_back({transaction.m_operation, *K_DIGEST, transaction.m_new_revision});
    while (operations.size() > m_limits.m_remembered_operations) operations.pop_front();
    if (m_persistence) {
        const auto K_PUBLISHED = m_persistence->publish(*next, operations, stop);
        if (!K_PUBLISHED) return std::unexpected(K_PUBLISHED.error());
    }
    m_operations.swap(operations);
    m_current = std::move(next);
    return transaction.m_new_revision;
}

Result<void> MapStore::reset(WorldEpoch epoch, const std::stop_token& stop) {
    std::lock_guard lock(m_mutex);
    try {
        if (stop.stop_requested()) return fail(ErrorCode::CANCELLED, "Map reset cancelled");
        if (m_persistence && m_persistence->requires_reopen())
            return fail(ErrorCode::RESYNC_REQUIRED, "Persistent map requires close/reopen");
        if (epoch.m_value <= m_current->m_epoch.m_value)
            return fail(ErrorCode::WRONG_EPOCH, "reset requires newer map epoch");
        auto next = std::make_shared<MapState>();
        next->m_epoch = epoch;
        std::deque<Remembered> operations;
        if (m_persistence) {
            const auto K_PUBLISHED = m_persistence->publish(*next, operations, stop);
            if (!K_PUBLISHED) return std::unexpected(K_PUBLISHED.error());
        }
        m_current = std::move(next);
        m_operations.swap(operations);
        return {};
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Map reset allocation failed");
    } catch (const std::exception& e) {
        return fail(ErrorCode::IO_ERROR, std::string("Map reset failed: ") + e.what());
    }
}

Result<MapRevisionId> MapStore::import_pose_asset(const std::filesystem::path& path,
                                                  const std::string& hash,
                                                  OperationId operation,
                                                  const std::stop_token& stop) {
    const auto K_BYTES = map_files::read_bytes(path, 256uz * 1024 * 1024, stop);
    if (!K_BYTES) return std::unexpected(K_BYTES.error());
    return import_encoded_pose_asset(*K_BYTES, hash, operation, stop);
}

Result<MapRevisionId> MapStore::import_pose_asset(const CanonicalPoseAsset& asset,
                                                  OperationId operation,
                                                  const std::stop_token& stop) {
    try {
        const auto K_BYTES = encode_pose_asset(asset, {}, stop);
        if (!K_BYTES) return std::unexpected(K_BYTES.error());
        return import_encoded_pose_asset(*K_BYTES, sha256(*K_BYTES), operation, stop);
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Map import allocation failed");
    } catch (const std::exception& e) {
        return fail(ErrorCode::IO_ERROR, std::string("Map import failed: ") + e.what());
    }
}

Result<MapRevisionId> MapStore::import_encoded_pose_asset(std::span<const std::byte> bytes,
                                                          const std::string& hash,
                                                          OperationId operation,
                                                          const std::stop_token& stop) {
    std::lock_guard lock(m_mutex);
    try {
        if (!m_persistence)
            return fail(ErrorCode::UNSUPPORTED_OPERATION,
                        "Canonical dependency import requires a persistent map");
        if (m_persistence->requires_reopen())
            return fail(ErrorCode::RESYNC_REQUIRED, "Persistent map requires close/reopen");
        const auto K_LOADED = decode_pose_asset(bytes, hash, {}, stop);
        if (!K_LOADED) return std::unexpected(K_LOADED.error());
        const auto& map = *K_LOADED->m_snapshot;
        if (map.m_epoch.m_value || map.m_revision.m_value != 1 || !map.m_invalidated_frames.empty())
            return fail(ErrorCode::UNSUPPORTED_OPERATION,
                        "Initial import requires epoch zero/revision one canonical poses");
        MapTransaction transaction;
        transaction.m_operation = operation;
        transaction.m_new_revision = { 1 };
        for (const auto& [K_ID, K_RECORD] : map.m_calibrations) {
            (void)K_ID;
            transaction.m_calibrations.push_back(K_RECORD);
        }
        for (const auto& [K_ID, K_RECORD] : map.m_observations) {
            (void)K_ID;
            transaction.m_observations.push_back(K_RECORD);
        }
        for (const auto& [K_ID, K_POSE] : map.m_poses) {
            (void)K_ID;
            transaction.m_poses.push_back(K_POSE);
        }
        for (const auto& [K_ID, K_TRANSFORM] : map.m_world_from_submap)
            transaction.m_alignments.push_back({K_ID, {1}, K_TRANSFORM});
        const auto K_INSTALLED = m_persistence->install_pose_asset(bytes, hash, stop);
        if (!K_INSTALLED) return std::unexpected(K_INSTALLED.error());
        return commit_locked(transaction, stop, hash);
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Map import allocation failed");
    } catch (const std::exception& e) {
        return fail(ErrorCode::IO_ERROR, std::string("Map import failed: ") + e.what());
    }
}

}  // namespace world_explorer
