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

constexpr std::size_t K_METADATA_BYTES { 2uz * 1024uz * 1024uz };
constexpr std::size_t K_MAX_JSON { 512uz * 1024uz };

std::string utf8(const std::filesystem::path& path) {
    const auto K_BYTES = path.generic_u8string();
    return { K_BYTES.begin(), K_BYTES.end() };
}

bool occupied(const std::filesystem::path& path) {
    return std::filesystem::exists(std::filesystem::symlink_status(path));
}

bool output_directory(const std::filesystem::path& path) {
    return std::filesystem::is_directory(std::filesystem::symlink_status(path)) &&
           std::filesystem::canonical(path) == path;
}

void publish_json(const std::filesystem::path& path,
                  const nlohmann::json& value,
                  bool resume = false) {
    const auto K_TEXT = value.dump();
    if (K_TEXT.size() > K_MAX_JSON) {
        throw std::invalid_argument("Batch manifest is oversized or already published");
    }
    if (occupied(path)) {
        if (resume && !std::filesystem::is_symlink(path)) {
            const auto K_DIGEST = sha256_file(path, K_MAX_JSON);
            if (K_DIGEST && *K_DIGEST == sha256(std::as_bytes(std::span(K_TEXT)))) {
                return;
            }
        }
        throw std::invalid_argument("Existing batch manifest differs from the resumed intent");
    }
    auto temporary = path;
    temporary += ".tmp";
    if (occupied(temporary)) {
        throw std::invalid_argument("Batch temporary manifest already exists");
    }
    std::ofstream output(temporary, std::ios::binary | std::ios::out | std::ios::noreplace);
    if (!output) {
        throw std::runtime_error("Batch temporary manifest creation failed");
    }
    output.write(K_TEXT.data(), static_cast<std::streamsize>(K_TEXT.size()));
    output.close();
    if (!output) {
        throw std::runtime_error("Batch manifest write failed");
    }
    std::filesystem::rename(temporary, path);
}

class ColmapBatchEstimator final : public BatchPoseEstimator {
  public:
    ColmapBatchEstimator(BatchPoseConfig config,
                         DecodedFrameCatalog catalog,
                         nlohmann::json metadata)
        : m_config(std::move(config)), m_catalog(std::move(catalog)),
          m_metadata(std::move(metadata)) { }

    EstimatorCapabilities capabilities() const noexcept override {
        return { false, false, m_catalog.m_observations.size(), EstimatorMode::BATCH_SOLVE };
    }

    Result<void> start(const PoseEstimatorConfig& config) override {
        try {
            if (m_cancellation.stop_requested()) {
                return fail(ErrorCode::CANCELLED, "Batch poses cancelled before start");
            }
            if (m_started || config.m_max_pending_observations < m_catalog.m_observations.size() ||
                config.m_max_pending_observations > 120 ||
                config.m_memory.m_bytes <= K_METADATA_BYTES) {
                return fail(
                    ErrorCode::INVALID_ARGUMENT,
                    "Batch start requires capacity for the complete selected-frame journal");
            }
            std::filesystem::create_directories(m_config.m_job_directory.parent_path());
            if (!std::filesystem::create_directory(m_config.m_job_directory) &&
                !m_config.m_resume_input) {
                return fail(ErrorCode::WOULD_BLOCK,
                            "Batch output exists; choose a fresh directory");
            }
            if (m_config.m_resume_input) {
                if (std::filesystem::is_symlink(m_config.m_job_directory)) {
                    return fail(ErrorCode::INVALID_DATA, "Batch job may not be a symlink");
                }
                const std::set<std::string> K_ALLOWED { "input",
                                                       "stages",
                                                       "batch-request.json",
                                                       "request.json",
                                                       "capabilities.json",
                                                       "result.json",
                                                       "decode.json",
                                                       "poses.json",
                                                       "job.lock" };
                for (const auto& entry :
                     std::filesystem::directory_iterator(m_config.m_job_directory)) {
                    if (entry.is_symlink() ||
                        !K_ALLOWED.contains(entry.path().filename().string())) {
                        return fail(ErrorCode::INVALID_DATA,
                                    "Batch job contains unexpected entries");
                    }
                }
                if (std::filesystem::exists(m_config.m_job_directory / "job.lock")) {
                    return fail(ErrorCode::WOULD_BLOCK,
                                "Batch worker lock exists; verify its process before recovery");
                }
            }
            std::filesystem::create_directory(m_config.m_job_directory / "input");
            if (std::filesystem::is_symlink(m_config.m_job_directory / "input")) {
                return fail(ErrorCode::INVALID_DATA, "Batch input may not be a symlink");
            }
            if (m_config.m_resume_input) {
                std::set<std::string> allowed { "journal.json" };
                for (std::size_t index { 0uz }; index < m_catalog.m_observations.size(); ++index) {
                    allowed.insert(std::to_string(index + 1) + ".ppm");
                }
                for (const auto& entry :
                     std::filesystem::directory_iterator(m_config.m_job_directory / "input")) {
                    if (entry.is_symlink() || !entry.is_regular_file() ||
                        !allowed.contains(entry.path().filename().string())) {
                        return fail(ErrorCode::INVALID_DATA,
                                    "Batch input contains uncommitted assets");
                    }
                }
            }
            m_memory = config.m_memory;
            m_started = true;
            return {};
        } catch (const std::exception& exception) {
            return fail(ErrorCode::IO_ERROR, exception.what());
        }
    }

    Result<ObservationTicket> submit(FrameObservation& observation) override {
        std::filesystem::path temporary, destination;
        bool temporary_owned {}, published {};
        const auto K_ROLLBACK = [&] {
            std::error_code ignored;
            if (temporary_owned) {
                std::filesystem::remove(temporary, ignored);
            }
            if (published) {
                std::filesystem::remove(destination, ignored);
            }
        };
        try {
            if (m_cancellation.stop_requested()) {
                return fail(ErrorCode::CANCELLED, "Batch observation cancelled");
            }
            if (!m_started || !observation.m_operation.m_value) {
                return fail(ErrorCode::INVALID_ARGUMENT,
                            "Batch submit requires start and operation ID");
            }
            const auto K_HEADER = adapter_detail::header_digest(observation.m_frame);
            if (const auto K_PRIOR = m_receipts.find(observation.m_operation);
                K_PRIOR != m_receipts.end()) {
                if (K_HEADER != K_PRIOR->second.m_header ||
                    (observation.m_frame.m_pixels.m_bytes &&
                     (observation.m_frame.m_pixels.m_bytes->size() != K_PRIOR->second.m_bytes ||
                      sha256(*observation.m_frame.m_pixels.m_bytes) != K_PRIOR->second.m_pixels))) {
                    return fail(ErrorCode::DUPLICATE_OPERATION, "Batch retry content differs");
                }
                observation.m_frame.m_pixels.m_bytes.reset();
                return K_PRIOR->second.m_ticket;
            }
            if (m_closed) {
                return fail(ErrorCode::INVALID_ARGUMENT, "Batch observation admission is closed");
            }
            if (m_receipts.size() == m_catalog.m_observations.size()) {
                return fail(ErrorCode::WOULD_BLOCK, "Selected-frame journal is full");
            }
            const auto K_INDEX = m_receipts.size();
            const auto& expected = m_catalog.m_observations[K_INDEX];
            const auto& frame = observation.m_frame;
            const auto K_BYTES = static_cast<std::size_t>(expected.m_width) * expected.m_height * 3;
            if (frame.m_id != expected.m_id || frame.m_capture != m_catalog.m_capture ||
                frame.m_calibration != expected.m_calibration ||
                frame.m_capture_time != expected.m_capture_time ||
                frame.m_original_timestamp.m_pts != expected.m_original_timestamp.m_pts ||
                frame.m_original_timestamp.m_time_base_numerator !=
                    expected.m_original_timestamp.m_time_base_numerator ||
                frame.m_original_timestamp.m_time_base_denominator !=
                    expected.m_original_timestamp.m_time_base_denominator ||
                frame.m_pixels.m_width != expected.m_width ||
                frame.m_pixels.m_height != expected.m_height ||
                frame.m_pixels.m_row_stride != expected.m_width * 3 || !frame.m_pixels.m_bytes ||
                frame.m_pixels.m_bytes->size() != K_BYTES) {
                return fail(ErrorCode::INVALID_DATA,
                            "Batch observation differs from the ordered selection");
            }
            if (frame.m_pixels.m_format != PixelFormat::RGB8 || frame.m_measured_depth ||
                !frame.m_imu_samples.empty()) {
                return fail(ErrorCode::UNSUPPORTED_OPERATION,
                            "Batch profile requires packed RGB8 only");
            }
            const auto K_PREFIX = "P6\n" + std::to_string(expected.m_width) + " " +
                                  std::to_string(expected.m_height) + "\n255\n";
            if (K_BYTES > m_memory.m_bytes - K_METADATA_BYTES ||
                K_BYTES + K_PREFIX.size() >
                    m_config.m_journal_bytes.m_bytes - K_METADATA_BYTES - m_asset_bytes) {
                return fail(ErrorCode::RESOURCE_EXHAUSTED,
                            "Batch observation exceeds memory/disk budget");
            }
            if (!output_directory(m_config.m_job_directory) ||
                !output_directory(m_config.m_job_directory / "input")) {
                return fail(ErrorCode::INVALID_DATA, "Batch output directory changed or is linked");
            }
            const auto K_PIXELS = sha256(*frame.m_pixels.m_bytes);
            const ObservationTicket K_TICKET { K_INDEX + 1 };
            auto receipts = m_receipts;
            receipts.emplace(observation.m_operation,
                             Receipt { K_HEADER, K_PIXELS, K_TICKET, K_BYTES });
            auto records = m_records;
            auto record = m_metadata.at("frames").at(K_INDEX);
            const auto K_NAME = std::to_string(K_INDEX + 1) + ".ppm";
            record["file"] = K_NAME;
            record["pixel_sha256"] = K_PIXELS;
            record["width"] = expected.m_width;
            record["height"] = expected.m_height;
            record["bytes"] = K_BYTES + K_PREFIX.size();
            destination = m_config.m_job_directory / "input" / K_NAME;
            temporary = destination;
            temporary += ".tmp";
            if (m_config.m_resume_input && occupied(destination) && !occupied(temporary)) {
                if (std::filesystem::is_symlink(destination) ||
                    std::filesystem::file_size(destination) != K_BYTES + K_PREFIX.size()) {
                    return fail(ErrorCode::INVALID_DATA, "Resumed RGB asset shape differs");
                }
                Sha256Builder expected_hash;
                expected_hash.update(std::as_bytes(std::span(K_PREFIX)));
                expected_hash.update(*frame.m_pixels.m_bytes);
                const auto K_DIGEST =
                    sha256_file(destination, K_BYTES + K_PREFIX.size(), m_cancellation.get_token());
                if (!K_DIGEST) {
                    return std::unexpected(K_DIGEST.error());
                }
                if (*K_DIGEST != expected_hash.finish()) {
                    return fail(ErrorCode::INVALID_DATA, "Resumed RGB asset content differs");
                }
                record["sha256"] = *K_DIGEST;
                records.push_back(std::move(record));
                m_receipts.swap(receipts);
                m_records.swap(records);
                m_asset_bytes += K_BYTES + K_PREFIX.size();
                observation.m_frame.m_pixels.m_bytes.reset();
                return K_TICKET;
            }
            if (occupied(destination) || occupied(temporary)) {
                return fail(ErrorCode::INVALID_DATA, "Batch observation asset already exists");
            }
            std::ofstream output(temporary, std::ios::binary | std::ios::out | std::ios::noreplace);
            if (!output) {
                throw std::runtime_error("Batch temporary RGB creation failed");
            }
            temporary_owned = true;
            output.write(K_PREFIX.data(), static_cast<std::streamsize>(K_PREFIX.size()));
            for (std::size_t offset { 0uz }; offset < K_BYTES;) {
                if (m_cancellation.stop_requested()) {
                    output.close();
                    K_ROLLBACK();
                    return fail(ErrorCode::CANCELLED, "Batch observation write cancelled");
                }
                const auto K_COUNT = std::min(K_BYTES - offset, 256uz * 1024);
                output.write(reinterpret_cast<const char*>(frame.m_pixels.m_bytes->data() + offset),
                             static_cast<std::streamsize>(K_COUNT));
                offset += K_COUNT;
            }
            output.close();
            if (!output) {
                throw std::runtime_error("Batch observation write failed");
            }
            const auto K_DIGEST =
                sha256_file(temporary, K_BYTES + K_PREFIX.size(), m_cancellation.get_token());
            if (!K_DIGEST) {
                K_ROLLBACK();
                return std::unexpected(K_DIGEST.error());
            }
            record["sha256"] = *K_DIGEST;
            records.push_back(std::move(record));
            std::filesystem::rename(temporary, destination);
            published = true;
            m_receipts.swap(receipts);
            m_records.swap(records);
            m_asset_bytes += K_BYTES + K_PREFIX.size();
            observation.m_frame.m_pixels.m_bytes.reset();
            return K_TICKET;
        } catch (const std::bad_alloc&) {
            K_ROLLBACK();
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "Batch observation allocation failed");
        } catch (const std::exception& exception) {
            K_ROLLBACK();
            return fail(ErrorCode::IO_ERROR, exception.what());
        }
    }

    Result<void> finish() override {
        if (m_failure) {
            return std::unexpected(*m_failure);
        }
        if (m_dataset) {
            return {};
        }
        if (m_cancellation.stop_requested()) {
            return fail(ErrorCode::CANCELLED, "Batch poses cancelled");
        }
        if (!m_started || m_receipts.size() != m_catalog.m_observations.size()) {
            return fail(ErrorCode::INVALID_ARGUMENT,
                        "Batch finish requires all selected observations");
        }
        m_closed = true;
        const auto K_SOLVED = solve();
        if (!K_SOLVED) {
            m_failure = K_SOLVED.error();
            return std::unexpected(*m_failure);
        }
        m_dataset = *K_SOLVED;
        return {};
    }

    Result<std::vector<PoseEvent>> drain_events(EventBudget budget) override {
        try {
            if (m_cancellation.stop_requested()) {
                return fail(ErrorCode::CANCELLED, "Batch pose events cancelled");
            }
            if (!m_started) {
                return fail(ErrorCode::INVALID_ARGUMENT, "Batch poses have not started");
            }
            if (m_failure) {
                return std::unexpected(*m_failure);
            }
            std::vector<PoseEvent> events;
            auto cursor = m_cursor;
            const auto K_AVAILABLE = m_dataset ? m_dataset->m_poses.size() + 5 : 1;
            events.reserve(std::min(budget.m_count, K_AVAILABLE));
            while (events.size() < budget.m_count && cursor < K_AVAILABLE) {
                if (cursor == 0) {
                    events.emplace_back(TrackingChange { TrackingState::INITIALIZING });
                } else if (cursor == 1) {
                    events.emplace_back(SubmapCreated { m_dataset->m_submap });
                } else if (cursor == 2) {
                    events.emplace_back(
                        MapAlignment { m_dataset->m_submap, m_dataset->m_revision, {} });
                } else if (cursor < m_dataset->m_poses.size() + 3) {
                    events.emplace_back(m_dataset->m_poses[cursor - 3]);
                } else if (cursor == m_dataset->m_poses.size() + 3) {
                    events.emplace_back(TrackingChange { TrackingState::FINISHED });
                } else {
                    events.emplace_back(EstimatorFinished {});
                }
                ++cursor;
            }
            m_cursor = cursor;
            return events;
        } catch (const std::bad_alloc&) {
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "Batch event allocation failed");
        }
    }

    void request_stop() noexcept override {
        m_cancellation.request_stop();
    }

    Result<std::shared_ptr<const CompletedPoseDataset>> acquire_dataset() const override {
        if (m_failure) {
            return std::unexpected(*m_failure);
        }
        if (!m_dataset) {
            return fail(ErrorCode::WOULD_BLOCK, "Batch poses are not complete");
        }
        return m_dataset;
    }

  private:
    Result<std::shared_ptr<const CompletedPoseDataset>> solve() {
        try {
            if (!output_directory(m_config.m_job_directory) ||
                !output_directory(m_config.m_job_directory / "input")) {
                return fail(ErrorCode::INVALID_DATA, "Batch output directory changed or is linked");
            }
            const auto K_JOURNAL = m_config.m_job_directory / "input" / "journal.json";
            publish_json(K_JOURNAL,
                         { { "schema_version", 1 },
                          { "profile", "native_rgb_journal_v1" },
                          { "source_sha256", m_config.m_source_sha256 },
                          { "decode_completion_sha256", m_catalog.m_completion_sha256 },
                          { "source_frame_count", m_metadata.at("source_frame_count") },
                          { "frames", m_records } },
                         m_config.m_resume_input);
            const auto K_DIGEST = sha256_file(K_JOURNAL, K_MAX_JSON, m_cancellation.get_token());
            if (!K_DIGEST) {
                return std::unexpected(K_DIGEST.error());
            }
            const auto K_REQUEST = m_config.m_job_directory / "batch-request.json";
            publish_json(
                K_REQUEST,
                { { "schema_version", 1 },
                 { "journal", utf8(K_JOURNAL) },
                 { "journal_sha256", *K_DIGEST },
                 { "job_dir", utf8(m_config.m_job_directory) },
                 { "pose_python", m_config.m_pose_python },
                 { "pose_recipe",
                  m_config.m_recipe == PoseRecipe::SEQUENTIAL ? "sequential" : "exhaustive_guided" },
                 { "limits", { { "cpu_threads", m_config.m_cpu_threads } } } },
                m_config.m_resume_input);
            std::optional<Error> worker_error;
            const auto K_OBSERVE = [&](const WorkerEvent& event) {
                if (const auto* handshake = std::get_if<WorkerHandshake>(&event)) {
                    if (!handshake->m_batch || handshake->m_streaming_tracking ||
                        handshake->m_max_frames < m_catalog.m_observations.size()) {
                        worker_error = Error { ErrorCode::UNSUPPORTED_OPERATION,
                                              "Worker lacks batch pose capacity" };
                        m_cancellation.request_stop();
                    }
                }
                if (const auto* failure = std::get_if<WorkerFailure>(&event)) {
                    if (!worker_error) {
                        const auto K_CODE =
                            failure->m_code == "CANCELLED"        ? ErrorCode::CANCELLED
                            : failure->m_code == "RESOURCE_LIMIT" ? ErrorCode::RESOURCE_EXHAUSTED
                            : failure->m_code == "UNSUPPORTED"    ? ErrorCode::UNSUPPORTED_OPERATION
                            : failure->m_code == "BAD_INPUT" || failure->m_code == "CORRUPT_ASSET"
                                ? ErrorCode::INVALID_DATA
                                : ErrorCode::BACKEND_FAILURE;
                        worker_error = Error { K_CODE, failure->m_code + ": " + failure->m_message };
                    }
                }
                if (const auto* completed = std::get_if<WorkerCompleted>(&event);
                    completed && completed->m_stage != "poses") {
                    worker_error =
                        Error { ErrorCode::INVALID_DATA, "Worker completed the wrong batch stage" };
                }
                if (m_config.m_observed) {
                    m_config.m_observed(event);
                }
            };
            const auto K_EXECUTED =
                execute_worker_protocol(m_config.m_worker_python,
                                        m_config.m_worker_script,
                                        { "solve-journal", "--request", utf8(K_REQUEST) },
                                        m_cancellation.get_token(),
                                        m_config.m_log,
                                        K_OBSERVE);
            if (worker_error) {
                return std::unexpected(*worker_error);
            }
            if (!K_EXECUTED) {
                return std::unexpected(K_EXECUTED.error());
            }
            if (*K_EXECUTED != 0) {
                return fail(ErrorCode::BACKEND_FAILURE, "Batch pose worker failed");
            }
            return load_completed_poses({ m_config.m_job_directory, m_config.m_source_sha256 },
                                        m_cancellation.get_token());
        } catch (const std::bad_alloc&) {
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "Batch solve allocation failed");
        } catch (const std::exception& exception) {
            return fail(ErrorCode::IO_ERROR, exception.what());
        }
    }

    struct Receipt {
        std::string m_header, m_pixels;
        ObservationTicket m_ticket;
        std::size_t m_bytes {};
    };

    BatchPoseConfig m_config;
    DecodedFrameCatalog m_catalog;
    nlohmann::json m_metadata;
    nlohmann::json m_records = nlohmann::json::array();
    std::map<OperationId, Receipt> m_receipts;
    std::shared_ptr<const CompletedPoseDataset> m_dataset;
    std::optional<Error> m_failure;
    std::stop_source m_cancellation;
    ByteBudget m_memory {};
    std::size_t m_asset_bytes {}, m_cursor {};
    bool m_started {}, m_closed {};
};

}  // namespace

Result<std::unique_ptr<BatchPoseEstimator>> make_batch_pose_estimator(BatchPoseConfig config,
                                                                      const std::stop_token& stop) {
    try {
        if (!adapter_detail::valid_digest(config.m_source_sha256) ||
            config.m_job_directory.empty() || config.m_worker_python.empty() ||
            config.m_pose_python.empty() || config.m_worker_python.size() > 4096 ||
            config.m_pose_python.size() > 4096 ||
            config.m_worker_python.find('\0') != std::string::npos ||
            config.m_pose_python.find('\0') != std::string::npos || config.m_cpu_threads == 0 ||
            config.m_cpu_threads > 4 || config.m_journal_bytes.m_bytes <= K_METADATA_BYTES ||
            config.m_journal_bytes.m_bytes > 32ULL * 1024 * 1024 * 1024 ||
            (config.m_recipe != PoseRecipe::SEQUENTIAL &&
             config.m_recipe != PoseRecipe::EXHAUSTIVE_GUIDED) ||
            !std::filesystem::is_regular_file(config.m_worker_script)) {
            return fail(ErrorCode::INVALID_ARGUMENT, "Invalid batch pose configuration");
        }
        const auto K_CATALOG = inspect_decoded_frame_source(
            { .m_stage_directory = config.m_decode_stage, .m_source_sha256 = config.m_source_sha256 },
            stop);
        if (!K_CATALOG) {
            return std::unexpected(K_CATALOG.error());
        }
        if (K_CATALOG->m_observations.size() < 3) {
            return fail(ErrorCode::INVALID_DATA,
                        "Batch poses require at least three selected frames");
        }
        if (std::filesystem::is_symlink(config.m_job_directory)) {
            return fail(ErrorCode::INVALID_DATA, "Batch job may not be a symlink");
        }
        config.m_job_directory = std::filesystem::weakly_canonical(config.m_job_directory);
        config.m_decode_stage = std::filesystem::canonical(config.m_decode_stage);
        auto metadata =
            adapter_detail::read_json(config.m_decode_stage / "result.json", K_MAX_JSON);
        const auto K_COMPLETION = adapter_detail::read_json(
            config.m_decode_stage / "completion.json", K_MAX_JSON, K_CATALOG->m_completion_sha256);
        for (const auto& asset : K_COMPLETION.at("files")) {
            if (asset.at("path") == "result.json") {
                metadata = adapter_detail::read_json(config.m_decode_stage / "result.json",
                                                     K_MAX_JSON,
                                                     asset.at("sha256").get<std::string>());
            }
        }
        nlohmann::json selected = { { "source_frame_count", metadata.at("source_frame_count") },
                                   { "frames", nlohmann::json::array() } };
        for (const auto& frame : metadata.at("frames")) {
            nlohmann::json record;
            for (const auto* field : { "frame_id",
                                      "calibration_id",
                                      "source_index",
                                      "pts",
                                      "time_base",
                                      "capture_time_ns" }) {
                record[field] = frame.at(field);
            }
            selected["frames"].push_back(std::move(record));
        }
        return std::unique_ptr<BatchPoseEstimator>(std::make_unique<ColmapBatchEstimator>(
            std::move(config), *K_CATALOG, std::move(selected)));
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Batch pose configuration allocation failed");
    } catch (const std::exception& exception) {
        return fail(ErrorCode::INVALID_DATA, exception.what());
    }
}

}  // namespace world_explorer
