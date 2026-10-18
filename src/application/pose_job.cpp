#include "pose_job.hpp"

#include "job_validation.hpp"
#include "owned_job.hpp"
#include "pose_pipeline.hpp"
#include "video_preflight_detail.hpp"
#include "world_explorer/core/hash.hpp"

#include <numeric>
#include <set>

namespace world_explorer::job_detail {

Result<std::unique_ptr<FileLock>> prepare_pose_job(const ReconstructionJob& job,
                                                   const DecodedVideo& decoded,
                                                   const VideoPreflightReport& preflight,
                                                   const std::stop_token& stop,
                                                   const LogCallback& log,
                                                   const WorkerEventCallback& observed) {
    try {
        if (!job.m_fresh_poses)
            return fail(ErrorCode::INVALID_ARGUMENT, "Missing explicit fresh pose configuration");
        const auto& config = *job.m_fresh_poses;
        const auto K_DIRECTORY = std::filesystem::weakly_canonical(job.m_pose_job);
        auto plan = K_DIRECTORY;
        plan += ".plan";
        const auto K_SCRIPT = std::filesystem::canonical(config.m_worker.m_script);
        const auto K_SCRIPT_HASH = sha256_file(K_SCRIPT, 16uz * 1024 * 1024, stop);
        if (!K_SCRIPT_HASH) return std::unexpected(K_SCRIPT_HASH.error());
        const auto K_DIVISOR =
            std::gcd(preflight.m_time_base_numerator, preflight.m_time_base_denominator);
        const bool K_ROTATED = preflight.m_rotation_degrees % 180 != 0;
        for (const auto& frame : decoded.m_catalog.m_observations)
            if (frame.m_width != (K_ROTATED ? preflight.m_height : preflight.m_width) ||
                frame.m_height != (K_ROTATED ? preflight.m_width : preflight.m_height) ||
                frame.m_original_timestamp.m_time_base_numerator !=
                    preflight.m_time_base_numerator / K_DIVISOR ||
                frame.m_original_timestamp.m_time_base_denominator !=
                    preflight.m_time_base_denominator / K_DIVISOR)
                return fail(ErrorCode::INVALID_DATA,
                            "Pose input differs from inspected image domain/time base");
        const auto K_RECIPE =
            config.m_recipe == PoseRecipe::SEQUENTIAL ? "sequential" : "exhaustive_guided";
        const nlohmann::json K_OWNER {{"schema_version", 1},
                                      {"role", "native_pose_job"},
                                      {"source_sha256", preflight.m_source_sha256},
                                      {"decode_sha256", decoded.m_selected.m_completion_sha256},
                                      {"pose_job", K_DIRECTORY.string()},
                                      {"worker_script", K_SCRIPT.string()},
                                      {"worker_sha256", *K_SCRIPT_HASH},
                                      {"worker_python", config.m_worker.m_python},
                                      {"pose_python", config.m_pose_python},
                                      {"recipe", K_RECIPE},
                                      {"cpu_threads", config.m_cpu_threads},
                                      {"journal_bytes", config.m_journal_bytes.m_bytes}};
        if (std::filesystem::exists(std::filesystem::symlink_status(K_DIRECTORY)) &&
            !std::filesystem::exists(std::filesystem::symlink_status(plan / "owner.json")))
            return fail(ErrorCode::INVALID_DATA,
                        "Existing pose job has no matching owned intent; "
                        "select explicit adoption or fresh output");
        auto lock = open_owned_job(plan, {"owner.json", "writer.lock"}, K_OWNER, {}, stop);
        if (!lock) return std::unexpected(lock.error());
        const auto K_CHECK_JOURNAL = [&]() -> Result<void> {
            const auto K_JOURNAL = read_json(K_DIRECTORY / "input/journal.json", 1024uz * 1024);
            if (!K_JOURNAL) return std::unexpected(K_JOURNAL.error());
            if (K_JOURNAL->at("source_sha256") != preflight.m_source_sha256 ||
                K_JOURNAL->at("decode_completion_sha256") !=
                    decoded.m_selected.m_completion_sha256 ||
                K_JOURNAL->at("schema_version") != 1 ||
                K_JOURNAL->at("profile") != "native_rgb_journal_v1" ||
                !K_JOURNAL->at("frames").is_array() ||
                K_JOURNAL->at("frames").size() != decoded.m_catalog.m_observations.size())
                return fail(ErrorCode::INVALID_DATA,
                            "Pose journal differs from selected decode dependency");
            if (std::filesystem::is_symlink(K_DIRECTORY / "input") ||
                std::filesystem::is_symlink(K_DIRECTORY / "input/journal.json"))
                return fail(ErrorCode::INVALID_DATA, "Pose input may not contain aliases");
            std::uint64_t total {};
            std::set<std::string> files { "journal.json" };
            for (std::size_t index { 0uz }; index < decoded.m_catalog.m_observations.size();
                 ++index) {
                const auto& expected = decoded.m_catalog.m_observations[index];
                const auto& frame = K_JOURNAL->at("frames").at(index);
                const auto K_NAME = std::to_string(index + 1) + ".ppm";
                files.insert(K_NAME);
                const auto K_PREFIX = "P6\n" + std::to_string(expected.m_width) + " " +
                                      std::to_string(expected.m_height) + "\n255\n";
                const auto K_BYTES =
                    std::uint64_t { expected.m_width } * expected.m_height * 3 + K_PREFIX.size();
                if (frame.at("file") != K_NAME ||
                    frame.at("frame_id") != expected.m_external_frame_id ||
                    frame.at("calibration_id") != expected.m_external_calibration_id ||
                    frame.at("width") != expected.m_width ||
                    frame.at("height") != expected.m_height ||
                    frame.at("pts") != expected.m_original_timestamp.m_pts ||
                    frame.at("bytes") != K_BYTES ||
                    K_BYTES > config.m_journal_bytes.m_bytes - total ||
                    std::filesystem::is_symlink(K_DIRECTORY / "input" / K_NAME) ||
                    std::filesystem::file_size(K_DIRECTORY / "input" / K_NAME) != K_BYTES)
                    return fail(ErrorCode::INVALID_DATA,
                                "Pose RGB input differs from selected frame dependency");
                total += K_BYTES;
                const auto K_DIGEST = sha256_file(
                    K_DIRECTORY / "input" / K_NAME, static_cast<std::size_t>(K_BYTES), stop);
                if (!K_DIGEST) return std::unexpected(K_DIGEST.error());
                if (frame.at("sha256") != *K_DIGEST)
                    return fail(ErrorCode::INVALID_DATA, "Pose RGB input checksum changed");
            }
            std::size_t entries {};
            for (const auto& entry : std::filesystem::directory_iterator(K_DIRECTORY / "input"))
                if (++entries > files.size() || entry.is_symlink() || !entry.is_regular_file() ||
                    !files.contains(entry.path().filename().string()))
                    return fail(ErrorCode::INVALID_DATA, "Uncommitted asset in pose input");
            const auto K_DATASET =
                load_completed_poses({K_DIRECTORY, preflight.m_source_sha256}, stop);
            if (!K_DATASET) return std::unexpected(K_DATASET.error());
            const auto K_DIGEST =
                sha256_file(K_DIRECTORY / "input/journal.json", 1024uz * 1024, stop);
            if (!K_DIGEST) return std::unexpected(K_DIGEST.error());
            if (!(*K_DATASET)->m_journal_sha256 || *(*K_DATASET)->m_journal_sha256 != *K_DIGEST)
                return fail(ErrorCode::INVALID_DATA,
                            "Completed native poses lack their exact input journal dependency");
            return {};
        };
        if (std::filesystem::exists(K_DIRECTORY / "result.json")) {
            const auto K_COMPLETE = K_CHECK_JOURNAL();
            if (!K_COMPLETE) return std::unexpected(K_COMPLETE.error());
            if (log)
                log("Reusing completed poses from the identical owned intent; solver skipped.\n");
            return std::move(*lock);
        }
        BatchPoseConfig batch {.m_decode_stage = decoded.m_selected.m_stage,
                               .m_job_directory = K_DIRECTORY,
                               .m_worker_script = K_SCRIPT,
                               .m_source_sha256 = preflight.m_source_sha256,
                               .m_worker_python = config.m_worker.m_python,
                               .m_pose_python = config.m_pose_python,
                               .m_recipe = config.m_recipe,
                               .m_journal_bytes = config.m_journal_bytes,
                               .m_cpu_threads = config.m_cpu_threads,
                               .m_log = log,
                               .m_resume_input = std::filesystem::exists(K_DIRECTORY),
                               .m_observed = observed};
        auto estimator = make_batch_pose_estimator(std::move(batch), stop);
        if (!estimator) return std::unexpected(estimator.error());
        auto source = open_decoded_frame_source({.m_stage_directory = decoded.m_selected.m_stage,
                                                 .m_source_sha256 = preflight.m_source_sha256},
                                                stop);
        if (!source) return std::unexpected(source.error());
        const auto K_STARTED = (*estimator)
                                   ->start({{64uz * 1024 * 1024},
                                            (*estimator)->capabilities().m_observation_capacity});
        if (!K_STARTED) return std::unexpected(K_STARTED.error());
        if (log)
            log("Solving fresh selected-frame poses through native RGB admission and an isolated "
                "batch worker.\n");
        const auto K_EVENTS = pose_pipeline::consume(**source, **estimator, stop);
        if (!K_EVENTS) return std::unexpected(K_EVENTS.error());
        const auto K_COMPLETE = K_CHECK_JOURNAL();
        if (!K_COMPLETE) return std::unexpected(K_COMPLETE.error());
        const auto K_FINAL_SCRIPT = sha256_file(K_SCRIPT, 16uz * 1024 * 1024, stop);
        if (!K_FINAL_SCRIPT) return std::unexpected(K_FINAL_SCRIPT.error());
        if (*K_FINAL_SCRIPT != *K_SCRIPT_HASH)
            return fail(ErrorCode::INVALID_DATA, "Pose worker changed during execution");
        return std::move(*lock);
    } catch (const std::exception& exception) {
        return fail(ErrorCode::INVALID_DATA,
                    "Pose job preparation failed: " + std::string(exception.what()));
    }
}

}  // namespace world_explorer::job_detail
