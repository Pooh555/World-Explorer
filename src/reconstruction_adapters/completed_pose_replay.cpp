#include "artifact_io.hpp"
#include "observation_identity.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/reconstruction_adapters/completed_poses.hpp"

#include <algorithm>
#include <array>
#include <deque>
#include <map>
#include <new>
#include <set>

namespace world_explorer {
namespace {

constexpr std::size_t K_METADATA_BUDGET { 256uz * 1024uz };

class CompletedPoseReplay final : public PoseEstimator {
  public:
    explicit CompletedPoseReplay(std::shared_ptr<const CompletedPoseDataset> dataset)
        : m_dataset(std::move(dataset)) {
        for (std::size_t index { 0uz }; index < m_dataset->m_observations.size(); ++index) {
            m_observations.emplace(m_dataset->m_observations[index].m_id, index);
        }
        for (std::size_t index { 0uz }; index < m_dataset->m_poses.size(); ++index) {
            m_poses.emplace(m_dataset->m_poses[index].m_frame, index);
        }
    }

    EstimatorCapabilities capabilities() const noexcept override {
        return { false,
                false,
                m_started ? m_config.m_max_pending_observations : m_dataset->m_observations.size(),
                EstimatorMode::COMPLETED_REPLAY };
    }

    Result<void> start(const PoseEstimatorConfig& config) override {
        if (m_cancellation.stop_requested()) {
            return fail(ErrorCode::CANCELLED, "Completed pose replay cancelled");
        }
        if (m_started) {
            return fail(ErrorCode::INVALID_ARGUMENT, "Pose replay has already started");
        }
        if (config.m_max_pending_observations == 0 ||
            config.m_max_pending_observations > m_dataset->m_observations.size() ||
            config.m_memory.m_bytes <= K_METADATA_BUDGET) {
            return fail(ErrorCode::INVALID_ARGUMENT, "Invalid pose replay capacity/memory budget");
        }
        m_config = config;
        m_started = true;
        return {};
    }

    Result<ObservationTicket> submit(FrameObservation& observation) override {
        try {
            if (m_cancellation.stop_requested()) {
                return fail(ErrorCode::CANCELLED, "Completed pose replay cancelled");
            }
            if (!m_started || !observation.m_operation.m_value) {
                return fail(ErrorCode::INVALID_ARGUMENT,
                            "Pose replay requires start and operation ID");
            }
            const auto K_HEADER = adapter_detail::header_digest(observation.m_frame);
            if (const auto K_PRIOR = m_receipts.find(observation.m_operation);
                K_PRIOR != m_receipts.end()) {
                if (K_PRIOR->second.m_header != K_HEADER ||
                    (observation.m_frame.m_pixels.m_bytes &&
                     (observation.m_frame.m_pixels.m_bytes->size() !=
                          K_PRIOR->second.m_image_bytes ||
                      sha256(*observation.m_frame.m_pixels.m_bytes) != K_PRIOR->second.m_pixels))) {
                    return fail(ErrorCode::DUPLICATE_OPERATION,
                                "Pose operation reused with different content");
                }
                observation.m_frame.m_pixels.m_bytes.reset();
                return K_PRIOR->second.m_ticket;
            }
            if (m_closed) {
                return fail(ErrorCode::INVALID_ARGUMENT,
                            "Pose replay no longer accepts observations");
            }
            const auto K_FOUND = m_observations.find(observation.m_frame.m_id);
            if (K_FOUND == m_observations.end()) {
                return fail(ErrorCode::INVALID_DATA,
                            "Observation is absent from completed pose dataset");
            }
            const auto& frame = observation.m_frame;
            const auto& expected = m_dataset->m_observations[K_FOUND->second];
            const auto& calibration = m_dataset->m_calibrations.front();
            if (frame.m_capture != expected.m_capture ||
                frame.m_calibration != expected.m_calibration ||
                frame.m_capture_time != expected.m_capture_time ||
                frame.m_original_timestamp.m_pts != expected.m_timestamp.m_pts ||
                frame.m_original_timestamp.m_time_base_numerator !=
                    expected.m_timestamp.m_time_base_numerator ||
                frame.m_original_timestamp.m_time_base_denominator !=
                    expected.m_timestamp.m_time_base_denominator ||
                frame.m_pixels.m_width != calibration.m_width ||
                frame.m_pixels.m_height != calibration.m_height ||
                frame.m_pixels.m_row_stride != calibration.m_width * 3) {
                return fail(ErrorCode::INVALID_DATA,
                            "Observation metadata differs from completed dataset");
            }
            if (frame.m_pixels.m_format != PixelFormat::RGB8 || frame.m_measured_depth ||
                !frame.m_imu_samples.empty()) {
                return fail(ErrorCode::UNSUPPORTED_OPERATION,
                            "Pose replay accepts packed RGB8 observations only");
            }
            const auto K_IMAGE_BYTES =
                static_cast<std::uint64_t>(frame.m_pixels.m_row_stride) * frame.m_pixels.m_height;
            if (!frame.m_pixels.m_bytes || frame.m_pixels.m_bytes->size() != K_IMAGE_BYTES) {
                return fail(ErrorCode::INVALID_DATA, "Invalid RGB lease byte count");
            }
            if (K_IMAGE_BYTES > m_config.m_memory.m_bytes - K_METADATA_BUDGET) {
                return fail(ErrorCode::RESOURCE_EXHAUSTED,
                            "Observation exceeds pose replay memory budget");
            }
            if (m_admitted.contains(frame.m_id)) {
                return fail(ErrorCode::DUPLICATE_OPERATION,
                            "Frame already admitted under another operation");
            }
            if (m_pending.size() == m_config.m_max_pending_observations) {
                return fail(ErrorCode::WOULD_BLOCK, "Pose replay queue is full; drain events");
            }
            const auto K_PIXELS = sha256(*frame.m_pixels.m_bytes);
            const ObservationTicket K_TICKET { m_receipts.size() + 1 };
            // Prepare bounded next state before consuming caller leases; allocation
            // failure leaves both admission state and caller data intact.
            auto receipts = m_receipts;
            auto admitted = m_admitted;
            auto pending = m_pending;
            receipts.emplace(
                observation.m_operation,
                Receipt { K_HEADER, K_PIXELS, K_TICKET, static_cast<std::size_t>(K_IMAGE_BYTES) });
            admitted.insert(frame.m_id);
            pending.push_back(frame.m_id);
            m_receipts.swap(receipts);
            m_admitted.swap(admitted);
            m_pending.swap(pending);
            observation.m_frame.m_pixels.m_bytes.reset();
            return K_TICKET;
        } catch (const std::bad_alloc&) {
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "Pose replay admission allocation failed");
        }
    }

    Result<std::vector<PoseEvent>> drain_events(EventBudget budget) override {
        try {
            if (m_cancellation.stop_requested()) {
                return fail(ErrorCode::CANCELLED, "Completed pose replay cancelled");
            }
            if (!m_started) {
                return fail(ErrorCode::INVALID_ARGUMENT, "Pose replay has not started");
            }
            std::vector<PoseEvent> events;
            const auto K_MAXIMUM = std::min(budget.m_count, m_pending.size() + 5);
            events.reserve(K_MAXIMUM);
            auto initial = m_initial;
            auto terminal = m_terminal;
            std::size_t consumed {};
            while (events.size() < K_MAXIMUM) {
                if (initial < 3) {
                    if (initial == 0) {
                        events.emplace_back(TrackingChange { TrackingState::INITIALIZING });
                    }
                    if (initial == 1) {
                        events.emplace_back(SubmapCreated { m_dataset->m_submap });
                    }
                    if (initial == 2) {
                        events.emplace_back(
                            MapAlignment { m_dataset->m_submap, m_dataset->m_revision, {} });
                    }
                    ++initial;
                } else if (consumed < m_pending.size()) {
                    const auto K_POSE = m_poses.find(m_pending[consumed]);
                    if (K_POSE != m_poses.end()) {
                        events.emplace_back(m_dataset->m_poses[K_POSE->second]);
                    } else {
                        events.emplace_back(TrackingChange { TrackingState::LOST });
                    }
                    ++consumed;
                } else if (m_closed && terminal < 2) {
                    if (terminal == 0) {
                        events.emplace_back(TrackingChange { TrackingState::FINISHED });
                    } else {
                        events.emplace_back(EstimatorFinished {});
                    }
                    ++terminal;
                } else {
                    break;
                }
            }
            m_initial = initial;
            m_terminal = terminal;
            for (std::size_t index { 0uz }; index < consumed; ++index) {
                m_pending.pop_front();
            }
            return events;
        } catch (const std::bad_alloc&) {
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "Pose replay event allocation failed");
        }
    }

    Result<void> finish() override {
        if (m_cancellation.stop_requested()) {
            return fail(ErrorCode::CANCELLED, "Completed pose replay cancelled");
        }
        if (!m_started || m_admitted.size() != m_dataset->m_observations.size()) {
            return fail(ErrorCode::INVALID_ARGUMENT,
                        "Pose replay requires every selected observation before finish");
        }
        m_closed = true;
        return {};
    }

    void request_stop() noexcept override {
        m_cancellation.request_stop();
    }

  private:
    struct Receipt {
        std::string m_header, m_pixels;
        ObservationTicket m_ticket;
        std::size_t m_image_bytes {};
    };

    std::shared_ptr<const CompletedPoseDataset> m_dataset;
    std::map<FrameId, std::size_t> m_observations, m_poses;
    std::map<OperationId, Receipt> m_receipts;
    std::set<FrameId> m_admitted;
    std::deque<FrameId> m_pending;
    std::stop_source m_cancellation;
    PoseEstimatorConfig m_config;
    std::uint8_t m_initial {}, m_terminal {};
    bool m_started {}, m_closed {};
};

}  // namespace

Result<std::unique_ptr<PoseEstimator>>
make_completed_pose_replay(const std::shared_ptr<const CompletedPoseDataset>& dataset) {
    try {
        if (!dataset || !adapter_detail::valid_digest(dataset->m_source_sha256) ||
            !adapter_detail::valid_digest(dataset->m_pose_completion_sha256) ||
            !adapter_detail::valid_digest(dataset->m_decode_completion_sha256) ||
            dataset->m_decode_directory.native().size() > 4096 ||
            dataset->m_calibrations.size() != 1 || !valid(dataset->m_calibrations.front()) ||
            dataset->m_calibrations.front().m_width > 8192 ||
            dataset->m_calibrations.front().m_height > 8192 || !dataset->m_submap.m_value ||
            dataset->m_epoch.m_value != 0 || dataset->m_revision.m_value != 1 ||
            dataset->m_scale != ScaleStatus::ARBITRARY || dataset->m_observations.size() < 3 ||
            dataset->m_observations.size() > 120 || dataset->m_poses.size() < 3 ||
            dataset->m_poses.size() > dataset->m_observations.size() ||
            dataset->m_poses.size() * 10 < dataset->m_observations.size() * 9) {
            return fail(ErrorCode::INVALID_DATA, "Invalid completed pose replay dataset");
        }
        std::map<FrameId, const ObservationRecord*> observations;
        for (const auto& frame : dataset->m_observations) {
            if (!frame.m_id.m_value || !frame.m_capture.m_value ||
                frame.m_calibration != dataset->m_calibrations.front().m_id ||
                frame.m_capture_time.count() < 0 || frame.m_timestamp.m_time_base_numerator <= 0 ||
                frame.m_timestamp.m_time_base_denominator <= 0 ||
                !adapter_detail::valid_digest(frame.m_image_sha256) ||
                frame.m_capture != dataset->m_observations.front().m_capture ||
                !observations.emplace(frame.m_id, &frame).second) {
                return fail(ErrorCode::INVALID_DATA, "Invalid or duplicate replay observation");
            }
        }
        std::set<FrameId> registered;
        for (const auto& pose : dataset->m_poses) {
            const auto K_FOUND = observations.find(pose.m_frame);
            if (K_FOUND == observations.end() || !registered.insert(pose.m_frame).second ||
                pose.m_capture_time != K_FOUND->second->m_capture_time ||
                pose.m_submap != dataset->m_submap || pose.m_revision != dataset->m_revision ||
                pose.m_tracking != TrackingState::TRACKING || pose.m_covariance ||
                !valid(pose.m_submap_from_camera) || pose.m_submap_from_camera.m_scale != 1) {
                return fail(ErrorCode::INVALID_DATA, "Invalid or duplicate replay pose");
            }
        }
        // Freeze independently of any mutable shared_ptr alias held by a caller.
        auto snapshot = std::make_shared<const CompletedPoseDataset>(*dataset);
        return std::unique_ptr<PoseEstimator>(
            std::make_unique<CompletedPoseReplay>(std::move(snapshot)));
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Pose replay allocation failed");
    }
}

}  // namespace world_explorer
