#pragma once

#include "world_explorer/capture/frame_source.hpp"

#include <cstdint>

namespace world_explorer {

enum class TrackingState : std::uint8_t { INITIALIZING, TRACKING, LOST, FINISHED };
enum class EstimatorMode : std::uint8_t { BATCH_SOLVE, COMPLETED_REPLAY };

struct EstimatorCapabilities {
    bool m_streaming_tracking {};
    bool m_covariance {};
    std::size_t m_observation_capacity {};
    EstimatorMode m_mode { EstimatorMode::BATCH_SOLVE };
};

struct PoseEstimatorConfig {
    ByteBudget m_memory;
    std::size_t m_max_pending_observations {};
};

struct FrameObservation {
    OperationId m_operation;
    FramePacket m_frame;
};

struct PoseEstimate {
    FrameId m_frame;
    SubmapId m_submap;
    SimilarityTransform m_submap_from_camera;
    std::chrono::nanoseconds m_capture_time;
    MapRevisionId m_revision;
    TrackingState m_tracking { TrackingState::INITIALIZING };
    // Row-major covariance of a left SE(3) tangent perturbation in submap axes:
    // rx, ry, rz (radians), tx, ty, tz (submap units). Absent unless calibrated
    // uncertainty is actually estimated; never substitute a confidence score.
    std::optional<std::array<double, 36>> m_covariance;
};

struct PoseRevision {
    MapRevisionId m_revision;
    std::vector<FrameId> m_affected;
    bool m_invalidates_geometry { true };
};

struct TrackingChange {
    TrackingState m_state;
};

struct SubmapCreated {
    SubmapId m_submap;
};

struct MapAlignment {
    SubmapId m_submap;
    MapRevisionId m_revision;
    SimilarityTransform m_world_from_submap;
};

struct EstimatorFinished {   };

using PoseEvent = std::variant<PoseEstimate,
                               PoseRevision,
                               TrackingChange,
                               SubmapCreated,
                               MapAlignment,
                               EstimatorFinished>;

// Calls serialized except thread-safe request_stop. submit moves observation only
// on success; WOULD_BLOCK preserves it. Duplicate operation IDs are idempotent.
class PoseEstimator {
  public:
    virtual ~PoseEstimator() = default;
    virtual EstimatorCapabilities capabilities() const noexcept = 0;
    virtual Result<void> start(const PoseEstimatorConfig& config) = 0;
    virtual Result<ObservationTicket> submit(FrameObservation& observation) = 0;
    virtual Result<std::vector<PoseEvent>> drain_events(EventBudget budget) = 0;
    virtual Result<void> finish() = 0;
    virtual void request_stop() noexcept = 0;
};

}  // namespace world_explorer
