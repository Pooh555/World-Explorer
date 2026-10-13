#include "pose_job.hpp"
#include "job_validation.hpp"
#include "owned_job.hpp"
#include "pose_pipeline.hpp"
#include "video_preflight_detail.hpp"
#include "world_explorer/core/hash.hpp"
#include <numeric>
#include <set>

namespace world_explorer::job_detail {
result<std::unique_ptr<file_lock>>
prepare_pose_job(const reconstruction_job& job, const decoded_video& decoded,
                 const video_preflight_report& preflight, const std::stop_token& stop,
                 const log_callback& log, const worker_event_callback& observed) {
    try {
        if (!job.fresh_poses)
            return fail(error_code::INVALID_ARGUMENT, "Missing explicit fresh pose configuration");
        const auto& config = *job.fresh_poses;
        const auto directory = std::filesystem::weakly_canonical(job.pose_job);
        auto plan = directory;
        plan += ".plan";
        const auto script = std::filesystem::canonical(config.worker.script);
        const auto script_hash = sha256_file(script, std::size_t{16} * 1024 * 1024, stop);
        if (!script_hash)
            return std::unexpected(script_hash.error());
        const auto divisor =
            std::gcd(preflight.time_base_numerator, preflight.time_base_denominator);
        const bool rotated = preflight.rotation_degrees % 180 != 0;
        for (const auto& frame : decoded.catalog.observations)
            if (frame.width != (rotated ? preflight.height : preflight.width) ||
                frame.height != (rotated ? preflight.width : preflight.height) ||
                frame.original_timestamp.time_base_numerator !=
                    preflight.time_base_numerator / divisor ||
                frame.original_timestamp.time_base_denominator !=
                    preflight.time_base_denominator / divisor)
                return fail(error_code::INVALID_DATA,
                            "Pose input differs from inspected image domain/time base");
        const auto recipe =
            config.recipe == pose_recipe::SEQUENTIAL ? "sequential" : "exhaustive_guided";
        const nlohmann::json owner{{"schema_version", 1},
                                   {"role", "native_pose_job"},
                                   {"source_sha256", preflight.source_sha256},
                                   {"decode_sha256", decoded.selected.completion_sha256},
                                   {"pose_job", directory.string()},
                                   {"worker_script", script.string()},
                                   {"worker_sha256", *script_hash},
                                   {"worker_python", config.worker.python},
                                   {"pose_python", config.pose_python},
                                   {"recipe", recipe},
                                   {"cpu_threads", config.cpu_threads},
                                   {"journal_bytes", config.journal_bytes.bytes}};
        if (std::filesystem::exists(std::filesystem::symlink_status(directory)) &&
            !std::filesystem::exists(std::filesystem::symlink_status(plan / "owner.json")))
            return fail(error_code::INVALID_DATA, "Existing pose job has no matching owned intent; "
                                                  "select explicit adoption or fresh output");
        auto lock = open_owned_job(plan, {"owner.json", "writer.lock"}, owner, {}, stop);
        if (!lock)
            return std::unexpected(lock.error());
        const auto check_journal = [&]() -> result<void> {
            const auto journal =
                read_json(directory / "input/journal.json", std::size_t{1024} * 1024);
            if (!journal)
                return std::unexpected(journal.error());
            if (journal->at("source_sha256") != preflight.source_sha256 ||
                journal->at("decode_completion_sha256") != decoded.selected.completion_sha256 ||
                journal->at("schema_version") != 1 ||
                journal->at("profile") != "native_rgb_journal_v1" ||
                !journal->at("frames").is_array() ||
                journal->at("frames").size() != decoded.catalog.observations.size())
                return fail(error_code::INVALID_DATA,
                            "Pose journal differs from selected decode dependency");
            if (std::filesystem::is_symlink(directory / "input") ||
                std::filesystem::is_symlink(directory / "input/journal.json"))
                return fail(error_code::INVALID_DATA, "Pose input may not contain aliases");
            std::uint64_t total{};
            std::set<std::string> files{"journal.json"};
            for (std::size_t index = 0; index < decoded.catalog.observations.size(); ++index) {
                const auto& expected = decoded.catalog.observations[index];
                const auto& frame = journal->at("frames").at(index);
                const auto name = std::to_string(index + 1) + ".ppm";
                files.insert(name);
                const auto prefix = "P6\n" + std::to_string(expected.width) + " " +
                                    std::to_string(expected.height) + "\n255\n";
                const auto bytes =
                    std::uint64_t{expected.width} * expected.height * 3 + prefix.size();
                if (frame.at("file") != name ||
                    frame.at("frame_id") != expected.external_frame_id ||
                    frame.at("calibration_id") != expected.external_calibration_id ||
                    frame.at("width") != expected.width || frame.at("height") != expected.height ||
                    frame.at("pts") != expected.original_timestamp.pts ||
                    frame.at("bytes") != bytes || bytes > config.journal_bytes.bytes - total ||
                    std::filesystem::is_symlink(directory / "input" / name) ||
                    std::filesystem::file_size(directory / "input" / name) != bytes)
                    return fail(error_code::INVALID_DATA,
                                "Pose RGB input differs from selected frame dependency");
                total += bytes;
                const auto digest =
                    sha256_file(directory / "input" / name, static_cast<std::size_t>(bytes), stop);
                if (!digest)
                    return std::unexpected(digest.error());
                if (frame.at("sha256") != *digest)
                    return fail(error_code::INVALID_DATA, "Pose RGB input checksum changed");
            }
            std::size_t entries{};
            for (const auto& entry : std::filesystem::directory_iterator(directory / "input"))
                if (++entries > files.size() || entry.is_symlink() || !entry.is_regular_file() ||
                    !files.contains(entry.path().filename().string()))
                    return fail(error_code::INVALID_DATA, "Uncommitted asset in pose input");
            const auto dataset = load_completed_poses({directory, preflight.source_sha256}, stop);
            if (!dataset)
                return std::unexpected(dataset.error());
            const auto digest =
                sha256_file(directory / "input/journal.json", std::size_t{1024} * 1024, stop);
            if (!digest)
                return std::unexpected(digest.error());
            if (!(*dataset)->journal_sha256 || *(*dataset)->journal_sha256 != *digest)
                return fail(error_code::INVALID_DATA,
                            "Completed native poses lack their exact input journal dependency");
            return {};
        };
        if (std::filesystem::exists(directory / "result.json")) {
            const auto complete = check_journal();
            if (!complete)
                return std::unexpected(complete.error());
            if (log)
                log("Reusing completed poses from the identical owned intent; solver skipped.\n");
            return std::move(*lock);
        }
        batch_pose_config batch{.decode_stage = decoded.selected.stage,
                                .job_directory = directory,
                                .worker_script = script,
                                .source_sha256 = preflight.source_sha256,
                                .worker_python = config.worker.python,
                                .pose_python = config.pose_python,
                                .recipe = config.recipe,
                                .journal_bytes = config.journal_bytes,
                                .cpu_threads = config.cpu_threads,
                                .log = log,
                                .resume_input = std::filesystem::exists(directory),
                                .observed = observed};
        auto estimator = make_batch_pose_estimator(std::move(batch), stop);
        if (!estimator)
            return std::unexpected(estimator.error());
        auto source = open_decoded_frame_source(
            {.stage_directory = decoded.selected.stage, .source_sha256 = preflight.source_sha256},
            stop);
        if (!source)
            return std::unexpected(source.error());
        const auto started = (*estimator)
                                 ->start({{std::size_t{64} * 1024 * 1024},
                                          (*estimator)->capabilities().observation_capacity});
        if (!started)
            return std::unexpected(started.error());
        if (log)
            log("Solving fresh selected-frame poses through native RGB admission and an isolated "
                "batch worker.\n");
        const auto events = pose_pipeline::consume(**source, **estimator, stop);
        if (!events)
            return std::unexpected(events.error());
        const auto complete = check_journal();
        if (!complete)
            return std::unexpected(complete.error());
        const auto final_script = sha256_file(script, std::size_t{16} * 1024 * 1024, stop);
        if (!final_script)
            return std::unexpected(final_script.error());
        if (*final_script != *script_hash)
            return fail(error_code::INVALID_DATA, "Pose worker changed during execution");
        return std::move(*lock);
    } catch (const std::exception& exception) {
        return fail(error_code::INVALID_DATA,
                    "Pose job preparation failed: " + std::string(exception.what()));
    }
}
} // namespace world_explorer::job_detail
