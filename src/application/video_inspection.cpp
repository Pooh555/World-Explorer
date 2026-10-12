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
constexpr std::size_t MAX_JSON = std::size_t{64} * 1024;
constexpr std::uint64_t MAX_SOURCE = 16ULL * 1024 * 1024 * 1024;
constexpr std::size_t MAX_SCRIPT = std::size_t{16} * 1024 * 1024;
constexpr std::string_view OWNER_FILE = ".we-inspection-owner.json";
constexpr std::string_view LOCK_FILE = ".we-inspection.lock";

} // namespace

result<inspected_video> inspect_video(const video_inspection_request& request,
                                      const std::stop_token& stop, const log_callback& log) {
    try {
        if (stop.stop_requested())
            return fail(error_code::CANCELLED, "Video inspection cancelled");
        const auto& config = request.configuration;
        if (request.video.empty() || config.job_directory.empty() || config.worker.script.empty() ||
            config.worker.python.empty() || config.worker.python.size() > 4096 ||
            config.worker.python.find('\0') != std::string::npos ||
            !config.worker.framed_protocol || request.outputs.empty() || request.outputs.size() > 3)
            return fail(error_code::INVALID_ARGUMENT, "Invalid local metadata inspection request");
        if (std::filesystem::is_symlink(config.job_directory))
            return fail(error_code::INVALID_DATA, "Inspection job may not be a symlink");
        const auto video = std::filesystem::canonical(request.video);
        const auto script = std::filesystem::canonical(config.worker.script);
        const auto directory = std::filesystem::weakly_canonical(config.job_directory);
        for (const auto& input : {video, script})
            if (video_detail::overlaps(directory, input))
                return fail(error_code::INVALID_ARGUMENT,
                            "Inspection output overlaps source or worker");
        for (const auto& output : request.outputs) {
            const auto selected = std::filesystem::weakly_canonical(output);
            if (output.empty() || video_detail::overlaps(directory, selected) ||
                video_detail::overlaps(video, selected) || video_detail::overlaps(script, selected))
                return fail(error_code::INVALID_ARGUMENT,
                            "Inspection output overlaps another input/output");
        }
        if (std::filesystem::path(config.worker.python).has_parent_path() &&
            video_detail::overlaps(directory,
                                   std::filesystem::weakly_canonical(config.worker.python)))
            return fail(error_code::INVALID_ARGUMENT,
                        "Inspection output overlaps its Python executable");
        auto storage_paths = request.outputs;
        storage_paths.push_back(directory);
        const auto storage =
            video_detail::check_storage(storage_paths, config.minimum_free_bytes, stop);
        if (!storage)
            return std::unexpected(storage.error());
        const auto source_hash = sha256_file(video, MAX_SOURCE, stop);
        const auto script_hash = sha256_file(script, MAX_SCRIPT, stop);
        if (!source_hash)
            return std::unexpected(source_hash.error());
        if (!script_hash)
            return std::unexpected(script_hash.error());
        const nlohmann::json owner{
            {"schema_version", 1},           {"role", "video_inspection"},
            {"protocol_major", 2},           {"source_sha256", *source_hash},
            {"worker_sha256", *script_hash}, {"worker_script", script.string()},
            {"python", config.worker.python}};
        auto lock = job_detail::open_owned_job(
            directory, {std::string(OWNER_FILE), std::string(LOCK_FILE)}, owner,
            {"request.json", "capabilities.json", "result.json", "inspect.json", "stages",
             "job.lock"},
            stop);
        if (!lock)
            return std::unexpected(lock.error());
        if (std::filesystem::exists(std::filesystem::symlink_status(directory / "job.lock")))
            return fail(error_code::WOULD_BLOCK, "Inspection worker lock exists; verify its "
                                                 "recorded process is gone before recovery");
        if (stop.stop_requested())
            return fail(error_code::CANCELLED, "Video inspection cancelled before worker start");
        if (log)
            log("Inspecting selected video locally; no RGB decode or pose solve.\n");
        std::stop_source cancellation;
        std::stop_callback forward(stop, [&] { cancellation.request_stop(); });
        std::optional<error> capability_error;
        std::optional<worker_completed> completion;
        const auto source_bytes = std::filesystem::file_size(video);
        const auto executed = execute_worker_protocol(
            config.worker.python, script,
            {"inspect", "--source", video.string(), "--job-dir", directory.string()},
            cancellation.get_token(), log, [&](const worker_event& event) {
                if (const auto* capability = std::get_if<worker_handshake>(&event)) {
                    if (!capability->batch ||
                        std::ranges::find(capability->sensors, std::string{"video"}) ==
                            capability->sensors.end() ||
                        capability->max_source_bytes < source_bytes ||
                        capability->max_output_bytes < MAX_SCRIPT) {
                        capability_error = error{
                            error_code::UNSUPPORTED_OPERATION,
                            "Worker lacks video batch capability or required resource limits"};
                        cancellation.request_stop();
                    }
                }
                if (const auto* completed = std::get_if<worker_completed>(&event))
                    completion = *completed;
            });
        if (stop.stop_requested())
            return fail(error_code::CANCELLED,
                        "Video inspection cancelled; committed metadata retained");
        if (capability_error)
            return std::unexpected(*capability_error);
        if (!executed)
            return std::unexpected(executed.error());
        if (*executed != 0)
            return fail(error_code::BACKEND_FAILURE,
                        "Metadata inspection worker failed; retained output is unchanged");
        if (!completion || completion->stage != "inspect" || !completion->artifact ||
            completion->artifact->bytes > MAX_JSON)
            return fail(error_code::INVALID_DATA,
                        "Worker did not return a bounded inspection artifact");
        const auto& artifact = *completion->artifact;
        const auto stage = std::filesystem::canonical(artifact.path).parent_path();
        const auto name = stage.filename().string();
        if (artifact.path != stage / "result.json" || stage.parent_path() != directory / "stages" ||
            !name.starts_with("inspect-") || !job_detail::valid_digest(name.substr(8)))
            return fail(error_code::INVALID_DATA,
                        "Inspection artifact escapes its owned metadata job");
        const auto digest = sha256_file(stage / "completion.json", MAX_JSON, stop);
        if (!digest)
            return std::unexpected(digest.error());
        retained_video_inspection selected{stage, *digest, config.minimum_free_bytes};
        const auto checked = preflight_video({video, selected, request.outputs}, stop, log);
        if (!checked)
            return std::unexpected(checked.error());
        const auto final_script = sha256_file(script, MAX_SCRIPT, stop);
        const auto final_result = sha256_file(artifact.path, MAX_JSON, stop);
        if (!final_script)
            return std::unexpected(final_script.error());
        if (!final_result)
            return std::unexpected(final_result.error());
        if (*final_script != *script_hash || *final_result != artifact.sha256 ||
            checked->source_sha256 != *source_hash ||
            std::filesystem::file_size(artifact.path) != artifact.bytes)
            return fail(error_code::INVALID_DATA,
                        "Inspection inputs or completion changed during execution");
        return inspected_video{std::move(selected), *checked};
    } catch (const std::bad_alloc&) {
        return fail(error_code::RESOURCE_EXHAUSTED, "Video inspection allocation failed");
    } catch (const std::exception& exception) {
        return fail(error_code::INVALID_DATA,
                    "Video inspection failed: " + std::string(exception.what()));
    }
}
} // namespace world_explorer
