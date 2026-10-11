#ifndef WORLD_EXPLORER_MAP_STORE_MAP_STORE_HPP
#define WORLD_EXPLORER_MAP_STORE_MAP_STORE_HPP

#include "world_explorer/reconstruction_api/pose_estimator.hpp"
#include <deque>
#include <map>
#include <mutex>
#include <set>

namespace world_explorer {
struct calibration_record {
    calibration_id id;
    pinhole_camera camera;
    std::uint32_t width{}, height{};
};
struct observation_record {
    frame_id id;
    capture_id capture;
    calibration_id calibration;
    source_timestamp timestamp;
    std::chrono::nanoseconds capture_time;
    std::string image_sha256;
};
struct map_state {
    world_epoch epoch;
    map_revision_id revision;
    std::map<calibration_id, calibration_record> calibrations;
    std::map<frame_id, observation_record> observations;
    std::map<frame_id, pose_estimate> poses;
    std::map<submap_id, similarity_transform> world_from_submap;
    // Consumers use this set to invalidate appearance/geometry derived from
    // corrected or deleted observations before publishing a new scene revision.
    std::set<frame_id> invalidated_frames;
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
// Thread-safe atomic immutable snapshots. Producers retain their input on failure.
class map_store {
  public:
    explicit map_store(map_store_limits limits = {});
    result<map_revision_id> commit(const map_transaction& transaction);
    map_snapshot acquire_snapshot() const;
    result<void> reset(world_epoch epoch);

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
};
} // namespace world_explorer

#endif // WORLD_EXPLORER_MAP_STORE_MAP_STORE_HPP
