#ifndef WORLD_EXPLORER_RECONSTRUCTION_API_POSE_ESTIMATOR_HPP
#define WORLD_EXPLORER_RECONSTRUCTION_API_POSE_ESTIMATOR_HPP
#include <cstdint>

#include "world_explorer/capture/frame_source.hpp"

namespace world_explorer {
enum class tracking_state : std::uint8_t { INITIALIZING, TRACKING, LOST, FINISHED };
enum class estimator_mode : std::uint8_t { BATCH_SOLVE, COMPLETED_REPLAY };
struct estimator_capabilities {
    bool streaming_tracking{};
    bool covariance{};
    std::size_t observation_capacity{};
    estimator_mode mode{estimator_mode::BATCH_SOLVE};
};
struct pose_estimator_config {
    byte_budget memory;
    std::size_t max_pending_observations{};
};
struct frame_observation {
    operation_id operation;
    frame_packet frame;
};
struct pose_estimate {
    frame_id frame;
    submap_id submap;
    similarity_transform submap_from_camera;
    std::chrono::nanoseconds capture_time;
    map_revision_id revision;
    tracking_state tracking{tracking_state::INITIALIZING};
    // Row-major covariance of a left SE(3) tangent perturbation in submap axes:
    // rx, ry, rz (radians), tx, ty, tz (submap units). Absent unless calibrated
    // uncertainty is actually estimated; never substitute a confidence score.
    std::optional<std::array<double, 36>> covariance;
};
struct pose_revision {
    map_revision_id revision;
    std::vector<frame_id> affected;
    bool invalidates_geometry{true};
};
struct tracking_change {
    tracking_state state;
};
struct submap_created {
    submap_id submap;
};
struct map_alignment {
    submap_id submap;
    map_revision_id revision;
    similarity_transform world_from_submap;
};
struct estimator_finished {};
using pose_event = std::variant<pose_estimate, pose_revision, tracking_change, submap_created,
                                map_alignment, estimator_finished>;
// Calls serialized except thread-safe request_stop. submit moves observation only
// on success; WOULD_BLOCK preserves it. Duplicate operation IDs are idempotent.
class pose_estimator {
  public:
    virtual ~pose_estimator() = default;
    virtual estimator_capabilities capabilities() const noexcept = 0;
    virtual result<void> start(const pose_estimator_config& config) = 0;
    virtual result<observation_ticket> submit(frame_observation& observation) = 0;
    virtual result<std::vector<pose_event>> drain_events(event_budget budget) = 0;
    virtual result<void> finish() = 0;
    virtual void request_stop() noexcept = 0;
};
} // namespace world_explorer

#endif // WORLD_EXPLORER_RECONSTRUCTION_API_POSE_ESTIMATOR_HPP
