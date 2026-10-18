#ifndef WORLD_EXPLORER_MAP_STORE_MAP_STORE_HPP
#define WORLD_EXPLORER_MAP_STORE_MAP_STORE_HPP

#include "world_explorer/reconstruction_api/map_records.hpp"
#include "world_explorer/reconstruction_api/pose_estimator.hpp"

#include <cstddef>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace world_explorer {

struct MapState {
    // Monocular map units are arbitrary until an explicit measured-scale
    // transaction is implemented and verified. Alignment scale alone is no proof.
    ScaleStatus m_scale { ScaleStatus::ARBITRARY };
    WorldEpoch m_epoch;
    MapRevisionId m_revision;
    std::map<CalibrationId, CalibrationRecord> m_calibrations;
    std::map<FrameId, ObservationRecord> m_observations;
    std::map<FrameId, PoseEstimate> m_poses;
    std::map<SubmapId, SimilarityTransform> m_world_from_submap;
    // Consumers use this set to invalidate appearance/geometry derived from
    // corrected or deleted observations before publishing a new scene revision.
    std::set<FrameId> m_invalidated_frames;
    // Initial canonical solve dependency, not the provenance of later corrections.
    std::optional<std::string> m_initial_pose_asset_sha256;
};

using MapSnapshot = std::shared_ptr<const MapState>;

struct MapTransaction {
    WorldEpoch m_epoch;
    MapRevisionId m_base_revision, m_new_revision;
    OperationId m_operation;
    std::vector<CalibrationRecord> m_calibrations;
    std::vector<ObservationRecord> m_observations;
    std::vector<PoseEstimate> m_poses;
    std::vector<MapAlignment> m_alignments;
    std::vector<FrameId> m_deleted_frames;
};

struct MapStoreLimits {
    std::size_t m_max_frames { 1000000uz };
    std::size_t m_max_calibrations { 4096uz };
    std::size_t m_max_submaps { 4096uz };
    std::size_t m_remembered_operations { 1024uz };
};

struct PersistentMapConfig {
    std::filesystem::path m_directory;
    MapStoreLimits m_records {};
    ByteBudget m_checkpoint_bytes { 256uz * 1024 * 1024 };
    // Explicit initialization only. Missing/corrupt manifests never reset a store.
    bool m_create_if_missing {};
    ByteBudget m_retained_asset_bytes { 8uz * 1024 * 1024 * 1024 };
};
class MapPersistence;
// Thread-safe serialized commits and immutable snapshots. Producers retain their
// input on failure. Retry acknowledgements persist within remembered_operations;
// older retries require snapshot resynchronization. Persistence performs blocking
// file IO: call commit off the rendering thread.
struct CanonicalPoseAsset;

class MapStore {
  public:
    explicit MapStore(MapStoreLimits limits = {});
    ~MapStore();
    static Result<std::unique_ptr<MapStore>> open_persistent(PersistentMapConfig config,
                                                             const std::stop_token& stop = {});
    Result<MapRevisionId> commit(const MapTransaction& transaction,
                                 const std::stop_token& stop = {});
    MapSnapshot acquire_snapshot() const;
    Result<void> reset(WorldEpoch epoch, const std::stop_token& stop = {});
    // Persistent revision-1 seeding from checked canonical poses. Copies the
    // immutable source dependency before publishing, and preserves retry IDs.
    Result<MapRevisionId> import_pose_asset(const std::filesystem::path& path,
                                            const std::string& sha256,
                                            OperationId operation,
                                            const std::stop_token& stop = {});
    Result<MapRevisionId> import_pose_asset(const CanonicalPoseAsset& asset,
                                            OperationId operation,
                                            const std::stop_token& stop = {});

  private:
    mutable std::mutex m_mutex;
    MapStoreLimits m_limits;
    MapSnapshot m_current;

    struct Remembered {
        OperationId m_operation;
        std::string m_digest;
        MapRevisionId m_revision;
    };

    std::deque<Remembered> m_operations;
    std::unique_ptr<MapPersistence> m_persistence;
    friend class MapPersistence;
    Result<MapRevisionId> import_encoded_pose_asset(std::span<const std::byte> bytes,
                                                    const std::string& hash,
                                                    OperationId operation,
                                                    const std::stop_token& stop);
    Result<MapRevisionId> commit_locked(const MapTransaction& transaction,
                                        const std::stop_token& stop,
                                        std::optional<std::string> initial_asset = {});
};

}  // namespace world_explorer

#endif  // WORLD_EXPLORER_MAP_STORE_MAP_STORE_HPP
