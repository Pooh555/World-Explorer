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
struct map_state {
    // Monocular map units are arbitrary until an explicit measured-scale
    // transaction is implemented and verified. Alignment scale alone is no proof.
    scale_status scale{scale_status::ARBITRARY};
    world_epoch epoch;
    map_revision_id revision;
    std::map<calibration_id, calibration_record> calibrations;
    std::map<frame_id, observation_record> observations;
    std::map<frame_id, pose_estimate> poses;
    std::map<submap_id, similarity_transform> world_from_submap;
    // Consumers use this set to invalidate appearance/geometry derived from
    // corrected or deleted observations before publishing a new scene revision.
    std::set<frame_id> invalidated_frames;
    // Initial canonical solve dependency, not the provenance of later corrections.
    std::optional<std::string> initial_pose_asset_sha256;
};
using map_snapshot = std::shared_ptr<const map_state>;
struct map_transaction {
    world_epoch epoch;
    map_revision_id base_revision, new_revision;
    operation_id operation;
    std::vector<calibration_record> calibrations;
    std::vector<observation_record> observations;
    std::vector<pose_estimate> poses;
    std::vector<map_alignment> alignments;
    std::vector<frame_id> deleted_frames;
};
struct map_store_limits {
    std::size_t max_frames{1000000};
    std::size_t max_calibrations{4096};
    std::size_t max_submaps{4096};
    std::size_t remembered_operations{1024};
};
struct persistent_map_config {
    std::filesystem::path directory;
    map_store_limits records{};
    byte_budget checkpoint_bytes{std::size_t{256} * 1024 * 1024};
    // Explicit initialization only. Missing/corrupt manifests never reset a store.
    bool create_if_missing{};
    byte_budget retained_asset_bytes{std::size_t{8} * 1024 * 1024 * 1024};
};
class map_persistence;
// Thread-safe serialized commits and immutable snapshots. Producers retain their
// input on failure. Retry acknowledgements persist within remembered_operations;
// older retries require snapshot resynchronization. Persistence performs blocking
// file IO: call commit off the rendering thread.
struct canonical_pose_asset;
class map_store {
  public:
    explicit map_store(map_store_limits limits = {});
    ~map_store();
    static result<std::unique_ptr<map_store>> open_persistent(persistent_map_config config,
                                                              const std::stop_token& stop = {});
    result<map_revision_id> commit(const map_transaction& transaction,
                                   const std::stop_token& stop = {});
    map_snapshot acquire_snapshot() const;
    result<void> reset(world_epoch epoch, const std::stop_token& stop = {});
    // Persistent revision-1 seeding from checked canonical poses. Copies the
    // immutable source dependency before publishing, and preserves retry IDs.
    result<map_revision_id> import_pose_asset(const std::filesystem::path& path,
                                              const std::string& sha256, operation_id operation,
                                              const std::stop_token& stop = {});
    result<map_revision_id> import_pose_asset(const canonical_pose_asset& asset,
                                              operation_id operation,
                                              const std::stop_token& stop = {});

  private:
    mutable std::mutex mutex_;
    map_store_limits limits_;
    map_snapshot current_;
    struct remembered {
        operation_id operation;
        std::string digest;
        map_revision_id revision;
    };
    std::deque<remembered> operations_;
    std::unique_ptr<map_persistence> persistence_;
    friend class map_persistence;
    result<map_revision_id> import_encoded_pose_asset(std::span<const std::byte> bytes,
                                                      const std::string& hash,
                                                      operation_id operation,
                                                      const std::stop_token& stop);
    result<map_revision_id> commit_locked(const map_transaction& transaction,
                                          const std::stop_token& stop,
                                          std::optional<std::string> initial_asset = {});
};
} // namespace world_explorer

#endif // WORLD_EXPLORER_MAP_STORE_MAP_STORE_HPP
