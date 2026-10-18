#include "job_validation.hpp"
#include "owned_job.hpp"
#include "video_preflight_detail.hpp"
#include "world_explorer/application/video_preflight.hpp"
#include "world_explorer/core/file_lock.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/reconstruction_adapters/worker_protocol.hpp"

#include <algorithm>
#include <new>

namespace world_explorer {
namespace {

constexpr std::size_t K_MAX_JSON { 64uz * 1024uz };
constexpr std::uint64_t K_MAX_SOURCE { 16ULL * 1024 * 1024 * 1024 };
constexpr std::size_t K_MAX_SCRIPT { 16uz * 1024uz * 1024uz };
constexpr std::string_view K_OWNER_FILE { ".we-inspection-owner.json" };
constexpr std::string_view K_LOCK_FILE { ".we-inspection.lock" };

}  // namespace

Result<InspectedVideo> inspect_video(const VideoInspectionRequest& request,
                                     const std::stop_token& stop,
                                     const LogCallback& log,
                                     const WorkerEventCallback& observed) {
    try {
        if (stop.stop_requested()) return fail(ErrorCode::CANCELLED, "Video inspection cancelled");
        const auto& config = request.m_configuration;
        if (request.m_video.empty() || config.m_job_directory.empty() ||
            config.m_worker.m_script.empty() || config.m_worker.m_python.empty() ||
            config.m_worker.m_python.size() > 4096 ||
            config.m_worker.m_python.find('\0') != std::string::npos ||
            !config.m_worker.m_framed_protocol || request.m_outputs.empty() ||
            request.m_outputs.size() > 3)
            return fail(ErrorCode::INVALID_ARGUMENT, "Invalid local metadata inspection request");
        if (std::filesystem::is_symlink(config.m_job_directory))
            return fail(ErrorCode::INVALID_DATA, "Inspection job may not be a symlink");
        const auto K_VIDEO = std::filesystem::canonical(request.m_video);
        const auto K_SCRIPT = std::filesystem::canonical(config.m_worker.m_script);
        const auto K_DIRECTORY = std::filesystem::weakly_canonical(config.m_job_directory);
        for (const auto& input : {K_VIDEO, K_SCRIPT})
            if (video_detail::overlaps(K_DIRECTORY, input))
                return fail(ErrorCode::INVALID_ARGUMENT,
                            "Inspection output overlaps source or worker");
        for (const auto& output : request.m_outputs) {
            const auto K_SELECTED = std::filesystem::weakly_canonical(output);
            if (output.empty() || video_detail::overlaps(K_DIRECTORY, K_SELECTED) ||
                video_detail::overlaps(K_VIDEO, K_SELECTED) ||
                video_detail::overlaps(K_SCRIPT, K_SELECTED))
                return fail(ErrorCode::INVALID_ARGUMENT,
                            "Inspection output overlaps another input/output");
        }
        if (std::filesystem::path(config.m_worker.m_python).has_parent_path() &&
            video_detail::overlaps(K_DIRECTORY,
                                   std::filesystem::weakly_canonical(config.m_worker.m_python)))
            return fail(ErrorCode::INVALID_ARGUMENT,
                        "Inspection output overlaps its Python executable");
        auto storage_paths = request.m_outputs;
        storage_paths.push_back(K_DIRECTORY);
        const auto K_STORAGE =
            video_detail::check_storage(storage_paths, config.m_minimum_free_bytes, stop);
        if (!K_STORAGE) return std::unexpected(K_STORAGE.error());
        const auto K_SOURCE_HASH = sha256_file(K_VIDEO, K_MAX_SOURCE, stop);
        const auto K_SCRIPT_HASH = sha256_file(K_SCRIPT, K_MAX_SCRIPT, stop);
        if (!K_SOURCE_HASH) return std::unexpected(K_SOURCE_HASH.error());
        if (!K_SCRIPT_HASH) return std::unexpected(K_SCRIPT_HASH.error());
        const nlohmann::json K_OWNER {{"schema_version", 1},
                                      {"role", "video_inspection"},
                                      {"protocol_major", 2},
                                      {"source_sha256", *K_SOURCE_HASH},
                                      {"worker_sha256", *K_SCRIPT_HASH},
                                      {"worker_script", K_SCRIPT.string()},
                                      {"python", config.m_worker.m_python}};
        auto lock =
            job_detail::open_owned_job(K_DIRECTORY,
                                       {std::string(K_OWNER_FILE), std::string(K_LOCK_FILE)},
                                       K_OWNER,
                                       {"request.json",
                                        "capabilities.json",
                                        "result.json",
                                        "inspect.json",
                                        "stages",
                                        "job.lock"},
                                       stop);
        if (!lock) return std::unexpected(lock.error());
        if (std::filesystem::exists(std::filesystem::symlink_status(K_DIRECTORY / "job.lock")))
            return fail(ErrorCode::WOULD_BLOCK,
                        "Inspection worker lock exists; verify its "
                        "recorded process is gone before recovery");
        if (stop.stop_requested())
            return fail(ErrorCode::CANCELLED, "Video inspection cancelled before worker start");
        if (log) log("Inspecting selected video locally; no RGB decode or pose solve.\n");
        std::stop_source cancellation;
        std::stop_callback forward(stop, [&] { cancellation.request_stop(); });
        std::optional<Error> capability_error;
        std::optional<WorkerCompleted> completion;
        const auto K_SOURCE_BYTES = std::filesystem::file_size(K_VIDEO);
        const auto K_EXECUTED = execute_worker_protocol(
            config.m_worker.m_python,
            K_SCRIPT,
            {"inspect", "--source", K_VIDEO.string(), "--job-dir", K_DIRECTORY.string()},
            cancellation.get_token(),
            log,
            [&](const WorkerEvent& event) {
                if (const auto* capability = std::get_if<WorkerHandshake>(&event)) {
                    if (!capability->m_batch ||
                        std::ranges::find(capability->m_sensors, std::string { "video" }) ==
                            capability->m_sensors.end() ||
                        capability->m_max_source_bytes < K_SOURCE_BYTES ||
                        capability->m_max_output_bytes < K_MAX_SCRIPT) {
                        capability_error = Error {
                            ErrorCode::UNSUPPORTED_OPERATION,
                            "Worker lacks video batch capability or required resource limits"};
                        cancellation.request_stop();
                    }
                }
                if (const auto* completed = std::get_if<WorkerCompleted>(&event))
                    completion = *completed;
                if (observed) observed(event);
            });
        if (stop.stop_requested())
            return fail(ErrorCode::CANCELLED,
                        "Video inspection cancelled; committed metadata retained");
        if (capability_error) return std::unexpected(*capability_error);
        if (!K_EXECUTED) return std::unexpected(K_EXECUTED.error());
        if (*K_EXECUTED != 0)
            return fail(ErrorCode::BACKEND_FAILURE,
                        "Metadata inspection worker failed; retained output is unchanged");
        if (!completion || completion->m_stage != "inspect" || !completion->m_artifact ||
            completion->m_artifact->m_bytes > K_MAX_JSON)
            return fail(ErrorCode::INVALID_DATA,
                        "Worker did not return a bounded inspection artifact");
        const auto& artifact = *completion->m_artifact;
        const auto K_STAGE = std::filesystem::canonical(artifact.m_path).parent_path();
        const auto K_NAME = K_STAGE.filename().string();
        if (artifact.m_path != K_STAGE / "result.json" ||
            K_STAGE.parent_path() != K_DIRECTORY / "stages" || !K_NAME.starts_with("inspect-") ||
            !job_detail::valid_digest(K_NAME.substr(8)))
            return fail(ErrorCode::INVALID_DATA,
                        "Inspection artifact escapes its owned metadata job");
        const auto K_DIGEST = sha256_file(K_STAGE / "completion.json", K_MAX_JSON, stop);
        if (!K_DIGEST) return std::unexpected(K_DIGEST.error());
        RetainedVideoInspection selected { K_STAGE, *K_DIGEST, config.m_minimum_free_bytes };
        const auto K_CHECKED = preflight_video({K_VIDEO, selected, request.m_outputs}, stop, log);
        if (!K_CHECKED) return std::unexpected(K_CHECKED.error());
        const auto K_FINAL_SCRIPT = sha256_file(K_SCRIPT, K_MAX_SCRIPT, stop);
        const auto K_FINAL_RESULT = sha256_file(artifact.m_path, K_MAX_JSON, stop);
        if (!K_FINAL_SCRIPT) return std::unexpected(K_FINAL_SCRIPT.error());
        if (!K_FINAL_RESULT) return std::unexpected(K_FINAL_RESULT.error());
        if (*K_FINAL_SCRIPT != *K_SCRIPT_HASH || *K_FINAL_RESULT != artifact.m_sha256 ||
            K_CHECKED->m_source_sha256 != *K_SOURCE_HASH ||
            std::filesystem::file_size(artifact.m_path) != artifact.m_bytes)
            return fail(ErrorCode::INVALID_DATA,
                        "Inspection inputs or completion changed during execution");
        return InspectedVideo { std::move(selected), *K_CHECKED };
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Video inspection allocation failed");
    } catch (const std::exception& exception) {
        return fail(ErrorCode::INVALID_DATA,
                    "Video inspection failed: " + std::string(exception.what()));
    }
}

}  // namespace world_explorer
