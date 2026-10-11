#include "artifact_io.hpp"
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
constexpr std::size_t METADATA_BUDGET = std::size_t{256} * 1024;
std::string header_digest(const frame_packet& frame) {
    const std::array values{
        frame.id.value,
        frame.capture.value,
        frame.calibration.value,
        static_cast<std::uint64_t>(frame.capture_time.count()),
        static_cast<std::uint64_t>(frame.original_timestamp.pts),
        static_cast<std::uint64_t>(frame.original_timestamp.time_base_numerator),
        static_cast<std::uint64_t>(frame.original_timestamp.time_base_denominator),
        static_cast<std::uint64_t>(frame.pixels.width),
        static_cast<std::uint64_t>(frame.pixels.height),
        static_cast<std::uint64_t>(frame.pixels.row_stride),
        static_cast<std::uint64_t>(frame.pixels.format),
        static_cast<std::uint64_t>(frame.measured_depth.has_value()),
        static_cast<std::uint64_t>(frame.imu_samples.size())};
    std::array<std::byte, values.size() * 8> bytes{};
    std::size_t offset{};
    for (auto value : values)
        for (int index = 0; index < 8; ++index) {
            bytes[offset++] = static_cast<std::byte>(value & 255ULL);
            value >>= 8U;
        }
    return sha256(bytes);
}
class completed_pose_replay final : public pose_estimator {
  public:
    explicit completed_pose_replay(std::shared_ptr<const completed_pose_dataset> dataset)
        : dataset_(std::move(dataset)) {
        for (std::size_t index = 0; index < dataset_->observations.size(); ++index)
            observations_.emplace(dataset_->observations[index].id, index);
        for (std::size_t index = 0; index < dataset_->poses.size(); ++index)
            poses_.emplace(dataset_->poses[index].frame, index);
    }
    estimator_capabilities capabilities() const noexcept override {
        return {false, false,
                started_ ? config_.max_pending_observations : dataset_->observations.size(),
                estimator_mode::COMPLETED_REPLAY};
    }
    result<void> start(const pose_estimator_config& config) override {
        if (cancellation_.stop_requested())
            return fail(error_code::CANCELLED, "Completed pose replay cancelled");
        if (started_)
            return fail(error_code::INVALID_ARGUMENT, "Pose replay has already started");
        if (config.max_pending_observations == 0 ||
            config.max_pending_observations > dataset_->observations.size() ||
            config.memory.bytes <= METADATA_BUDGET)
            return fail(error_code::INVALID_ARGUMENT, "Invalid pose replay capacity/memory budget");
        config_ = config;
        started_ = true;
        return {};
    }
    result<observation_ticket> submit(frame_observation& observation) override {
        try {
            if (cancellation_.stop_requested())
                return fail(error_code::CANCELLED, "Completed pose replay cancelled");
            if (!started_ || !observation.operation.value)
                return fail(error_code::INVALID_ARGUMENT,
                            "Pose replay requires start and operation ID");
            const auto header = header_digest(observation.frame);
            if (const auto prior = receipts_.find(observation.operation);
                prior != receipts_.end()) {
                if (prior->second.header != header ||
                    (observation.frame.pixels.bytes &&
                     (observation.frame.pixels.bytes->size() != prior->second.image_bytes ||
                      sha256(*observation.frame.pixels.bytes) != prior->second.pixels)))
                    return fail(error_code::DUPLICATE_OPERATION,
                                "Pose operation reused with different content");
                observation.frame.pixels.bytes.reset();
                return prior->second.ticket;
            }
            if (closed_)
                return fail(error_code::INVALID_ARGUMENT,
                            "Pose replay no longer accepts observations");
            const auto found = observations_.find(observation.frame.id);
            if (found == observations_.end())
                return fail(error_code::INVALID_DATA,
                            "Observation is absent from completed pose dataset");
            const auto& frame = observation.frame;
            const auto& expected = dataset_->observations[found->second];
            const auto& calibration = dataset_->calibrations.front();
            if (frame.capture != expected.capture || frame.calibration != expected.calibration ||
                frame.capture_time != expected.capture_time ||
                frame.original_timestamp.pts != expected.timestamp.pts ||
                frame.original_timestamp.time_base_numerator !=
                    expected.timestamp.time_base_numerator ||
                frame.original_timestamp.time_base_denominator !=
                    expected.timestamp.time_base_denominator ||
                frame.pixels.width != calibration.width ||
                frame.pixels.height != calibration.height ||
                frame.pixels.row_stride != calibration.width * 3)
                return fail(error_code::INVALID_DATA,
                            "Observation metadata differs from completed dataset");
            if (frame.pixels.format != pixel_format::RGB8 || frame.measured_depth ||
                !frame.imu_samples.empty())
                return fail(error_code::UNSUPPORTED_OPERATION,
                            "Pose replay accepts packed RGB8 observations only");
            const auto image_bytes =
                static_cast<std::uint64_t>(frame.pixels.row_stride) * frame.pixels.height;
            if (!frame.pixels.bytes || frame.pixels.bytes->size() != image_bytes)
                return fail(error_code::INVALID_DATA, "Invalid RGB lease byte count");
            if (image_bytes > config_.memory.bytes - METADATA_BUDGET)
                return fail(error_code::RESOURCE_EXHAUSTED,
                            "Observation exceeds pose replay memory budget");
            if (admitted_.contains(frame.id))
                return fail(error_code::DUPLICATE_OPERATION,
                            "Frame already admitted under another operation");
            if (pending_.size() == config_.max_pending_observations)
                return fail(error_code::WOULD_BLOCK, "Pose replay queue is full; drain events");
            const auto pixels = sha256(*frame.pixels.bytes);
            const observation_ticket ticket{receipts_.size() + 1};
            // Prepare bounded next state before consuming caller leases; allocation
            // failure leaves both admission state and caller data intact.
            auto receipts = receipts_;
            auto admitted = admitted_;
            auto pending = pending_;
            receipts.emplace(observation.operation, receipt{header, pixels, ticket,
                                                            static_cast<std::size_t>(image_bytes)});
            admitted.insert(frame.id);
            pending.push_back(frame.id);
            receipts_.swap(receipts);
            admitted_.swap(admitted);
            pending_.swap(pending);
            observation.frame.pixels.bytes.reset();
            return ticket;
        } catch (const std::bad_alloc&) {
            return fail(error_code::RESOURCE_EXHAUSTED, "Pose replay admission allocation failed");
        }
    }
    result<std::vector<pose_event>> drain_events(event_budget budget) override {
        try {
            if (cancellation_.stop_requested())
                return fail(error_code::CANCELLED, "Completed pose replay cancelled");
            if (!started_)
                return fail(error_code::INVALID_ARGUMENT, "Pose replay has not started");
            std::vector<pose_event> events;
            const auto maximum = std::min(budget.count, pending_.size() + 5);
            events.reserve(maximum);
            auto initial = initial_;
            auto terminal = terminal_;
            std::size_t consumed{};
            while (events.size() < maximum) {
                if (initial < 3) {
                    if (initial == 0)
                        events.emplace_back(tracking_change{tracking_state::INITIALIZING});
                    if (initial == 1)
                        events.emplace_back(submap_created{dataset_->submap});
                    if (initial == 2)
                        events.emplace_back(
                            map_alignment{dataset_->submap, dataset_->revision, {}});
                    ++initial;
                } else if (consumed < pending_.size()) {
                    const auto pose = poses_.find(pending_[consumed]);
                    if (pose != poses_.end())
                        events.emplace_back(dataset_->poses[pose->second]);
                    else
                        events.emplace_back(tracking_change{tracking_state::LOST});
                    ++consumed;
                } else if (closed_ && terminal < 2) {
                    if (terminal == 0)
                        events.emplace_back(tracking_change{tracking_state::FINISHED});
                    else
                        events.emplace_back(estimator_finished{});
                    ++terminal;
                } else
                    break;
            }
            initial_ = initial;
            terminal_ = terminal;
            for (std::size_t index = 0; index < consumed; ++index)
                pending_.pop_front();
            return events;
        } catch (const std::bad_alloc&) {
            return fail(error_code::RESOURCE_EXHAUSTED, "Pose replay event allocation failed");
        }
    }
    result<void> finish() override {
        if (cancellation_.stop_requested())
            return fail(error_code::CANCELLED, "Completed pose replay cancelled");
        if (!started_ || admitted_.size() != dataset_->observations.size())
            return fail(error_code::INVALID_ARGUMENT,
                        "Pose replay requires every selected observation before finish");
        closed_ = true;
        return {};
    }
    void request_stop() noexcept override { cancellation_.request_stop(); }

  private:
    struct receipt {
        std::string header, pixels;
        observation_ticket ticket;
        std::size_t image_bytes{};
    };
    std::shared_ptr<const completed_pose_dataset> dataset_;
    std::map<frame_id, std::size_t> observations_, poses_;
    std::map<operation_id, receipt> receipts_;
    std::set<frame_id> admitted_;
    std::deque<frame_id> pending_;
    std::stop_source cancellation_;
    pose_estimator_config config_;
    std::uint8_t initial_{}, terminal_{};
    bool started_{}, closed_{};
};
} // namespace
result<std::unique_ptr<pose_estimator>>
make_completed_pose_replay(const std::shared_ptr<const completed_pose_dataset>& dataset) {
    try {
        if (!dataset || !adapter_detail::valid_digest(dataset->source_sha256) ||
            !adapter_detail::valid_digest(dataset->pose_completion_sha256) ||
            !adapter_detail::valid_digest(dataset->decode_completion_sha256) ||
            dataset->decode_directory.native().size() > 4096 || dataset->calibrations.size() != 1 ||
            !valid(dataset->calibrations.front()) || dataset->calibrations.front().width > 8192 ||
            dataset->calibrations.front().height > 8192 || !dataset->submap.value ||
            dataset->epoch.value != 0 || dataset->revision.value != 1 ||
            dataset->scale != scale_status::ARBITRARY || dataset->observations.size() < 3 ||
            dataset->observations.size() > 120 || dataset->poses.size() < 3 ||
            dataset->poses.size() > dataset->observations.size() ||
            dataset->poses.size() * 10 < dataset->observations.size() * 9)
            return fail(error_code::INVALID_DATA, "Invalid completed pose replay dataset");
        std::map<frame_id, const observation_record*> observations;
        for (const auto& frame : dataset->observations) {
            if (!frame.id.value || !frame.capture.value ||
                frame.calibration != dataset->calibrations.front().id ||
                frame.capture_time.count() < 0 || frame.timestamp.time_base_numerator <= 0 ||
                frame.timestamp.time_base_denominator <= 0 ||
                !adapter_detail::valid_digest(frame.image_sha256) ||
                frame.capture != dataset->observations.front().capture ||
                !observations.emplace(frame.id, &frame).second)
                return fail(error_code::INVALID_DATA, "Invalid or duplicate replay observation");
        }
        std::set<frame_id> registered;
        for (const auto& pose : dataset->poses) {
            const auto found = observations.find(pose.frame);
            if (found == observations.end() || !registered.insert(pose.frame).second ||
                pose.capture_time != found->second->capture_time ||
                pose.submap != dataset->submap || pose.revision != dataset->revision ||
                pose.tracking != tracking_state::TRACKING || pose.covariance ||
                !valid(pose.submap_from_camera) || pose.submap_from_camera.scale != 1)
                return fail(error_code::INVALID_DATA, "Invalid or duplicate replay pose");
        }
        // Freeze independently of any mutable shared_ptr alias held by a caller.
        auto snapshot = std::make_shared<const completed_pose_dataset>(*dataset);
        return std::unique_ptr<pose_estimator>(
            std::make_unique<completed_pose_replay>(std::move(snapshot)));
    } catch (const std::bad_alloc&) {
        return fail(error_code::RESOURCE_EXHAUSTED, "Pose replay allocation failed");
    }
}
} // namespace world_explorer
