#include "world_explorer/reconstruction_adapters/batch_poses.hpp"
#include "artifact_io.hpp"
#include "observation_identity.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/reconstruction_adapters/decoded_frame_source.hpp"
#include "world_explorer/reconstruction_adapters/worker_protocol.hpp"
#include <algorithm>
#include <fstream>
#include <map>
#include <new>
#include <set>

namespace world_explorer {
namespace {
constexpr std::size_t METADATA_BYTES = std::size_t{2} * 1024 * 1024;
constexpr std::size_t MAX_JSON = std::size_t{512} * 1024;
std::string utf8(const std::filesystem::path& path) {
    const auto bytes = path.generic_u8string();
    return {bytes.begin(), bytes.end()};
}
void publish_json(const std::filesystem::path& path, const nlohmann::json& value,
                  bool resume = false) {
    const auto text = value.dump();
    if (text.size() > MAX_JSON)
        throw std::invalid_argument("Batch manifest is oversized or already published");
    if (std::filesystem::exists(std::filesystem::symlink_status(path))) {
        if (resume && !std::filesystem::is_symlink(path)) {
            const auto digest = sha256_file(path, MAX_JSON);
            if (digest && *digest == sha256(std::as_bytes(std::span(text))))
                return;
        }
        throw std::invalid_argument("Existing batch manifest differs from the resumed intent");
    }
    auto temporary = path;
    temporary += ".tmp";
    if (std::filesystem::exists(temporary))
        throw std::invalid_argument("Batch temporary manifest already exists");
    std::ofstream output(temporary, std::ios::binary);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.close();
    if (!output)
        throw std::runtime_error("Batch manifest write failed");
    std::filesystem::rename(temporary, path);
}
class colmap_batch_estimator final : public batch_pose_estimator {
  public:
    colmap_batch_estimator(batch_pose_config config, decoded_frame_catalog catalog,
                           nlohmann::json metadata)
        : config_(std::move(config)), catalog_(std::move(catalog)), metadata_(std::move(metadata)) {
    }
    estimator_capabilities capabilities() const noexcept override {
        return {false, false, catalog_.observations.size(), estimator_mode::BATCH_SOLVE};
    }
    result<void> start(const pose_estimator_config& config) override {
        try {
            if (cancellation_.stop_requested())
                return fail(error_code::CANCELLED, "Batch poses cancelled before start");
            if (started_ || config.max_pending_observations < catalog_.observations.size() ||
                config.max_pending_observations > 120 || config.memory.bytes <= METADATA_BYTES)
                return fail(
                    error_code::INVALID_ARGUMENT,
                    "Batch start requires capacity for the complete selected-frame journal");
            std::filesystem::create_directories(config_.job_directory.parent_path());
            if (!std::filesystem::create_directory(config_.job_directory) && !config_.resume_input)
                return fail(error_code::WOULD_BLOCK,
                            "Batch output exists; choose a fresh directory");
            if (config_.resume_input) {
                if (std::filesystem::is_symlink(config_.job_directory))
                    return fail(error_code::INVALID_DATA, "Batch job may not be a symlink");
                const std::set<std::string> allowed{"input",
                                                    "stages",
                                                    "batch-request.json",
                                                    "request.json",
                                                    "capabilities.json",
                                                    "result.json",
                                                    "decode.json",
                                                    "poses.json",
                                                    "job.lock"};
                for (const auto& entry : std::filesystem::directory_iterator(config_.job_directory))
                    if (entry.is_symlink() || !allowed.contains(entry.path().filename().string()))
                        return fail(error_code::INVALID_DATA,
                                    "Batch job contains unexpected entries");
                if (std::filesystem::exists(config_.job_directory / "job.lock"))
                    return fail(error_code::WOULD_BLOCK,
                                "Batch worker lock exists; verify its process before recovery");
            }
            std::filesystem::create_directory(config_.job_directory / "input");
            if (std::filesystem::is_symlink(config_.job_directory / "input"))
                return fail(error_code::INVALID_DATA, "Batch input may not be a symlink");
            if (config_.resume_input) {
                std::set<std::string> allowed{"journal.json"};
                for (std::size_t index = 0; index < catalog_.observations.size(); ++index)
                    allowed.insert(std::to_string(index + 1) + ".ppm");
                for (const auto& entry :
                     std::filesystem::directory_iterator(config_.job_directory / "input"))
                    if (entry.is_symlink() || !entry.is_regular_file() ||
                        !allowed.contains(entry.path().filename().string()))
                        return fail(error_code::INVALID_DATA,
                                    "Batch input contains uncommitted assets");
            }
            memory_ = config.memory;
            started_ = true;
            return {};
        } catch (const std::exception& exception) {
            return fail(error_code::IO_ERROR, exception.what());
        }
    }
    result<observation_ticket> submit(frame_observation& observation) override {
        std::filesystem::path temporary, destination;
        bool published{};
        const auto rollback = [&] {
            std::error_code ignored;
            if (!temporary.empty())
                std::filesystem::remove(temporary, ignored);
            if (published)
                std::filesystem::remove(destination, ignored);
        };
        try {
            if (cancellation_.stop_requested())
                return fail(error_code::CANCELLED, "Batch observation cancelled");
            if (!started_ || !observation.operation.value)
                return fail(error_code::INVALID_ARGUMENT,
                            "Batch submit requires start and operation ID");
            const auto header = adapter_detail::header_digest(observation.frame);
            if (const auto prior = receipts_.find(observation.operation);
                prior != receipts_.end()) {
                if (header != prior->second.header ||
                    (observation.frame.pixels.bytes &&
                     (observation.frame.pixels.bytes->size() != prior->second.bytes ||
                      sha256(*observation.frame.pixels.bytes) != prior->second.pixels)))
                    return fail(error_code::DUPLICATE_OPERATION, "Batch retry content differs");
                observation.frame.pixels.bytes.reset();
                return prior->second.ticket;
            }
            if (closed_)
                return fail(error_code::INVALID_ARGUMENT, "Batch observation admission is closed");
            if (receipts_.size() == catalog_.observations.size())
                return fail(error_code::WOULD_BLOCK, "Selected-frame journal is full");
            const auto index = receipts_.size();
            const auto& expected = catalog_.observations[index];
            const auto& frame = observation.frame;
            const auto bytes = static_cast<std::size_t>(expected.width) * expected.height * 3;
            if (frame.id != expected.id || frame.capture != catalog_.capture ||
                frame.calibration != expected.calibration ||
                frame.capture_time != expected.capture_time ||
                frame.original_timestamp.pts != expected.original_timestamp.pts ||
                frame.original_timestamp.time_base_numerator !=
                    expected.original_timestamp.time_base_numerator ||
                frame.original_timestamp.time_base_denominator !=
                    expected.original_timestamp.time_base_denominator ||
                frame.pixels.width != expected.width || frame.pixels.height != expected.height ||
                frame.pixels.row_stride != expected.width * 3 || !frame.pixels.bytes ||
                frame.pixels.bytes->size() != bytes)
                return fail(error_code::INVALID_DATA,
                            "Batch observation differs from the ordered selection");
            if (frame.pixels.format != pixel_format::RGB8 || frame.measured_depth ||
                !frame.imu_samples.empty())
                return fail(error_code::UNSUPPORTED_OPERATION,
                            "Batch profile requires packed RGB8 only");
            const auto prefix = "P6\n" + std::to_string(expected.width) + " " +
                                std::to_string(expected.height) + "\n255\n";
            if (bytes > memory_.bytes - METADATA_BYTES ||
                bytes + prefix.size() > config_.journal_bytes.bytes - METADATA_BYTES - asset_bytes_)
                return fail(error_code::RESOURCE_EXHAUSTED,
                            "Batch observation exceeds memory/disk budget");
            const auto pixels = sha256(*frame.pixels.bytes);
            const observation_ticket ticket{index + 1};
            auto receipts = receipts_;
            receipts.emplace(observation.operation, receipt{header, pixels, ticket, bytes});
            auto records = records_;
            auto record = metadata_.at("frames").at(index);
            const auto name = std::to_string(index + 1) + ".ppm";
            record["file"] = name;
            record["pixel_sha256"] = pixels;
            record["width"] = expected.width;
            record["height"] = expected.height;
            record["bytes"] = bytes + prefix.size();
            destination = config_.job_directory / "input" / name;
            temporary = destination;
            temporary += ".tmp";
            if (config_.resume_input && std::filesystem::exists(destination) &&
                !std::filesystem::exists(temporary)) {
                if (std::filesystem::is_symlink(destination) ||
                    std::filesystem::file_size(destination) != bytes + prefix.size())
                    return fail(error_code::INVALID_DATA, "Resumed RGB asset shape differs");
                sha256_builder expected_hash;
                expected_hash.update(std::as_bytes(std::span(prefix)));
                expected_hash.update(*frame.pixels.bytes);
                const auto digest =
                    sha256_file(destination, bytes + prefix.size(), cancellation_.get_token());
                if (!digest)
                    return std::unexpected(digest.error());
                if (*digest != expected_hash.finish())
                    return fail(error_code::INVALID_DATA, "Resumed RGB asset content differs");
                record["sha256"] = *digest;
                records.push_back(std::move(record));
                receipts_.swap(receipts);
                records_.swap(records);
                asset_bytes_ += bytes + prefix.size();
                observation.frame.pixels.bytes.reset();
                return ticket;
            }
            if (std::filesystem::exists(destination) || std::filesystem::exists(temporary))
                return fail(error_code::INVALID_DATA, "Batch observation asset already exists");
            std::ofstream output(temporary, std::ios::binary);
            output.write(prefix.data(), static_cast<std::streamsize>(prefix.size()));
            for (std::size_t offset = 0; offset < bytes;) {
                if (cancellation_.stop_requested()) {
                    output.close();
                    rollback();
                    return fail(error_code::CANCELLED, "Batch observation write cancelled");
                }
                const auto count = std::min(bytes - offset, std::size_t{256} * 1024);
                output.write(reinterpret_cast<const char*>(frame.pixels.bytes->data() + offset),
                             static_cast<std::streamsize>(count));
                offset += count;
            }
            output.close();
            if (!output)
                throw std::runtime_error("Batch observation write failed");
            const auto digest =
                sha256_file(temporary, bytes + prefix.size(), cancellation_.get_token());
            if (!digest) {
                rollback();
                return std::unexpected(digest.error());
            }
            record["sha256"] = *digest;
            records.push_back(std::move(record));
            std::filesystem::rename(temporary, destination);
            published = true;
            receipts_.swap(receipts);
            records_.swap(records);
            asset_bytes_ += bytes + prefix.size();
            observation.frame.pixels.bytes.reset();
            return ticket;
        } catch (const std::bad_alloc&) {
            rollback();
            return fail(error_code::RESOURCE_EXHAUSTED, "Batch observation allocation failed");
        } catch (const std::exception& exception) {
            rollback();
            return fail(error_code::IO_ERROR, exception.what());
        }
    }
    result<void> finish() override {
        if (failure_)
            return std::unexpected(*failure_);
        if (dataset_)
            return {};
        if (cancellation_.stop_requested())
            return fail(error_code::CANCELLED, "Batch poses cancelled");
        if (!started_ || receipts_.size() != catalog_.observations.size())
            return fail(error_code::INVALID_ARGUMENT,
                        "Batch finish requires all selected observations");
        closed_ = true;
        const auto solved = solve();
        if (!solved) {
            failure_ = solved.error();
            return std::unexpected(*failure_);
        }
        dataset_ = *solved;
        return {};
    }
    result<std::vector<pose_event>> drain_events(event_budget budget) override {
        try {
            if (cancellation_.stop_requested())
                return fail(error_code::CANCELLED, "Batch pose events cancelled");
            if (!started_)
                return fail(error_code::INVALID_ARGUMENT, "Batch poses have not started");
            if (failure_)
                return std::unexpected(*failure_);
            std::vector<pose_event> events;
            auto cursor = cursor_;
            const auto available = dataset_ ? dataset_->poses.size() + 5 : 1;
            events.reserve(std::min(budget.count, available));
            while (events.size() < budget.count && cursor < available) {
                if (cursor == 0)
                    events.emplace_back(tracking_change{tracking_state::INITIALIZING});
                else if (cursor == 1)
                    events.emplace_back(submap_created{dataset_->submap});
                else if (cursor == 2)
                    events.emplace_back(map_alignment{dataset_->submap, dataset_->revision, {}});
                else if (cursor < dataset_->poses.size() + 3)
                    events.emplace_back(dataset_->poses[cursor - 3]);
                else if (cursor == dataset_->poses.size() + 3)
                    events.emplace_back(tracking_change{tracking_state::FINISHED});
                else
                    events.emplace_back(estimator_finished{});
                ++cursor;
            }
            cursor_ = cursor;
            return events;
        } catch (const std::bad_alloc&) {
            return fail(error_code::RESOURCE_EXHAUSTED, "Batch event allocation failed");
        }
    }
    void request_stop() noexcept override { cancellation_.request_stop(); }
    result<std::shared_ptr<const completed_pose_dataset>> acquire_dataset() const override {
        if (failure_)
            return std::unexpected(*failure_);
        if (!dataset_)
            return fail(error_code::WOULD_BLOCK, "Batch poses are not complete");
        return dataset_;
    }

  private:
    result<std::shared_ptr<const completed_pose_dataset>> solve() {
        try {
            const auto journal = config_.job_directory / "input" / "journal.json";
            publish_json(journal,
                         {{"schema_version", 1},
                          {"profile", "native_rgb_journal_v1"},
                          {"source_sha256", config_.source_sha256},
                          {"decode_completion_sha256", catalog_.completion_sha256},
                          {"source_frame_count", metadata_.at("source_frame_count")},
                          {"frames", records_}},
                         config_.resume_input);
            const auto digest = sha256_file(journal, MAX_JSON, cancellation_.get_token());
            if (!digest)
                return std::unexpected(digest.error());
            const auto request = config_.job_directory / "batch-request.json";
            publish_json(
                request,
                {{"schema_version", 1},
                 {"journal", utf8(journal)},
                 {"journal_sha256", *digest},
                 {"job_dir", utf8(config_.job_directory)},
                 {"pose_python", config_.pose_python},
                 {"pose_recipe",
                  config_.recipe == pose_recipe::SEQUENTIAL ? "sequential" : "exhaustive_guided"},
                 {"limits", {{"cpu_threads", config_.cpu_threads}}}},
                config_.resume_input);
            std::optional<error> worker_error;
            const auto observe = [&](const worker_event& event) {
                if (const auto* handshake = std::get_if<worker_handshake>(&event)) {
                    if (!handshake->batch || handshake->streaming_tracking ||
                        handshake->max_frames < catalog_.observations.size()) {
                        worker_error = error{error_code::UNSUPPORTED_OPERATION,
                                             "Worker lacks batch pose capacity"};
                        cancellation_.request_stop();
                    }
                }
                if (const auto* failure = std::get_if<worker_failure>(&event))
                    if (!worker_error) {
                        const auto code =
                            failure->code == "CANCELLED"        ? error_code::CANCELLED
                            : failure->code == "RESOURCE_LIMIT" ? error_code::RESOURCE_EXHAUSTED
                            : failure->code == "UNSUPPORTED"    ? error_code::UNSUPPORTED_OPERATION
                            : failure->code == "BAD_INPUT" || failure->code == "CORRUPT_ASSET"
                                ? error_code::INVALID_DATA
                                : error_code::BACKEND_FAILURE;
                        worker_error = error{code, failure->code + ": " + failure->message};
                    }
                if (const auto* completed = std::get_if<worker_completed>(&event);
                    completed && completed->stage != "poses")
                    worker_error =
                        error{error_code::INVALID_DATA, "Worker completed the wrong batch stage"};
                if (config_.observed)
                    config_.observed(event);
            };
            const auto executed =
                execute_worker_protocol(config_.worker_python, config_.worker_script,
                                        {"solve-journal", "--request", utf8(request)},
                                        cancellation_.get_token(), config_.log, observe);
            if (worker_error)
                return std::unexpected(*worker_error);
            if (!executed)
                return std::unexpected(executed.error());
            if (*executed != 0)
                return fail(error_code::BACKEND_FAILURE, "Batch pose worker failed");
            return load_completed_poses({config_.job_directory, config_.source_sha256},
                                        cancellation_.get_token());
        } catch (const std::bad_alloc&) {
            return fail(error_code::RESOURCE_EXHAUSTED, "Batch solve allocation failed");
        } catch (const std::exception& exception) {
            return fail(error_code::IO_ERROR, exception.what());
        }
    }
    struct receipt {
        std::string header, pixels;
        observation_ticket ticket;
        std::size_t bytes{};
    };
    batch_pose_config config_;
    decoded_frame_catalog catalog_;
    nlohmann::json metadata_;
    nlohmann::json records_ = nlohmann::json::array();
    std::map<operation_id, receipt> receipts_;
    std::shared_ptr<const completed_pose_dataset> dataset_;
    std::optional<error> failure_;
    std::stop_source cancellation_;
    byte_budget memory_{};
    std::size_t asset_bytes_{}, cursor_{};
    bool started_{}, closed_{};
};
} // namespace
result<std::unique_ptr<batch_pose_estimator>>
make_batch_pose_estimator(batch_pose_config config, const std::stop_token& stop) {
    try {
        if (!adapter_detail::valid_digest(config.source_sha256) || config.job_directory.empty() ||
            config.worker_python.empty() || config.pose_python.empty() ||
            config.worker_python.size() > 4096 || config.pose_python.size() > 4096 ||
            config.worker_python.find('\0') != std::string::npos ||
            config.pose_python.find('\0') != std::string::npos || config.cpu_threads == 0 ||
            config.cpu_threads > 4 || config.journal_bytes.bytes <= METADATA_BYTES ||
            config.journal_bytes.bytes > 32ULL * 1024 * 1024 * 1024 ||
            (config.recipe != pose_recipe::SEQUENTIAL &&
             config.recipe != pose_recipe::EXHAUSTIVE_GUIDED) ||
            !std::filesystem::is_regular_file(config.worker_script))
            return fail(error_code::INVALID_ARGUMENT, "Invalid batch pose configuration");
        const auto catalog = inspect_decoded_frame_source(
            {.stage_directory = config.decode_stage, .source_sha256 = config.source_sha256}, stop);
        if (!catalog)
            return std::unexpected(catalog.error());
        if (catalog->observations.size() < 3)
            return fail(error_code::INVALID_DATA,
                        "Batch poses require at least three selected frames");
        config.job_directory = std::filesystem::weakly_canonical(config.job_directory);
        config.decode_stage = std::filesystem::canonical(config.decode_stage);
        auto metadata = adapter_detail::read_json(config.decode_stage / "result.json", MAX_JSON);
        const auto completion = adapter_detail::read_json(config.decode_stage / "completion.json",
                                                          MAX_JSON, catalog->completion_sha256);
        for (const auto& asset : completion.at("files"))
            if (asset.at("path") == "result.json")
                metadata = adapter_detail::read_json(config.decode_stage / "result.json", MAX_JSON,
                                                     asset.at("sha256").get<std::string>());
        nlohmann::json selected = {{"source_frame_count", metadata.at("source_frame_count")},
                                   {"frames", nlohmann::json::array()}};
        for (const auto& frame : metadata.at("frames")) {
            nlohmann::json record;
            for (const auto* field : {"frame_id", "calibration_id", "source_index", "pts",
                                      "time_base", "capture_time_ns"})
                record[field] = frame.at(field);
            selected["frames"].push_back(std::move(record));
        }
        return std::unique_ptr<batch_pose_estimator>(std::make_unique<colmap_batch_estimator>(
            std::move(config), *catalog, std::move(selected)));
    } catch (const std::bad_alloc&) {
        return fail(error_code::RESOURCE_EXHAUSTED, "Batch pose configuration allocation failed");
    } catch (const std::exception& exception) {
        return fail(error_code::INVALID_DATA, exception.what());
    }
}
} // namespace world_explorer
