#include "world_explorer/application/video_decode.hpp"

#include "job_validation.hpp"
#include "owned_job.hpp"
#include "video_preflight_detail.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/reconstruction_adapters/worker_artifacts.hpp"
#include "world_explorer/reconstruction_adapters/worker_protocol.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <string_view>

namespace world_explorer {

Result<DecodedVideo> adopt_video_decode(const RetainedVideoDecode& selected,
                                        const std::string& source_sha256,
                                        const std::stop_token& stop) {
    try {
        if (!job_detail::valid_digest(selected.m_completion_sha256) ||
            !job_detail::valid_digest(source_sha256)) {
            return fail(ErrorCode::INVALID_ARGUMENT, "Invalid selected decode/source digest");
        }
        const auto K_STAGE = std::filesystem::canonical(selected.m_stage);
        const auto K_NAME = K_STAGE.filename().string();
        if (std::filesystem::is_symlink(selected.m_stage) ||
            K_STAGE.parent_path().filename() != "stages" || !K_NAME.starts_with("decode-") ||
            !job_detail::valid_digest(K_NAME.substr(7))) {
            return fail(ErrorCode::INVALID_DATA, "Select a committed decode stage explicitly");
        }
        const auto K_VERIFIED = verify_committed_stage(K_STAGE, K_NAME.substr(7), stop);
        if (!K_VERIFIED) {
            return std::unexpected(K_VERIFIED.error());
        }
        if (K_VERIFIED->m_completion_sha256 != selected.m_completion_sha256 ||
            !K_VERIFIED->m_assets.contains("result.json")) {
            return fail(ErrorCode::INVALID_DATA, "Decode completion differs from selection");
        }
        const auto K_CATALOG = inspect_decoded_frame_source(
            { .m_stage_directory = K_STAGE, .m_source_sha256 = source_sha256 }, stop);
        if (!K_CATALOG) {
            return std::unexpected(K_CATALOG.error());
        }
        if (K_CATALOG->m_completion_sha256 != selected.m_completion_sha256) {
            return fail(ErrorCode::INVALID_DATA, "Decode changed during catalog admission");
        }
        return DecodedVideo { { K_STAGE, selected.m_completion_sha256 }, *K_CATALOG };
    } catch (const std::exception& exception) {
        return fail(ErrorCode::INVALID_DATA,
                    "Decode admission failed: " + std::string(exception.what()));
    }
}

Result<DecodedVideo> decode_video(const VideoDecodeRequest& request,
                                  const std::stop_token& stop,
                                  const LogCallback& log,
                                  const WorkerEventCallback& observed) {
    try {
        const auto& config = request.m_configuration;
        const auto K_CALIBRATION =
            config.m_frame_selection == FrameSelection::QUALITY_KEYFRAMES_CANDIDATE_V3;
        const auto K_TRACKING = K_CALIBRATION || config.m_frame_selection ==
                                                     FrameSelection::QUALITY_KEYFRAMES_CANDIDATE_V2;
        const auto K_QUALITY = K_TRACKING || config.m_frame_selection ==
                                                 FrameSelection::QUALITY_KEYFRAMES_CANDIDATE_V1;
        const std::string_view K_SELECTION { K_CALIBRATION ? "quality_keyframes_candidate_v3"
                                            : K_TRACKING  ? "quality_keyframes_candidate_v2"
                                            : K_QUALITY   ? "quality_keyframes_candidate_v1"
                                                          : "timestamp_sampling_v1" };
        if (stop.stop_requested()) {
            return fail(ErrorCode::CANCELLED, "Video decode cancelled");
        }
        if (config.m_job_directory.empty() || config.m_worker.m_script.empty() ||
            config.m_worker.m_python.empty() || config.m_worker.m_python.size() > 4096 ||
            config.m_worker.m_python.find('\0') != std::string::npos ||
            !config.m_worker.m_framed_protocol || !std::isfinite(config.m_sample_fps) ||
            config.m_sample_fps < .1 || config.m_sample_fps > 4 || config.m_max_frames < 3 ||
            config.m_max_frames > 120 || !config.m_cpu_threads || config.m_cpu_threads > 4 ||
            !config.m_max_output_bytes || config.m_max_output_bytes > 32ULL * 1024 * 1024 * 1024 ||
            (config.m_frame_selection != FrameSelection::TIMESTAMP_SAMPLING_V1 && !K_QUALITY) ||
            (K_QUALITY && config.m_sample_fps < 2)) {
            return fail(ErrorCode::INVALID_ARGUMENT, "Invalid explicit video decode configuration");
        }
        const auto K_PREFLIGHT =
            preflight_video({ request.m_video, request.m_inspection, request.m_outputs }, stop);
        if (!K_PREFLIGHT) {
            return std::unexpected(K_PREFLIGHT.error());
        }
        if (std::filesystem::is_symlink(config.m_job_directory)) {
            return fail(ErrorCode::INVALID_DATA, "Decode job may not be a symlink");
        }
        const auto K_DIRECTORY = std::filesystem::weakly_canonical(config.m_job_directory);
        const auto K_VIDEO = std::filesystem::canonical(request.m_video);
        const auto K_SCRIPT = std::filesystem::canonical(config.m_worker.m_script);
        std::vector<std::filesystem::path> helper_paths { K_SCRIPT.parent_path() /
                                                         (K_CALIBRATION ? "frame_calibration.py"
                                                          : K_TRACKING  ? "frame_tracking.py"
                                                                        : "frame_quality.py") };
        if (K_CALIBRATION) {
            helper_paths.push_back(K_SCRIPT.parent_path() / "frame_tracking.py");
        }
        const auto K_HELPER_IDENTITY = [&]() -> Result<std::string> {
            nlohmann::json files = nlohmann::json::object();
            for (const auto& path : helper_paths) {
                const auto K_HASH = sha256_file(path, 1024uz * 1024uz, stop);
                if (!K_HASH) {
                    return std::unexpected(K_HASH.error());
                }
                files[path.filename().string()] = *K_HASH;
            }
            if (!K_CALIBRATION) {
                return files.begin().value().get<std::string>();
            }
            const auto K_BYTES = files.dump();
            return sha256(std::as_bytes(std::span(K_BYTES)));
        };
        const auto K_INSPECTION_JOB =
            std::filesystem::canonical(request.m_inspection.m_stage).parent_path().parent_path();
        auto protected_paths = request.m_outputs;
        protected_paths.insert(protected_paths.end(), { K_VIDEO, K_SCRIPT, K_INSPECTION_JOB });
        if (K_QUALITY) {
            protected_paths.insert(protected_paths.end(), helper_paths.begin(), helper_paths.end());
        }
        if (std::filesystem::path(config.m_worker.m_python).has_parent_path()) {
            protected_paths.emplace_back(
                std::filesystem::weakly_canonical(config.m_worker.m_python));
        }
        for (const auto& path : protected_paths) {
            if (video_detail::overlaps(K_DIRECTORY, std::filesystem::weakly_canonical(path))) {
                return fail(ErrorCode::INVALID_ARGUMENT,
                            "Decode job overlaps another input/output");
            }
        }
        auto storage_paths = request.m_outputs;
        storage_paths.push_back(K_DIRECTORY);
        const auto K_STORAGE = video_detail::check_storage(
            storage_paths, request.m_inspection.m_minimum_free_bytes, stop);
        if (!K_STORAGE) {
            return std::unexpected(K_STORAGE.error());
        }
        const auto K_SCRIPT_HASH = sha256_file(K_SCRIPT, 16uz * 1024 * 1024, stop);
        if (!K_SCRIPT_HASH) {
            return std::unexpected(K_SCRIPT_HASH.error());
        }
        nlohmann::json parameters { { "sample_fps", config.m_sample_fps },
                                   { "max_frames", config.m_max_frames },
                                   { "cpu_threads", config.m_cpu_threads },
                                   { "max_output_bytes", config.m_max_output_bytes } };
        if (K_QUALITY) {
            const auto K_HELPER = K_HELPER_IDENTITY();
            if (!K_HELPER) {
                return std::unexpected(K_HELPER.error());
            }
            parameters["frame_selection"] = K_SELECTION;
            parameters["selection_implementation_sha256"] = *K_HELPER;
        }
        const nlohmann::json K_OWNER {
            { "schema_version", 1 },
            { "role", "video_decode" },
            { "protocol_major", 2 },
            { "source_sha256", K_PREFLIGHT->m_source_sha256 },
            { "inspection_sha256", request.m_inspection.m_completion_sha256 },
            { "worker_sha256", *K_SCRIPT_HASH },
            { "worker_script", K_SCRIPT.string() },
            { "python", config.m_worker.m_python },
            { "parameters", parameters } };
        auto lock = job_detail::open_owned_job(K_DIRECTORY,
                                               { ".we-decode-owner.json", ".we-decode.lock" },
                                               K_OWNER,
                                               { "decode-request.json",
                                                "request.json",
                                                "capabilities.json",
                                                "result.json",
                                                "inspect.json",
                                                "decode.json",
                                                "stages",
                                                "job.lock" },
                                               stop);
        if (!lock) {
            return std::unexpected(lock.error());
        }
        if (std::filesystem::exists(std::filesystem::symlink_status(K_DIRECTORY / "job.lock"))) {
            return fail(ErrorCode::WOULD_BLOCK,
                        "Decode worker lock exists; verify its process is gone before recovery");
        }
        nlohmann::json worker_request { { "schema_version", 1 },
                                       { "source", K_VIDEO.string() },
                                       { "job_dir", K_DIRECTORY.string() },
                                       { "stop_after", "decode" },
                                       { "sample_fps", config.m_sample_fps },
                                       { "limits",
                                        { { "max_frames", config.m_max_frames },
                                         { "cpu_threads", config.m_cpu_threads },
                                         { "max_output_bytes", config.m_max_output_bytes } } } };
        if (K_QUALITY) {
            worker_request["frame_selection"] = K_SELECTION;
        }
        const auto K_PUBLISHED = job_detail::publish_owned_json(
            K_DIRECTORY / "decode-request.json", worker_request, stop);
        if (!K_PUBLISHED) {
            return std::unexpected(K_PUBLISHED.error());
        }
        if (log) {
            log(K_QUALITY
                    ? "Decoding local quality candidates (thresholds unqualified).\n"
                    : "Decoding selected video frames locally (timestamp sampling; diagnostics "
                      "unqualified).\n");
        }
        std::stop_source cancellation;
        std::stop_callback forward(stop, [&] {
            cancellation.request_stop();
        });
        std::optional<Error> failure;
        std::optional<WorkerCompleted> completion;
        const auto K_EXECUTED = execute_worker_protocol(
            config.m_worker.m_python,
            K_SCRIPT,
            { "run", "--request", (K_DIRECTORY / "decode-request.json").string() },
            cancellation.get_token(),
            log,
            [&](const WorkerEvent& event) {
                if (const auto* capability = std::get_if<WorkerHandshake>(&event)) {
                    if (!capability->m_batch ||
                        std::ranges::find(capability->m_sensors, std::string { "video" }) ==
                            capability->m_sensors.end() ||
                        capability->m_max_frames < config.m_max_frames ||
                        capability->m_max_source_bytes < K_PREFLIGHT->m_source_bytes ||
                        capability->m_max_output_bytes < config.m_max_output_bytes) {
                        failure = Error { ErrorCode::UNSUPPORTED_OPERATION,
                                         "Worker lacks video decode capacity" };
                        cancellation.request_stop();
                    }
                }
                if (const auto* value = std::get_if<WorkerCompleted>(&event)) {
                    completion = *value;
                }
                if (observed) {
                    observed(event);
                }
            });
        if (stop.stop_requested()) {
            return fail(ErrorCode::CANCELLED, "Decode cancelled; committed stages retained");
        }
        if (failure) {
            return std::unexpected(*failure);
        }
        if (!K_EXECUTED) {
            return std::unexpected(K_EXECUTED.error());
        }
        if (*K_EXECUTED != 0) {
            return fail(ErrorCode::BACKEND_FAILURE,
                        "Decode worker failed; previous scene retained");
        }
        if (!completion || completion->m_stage != "decode" || !completion->m_artifact) {
            return fail(ErrorCode::INVALID_DATA, "Worker did not return a decode artifact");
        }
        const auto& artifact = *completion->m_artifact;
        const auto K_STAGE = std::filesystem::canonical(artifact.m_path).parent_path();
        if (artifact.m_path != K_STAGE / "result.json" ||
            K_STAGE.parent_path() != K_DIRECTORY / "stages") {
            return fail(ErrorCode::INVALID_DATA, "Decode artifact escapes its owned job");
        }
        const auto K_DIGEST = sha256_file(K_STAGE / "completion.json", 16uz * 1024 * 1024, stop);
        if (!K_DIGEST) {
            return std::unexpected(K_DIGEST.error());
        }
        const auto K_CHECKED =
            adopt_video_decode({ K_STAGE, *K_DIGEST }, K_PREFLIGHT->m_source_sha256, stop);
        if (!K_CHECKED) {
            return std::unexpected(K_CHECKED.error());
        }
        const auto K_METADATA =
            job_detail::read_json(artifact.m_path, K_WORKER_MESSAGE_LIMIT, artifact.m_sha256);
        if (!K_METADATA ||
            K_METADATA->at("selection").get_ref<const std::string&>() != K_SELECTION) {
            return fail(ErrorCode::INVALID_DATA,
                        "Worker returned a different frame selection profile");
        }
        if (K_QUALITY) {
            const auto K_LEDGER =
                job_detail::read_json(K_STAGE / "quality.json",
                                      K_WORKER_MESSAGE_LIMIT,
                                      K_METADATA->at("quality_ledger_sha256").get<std::string>());
            const auto K_HELPER = K_HELPER_IDENTITY();
            if (!K_HELPER) {
                return std::unexpected(K_HELPER.error());
            }
            if (!K_LEDGER || K_LEDGER->at("implementation_sha256") != *K_HELPER ||
                parameters.at("selection_implementation_sha256") != *K_HELPER ||
                K_LEDGER->at("sample_fps") != config.m_sample_fps) {
                return fail(ErrorCode::INVALID_DATA,
                            "Quality implementation/rate changed during decode");
            }
        }
        const auto K_DIVISOR =
            std::gcd(K_PREFLIGHT->m_time_base_numerator, K_PREFLIGHT->m_time_base_denominator);
        const bool K_ROTATED = K_PREFLIGHT->m_rotation_degrees % 180 != 0;
        if (K_CHECKED->m_catalog.m_observations.size() > config.m_max_frames) {
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "Decode exceeds selected frame budget");
        }
        for (const auto& frame : K_CHECKED->m_catalog.m_observations) {
            if (frame.m_width != (K_ROTATED ? K_PREFLIGHT->m_height : K_PREFLIGHT->m_width) ||
                frame.m_height != (K_ROTATED ? K_PREFLIGHT->m_width : K_PREFLIGHT->m_height) ||
                frame.m_original_timestamp.m_time_base_numerator !=
                    K_PREFLIGHT->m_time_base_numerator / K_DIVISOR ||
                frame.m_original_timestamp.m_time_base_denominator !=
                    K_PREFLIGHT->m_time_base_denominator / K_DIVISOR) {
                return fail(ErrorCode::INVALID_DATA,
                            "Decoded dimensions/time base differ from inspected video");
            }
        }
        const auto K_FINAL_SOURCE = sha256_file(K_VIDEO, 16ULL * 1024 * 1024 * 1024, stop);
        const auto K_FINAL_SCRIPT = sha256_file(K_SCRIPT, 16uz * 1024 * 1024, stop);
        const auto K_FINAL_RESULT = sha256_file(artifact.m_path, K_WORKER_MESSAGE_LIMIT, stop);
        if (!K_FINAL_SOURCE) {
            return std::unexpected(K_FINAL_SOURCE.error());
        }
        if (!K_FINAL_SCRIPT) {
            return std::unexpected(K_FINAL_SCRIPT.error());
        }
        if (!K_FINAL_RESULT) {
            return std::unexpected(K_FINAL_RESULT.error());
        }
        if (*K_FINAL_SOURCE != K_PREFLIGHT->m_source_sha256 || *K_FINAL_SCRIPT != *K_SCRIPT_HASH ||
            *K_FINAL_RESULT != artifact.m_sha256) {
            return fail(ErrorCode::INVALID_DATA,
                        "Decode source/worker/artifact changed during execution");
        }
        return *K_CHECKED;
    } catch (const std::exception& exception) {
        return fail(ErrorCode::INVALID_DATA,
                    "Video decode failed: " + std::string(exception.what()));
    }
}

}  // namespace world_explorer
