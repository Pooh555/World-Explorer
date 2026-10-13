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

namespace world_explorer {
result<decoded_video> adopt_video_decode(const retained_video_decode& selected,
                                         const std::string& source_sha256,
                                         const std::stop_token& stop) {
    try {
        if (!job_detail::valid_digest(selected.completion_sha256) ||
            !job_detail::valid_digest(source_sha256))
            return fail(error_code::INVALID_ARGUMENT, "Invalid selected decode/source digest");
        const auto stage = std::filesystem::canonical(selected.stage);
        const auto name = stage.filename().string();
        if (std::filesystem::is_symlink(selected.stage) ||
            stage.parent_path().filename() != "stages" || !name.starts_with("decode-") ||
            !job_detail::valid_digest(name.substr(7)))
            return fail(error_code::INVALID_DATA, "Select a committed decode stage explicitly");
        const auto verified = verify_committed_stage(stage, name.substr(7), stop);
        if (!verified)
            return std::unexpected(verified.error());
        if (verified->completion_sha256 != selected.completion_sha256 ||
            !verified->assets.contains("result.json"))
            return fail(error_code::INVALID_DATA, "Decode completion differs from selection");
        const auto catalog = inspect_decoded_frame_source(
            {.stage_directory = stage, .source_sha256 = source_sha256}, stop);
        if (!catalog)
            return std::unexpected(catalog.error());
        if (catalog->completion_sha256 != selected.completion_sha256)
            return fail(error_code::INVALID_DATA, "Decode changed during catalog admission");
        return decoded_video{{stage, selected.completion_sha256}, *catalog};
    } catch (const std::exception& exception) {
        return fail(error_code::INVALID_DATA,
                    "Decode admission failed: " + std::string(exception.what()));
    }
}
result<decoded_video> decode_video(const video_decode_request& request, const std::stop_token& stop,
                                   const log_callback& log, const worker_event_callback& observed) {
    try {
        const auto& config = request.configuration;
        if (stop.stop_requested())
            return fail(error_code::CANCELLED, "Video decode cancelled");
        if (config.job_directory.empty() || config.worker.script.empty() ||
            config.worker.python.empty() || config.worker.python.size() > 4096 ||
            config.worker.python.find('\0') != std::string::npos ||
            !config.worker.framed_protocol || !std::isfinite(config.sample_fps) ||
            config.sample_fps < .1 || config.sample_fps > 4 || config.max_frames < 3 ||
            config.max_frames > 120 || !config.cpu_threads || config.cpu_threads > 4 ||
            !config.max_output_bytes || config.max_output_bytes > 32ULL * 1024 * 1024 * 1024)
            return fail(error_code::INVALID_ARGUMENT,
                        "Invalid explicit video decode configuration");
        const auto preflight =
            preflight_video({request.video, request.inspection, request.outputs}, stop);
        if (!preflight)
            return std::unexpected(preflight.error());
        if (std::filesystem::is_symlink(config.job_directory))
            return fail(error_code::INVALID_DATA, "Decode job may not be a symlink");
        const auto directory = std::filesystem::weakly_canonical(config.job_directory);
        const auto video = std::filesystem::canonical(request.video);
        const auto script = std::filesystem::canonical(config.worker.script);
        const auto inspection_job =
            std::filesystem::canonical(request.inspection.stage).parent_path().parent_path();
        auto protected_paths = request.outputs;
        protected_paths.insert(protected_paths.end(), {video, script, inspection_job});
        if (std::filesystem::path(config.worker.python).has_parent_path())
            protected_paths.emplace_back(std::filesystem::weakly_canonical(config.worker.python));
        for (const auto& path : protected_paths)
            if (video_detail::overlaps(directory, std::filesystem::weakly_canonical(path)))
                return fail(error_code::INVALID_ARGUMENT,
                            "Decode job overlaps another input/output");
        auto storage_paths = request.outputs;
        storage_paths.push_back(directory);
        const auto storage =
            video_detail::check_storage(storage_paths, request.inspection.minimum_free_bytes, stop);
        if (!storage)
            return std::unexpected(storage.error());
        const auto script_hash = sha256_file(script, std::size_t{16} * 1024 * 1024, stop);
        if (!script_hash)
            return std::unexpected(script_hash.error());
        const nlohmann::json parameters{{"sample_fps", config.sample_fps},
                                        {"max_frames", config.max_frames},
                                        {"cpu_threads", config.cpu_threads},
                                        {"max_output_bytes", config.max_output_bytes}};
        const nlohmann::json owner{{"schema_version", 1},
                                   {"role", "video_decode"},
                                   {"protocol_major", 2},
                                   {"source_sha256", preflight->source_sha256},
                                   {"inspection_sha256", request.inspection.completion_sha256},
                                   {"worker_sha256", *script_hash},
                                   {"worker_script", script.string()},
                                   {"python", config.worker.python},
                                   {"parameters", parameters}};
        auto lock = job_detail::open_owned_job(
            directory, {".we-decode-owner.json", ".we-decode.lock"}, owner,
            {"decode-request.json", "request.json", "capabilities.json", "result.json",
             "inspect.json", "decode.json", "stages", "job.lock"},
            stop);
        if (!lock)
            return std::unexpected(lock.error());
        if (std::filesystem::exists(std::filesystem::symlink_status(directory / "job.lock")))
            return fail(error_code::WOULD_BLOCK,
                        "Decode worker lock exists; verify its process is gone before recovery");
        const nlohmann::json worker_request{{"schema_version", 1},
                                            {"source", video.string()},
                                            {"job_dir", directory.string()},
                                            {"stop_after", "decode"},
                                            {"sample_fps", config.sample_fps},
                                            {"limits",
                                             {{"max_frames", config.max_frames},
                                              {"cpu_threads", config.cpu_threads},
                                              {"max_output_bytes", config.max_output_bytes}}}};
        const auto published =
            job_detail::publish_owned_json(directory / "decode-request.json", worker_request, stop);
        if (!published)
            return std::unexpected(published.error());
        if (log)
            log("Decoding selected video frames locally (timestamp sampling; diagnostics "
                "unqualified).\n");
        std::stop_source cancellation;
        std::stop_callback forward(stop, [&] { cancellation.request_stop(); });
        std::optional<error> failure;
        std::optional<worker_completed> completion;
        const auto executed = execute_worker_protocol(
            config.worker.python, script,
            {"run", "--request", (directory / "decode-request.json").string()},
            cancellation.get_token(), log, [&](const worker_event& event) {
                if (const auto* capability = std::get_if<worker_handshake>(&event))
                    if (!capability->batch ||
                        std::ranges::find(capability->sensors, std::string{"video"}) ==
                            capability->sensors.end() ||
                        capability->max_frames < config.max_frames ||
                        capability->max_source_bytes < preflight->source_bytes ||
                        capability->max_output_bytes < config.max_output_bytes) {
                        failure = error{error_code::UNSUPPORTED_OPERATION,
                                        "Worker lacks video decode capacity"};
                        cancellation.request_stop();
                    }
                if (const auto* value = std::get_if<worker_completed>(&event))
                    completion = *value;
                if (observed)
                    observed(event);
            });
        if (stop.stop_requested())
            return fail(error_code::CANCELLED, "Decode cancelled; committed stages retained");
        if (failure)
            return std::unexpected(*failure);
        if (!executed)
            return std::unexpected(executed.error());
        if (*executed != 0)
            return fail(error_code::BACKEND_FAILURE,
                        "Decode worker failed; previous scene retained");
        if (!completion || completion->stage != "decode" || !completion->artifact)
            return fail(error_code::INVALID_DATA, "Worker did not return a decode artifact");
        const auto& artifact = *completion->artifact;
        const auto stage = std::filesystem::canonical(artifact.path).parent_path();
        if (artifact.path != stage / "result.json" || stage.parent_path() != directory / "stages")
            return fail(error_code::INVALID_DATA, "Decode artifact escapes its owned job");
        const auto digest =
            sha256_file(stage / "completion.json", std::size_t{16} * 1024 * 1024, stop);
        if (!digest)
            return std::unexpected(digest.error());
        const auto checked = adopt_video_decode({stage, *digest}, preflight->source_sha256, stop);
        if (!checked)
            return std::unexpected(checked.error());
        const auto divisor =
            std::gcd(preflight->time_base_numerator, preflight->time_base_denominator);
        const bool rotated = preflight->rotation_degrees % 180 != 0;
        if (checked->catalog.observations.size() > config.max_frames)
            return fail(error_code::RESOURCE_EXHAUSTED, "Decode exceeds selected frame budget");
        for (const auto& frame : checked->catalog.observations)
            if (frame.width != (rotated ? preflight->height : preflight->width) ||
                frame.height != (rotated ? preflight->width : preflight->height) ||
                frame.original_timestamp.time_base_numerator !=
                    preflight->time_base_numerator / divisor ||
                frame.original_timestamp.time_base_denominator !=
                    preflight->time_base_denominator / divisor)
                return fail(error_code::INVALID_DATA,
                            "Decoded dimensions/time base differ from inspected video");
        const auto final_source = sha256_file(video, 16ULL * 1024 * 1024 * 1024, stop);
        const auto final_script = sha256_file(script, std::size_t{16} * 1024 * 1024, stop);
        const auto final_result = sha256_file(artifact.path, WORKER_MESSAGE_LIMIT, stop);
        if (!final_source)
            return std::unexpected(final_source.error());
        if (!final_script)
            return std::unexpected(final_script.error());
        if (!final_result)
            return std::unexpected(final_result.error());
        if (*final_source != preflight->source_sha256 || *final_script != *script_hash ||
            *final_result != artifact.sha256)
            return fail(error_code::INVALID_DATA,
                        "Decode source/worker/artifact changed during execution");
        return *checked;
    } catch (const std::exception& exception) {
        return fail(error_code::INVALID_DATA,
                    "Video decode failed: " + std::string(exception.what()));
    }
}
} // namespace world_explorer
