#include "request_summary.hpp"

#include "world_explorer/application/reconstruction_job.hpp"
#include "world_explorer/core/hash.hpp"

#include <format>
#include <stdexcept>

namespace world_explorer::viewer {
namespace {

std::string display_path(const std::filesystem::path& path) {
    const auto K_UTF8 = path.generic_u8string();
    return { K_UTF8.begin(), K_UTF8.end() };
}

}  // namespace

Result<request_summary> review_reconstruction_request(const std::filesystem::path& path,
                                                      const std::stop_token& stop) {
    try {
        if (stop.stop_requested()) {
            return fail(ErrorCode::CANCELLED, "Request review cancelled");
        }
        const auto K_SELECTED = std::filesystem::canonical(path);
        constexpr std::size_t K_MAX_REQUEST_BYTES { 64uz * 1024uz };
        if (!std::filesystem::is_regular_file(K_SELECTED) ||
            std::filesystem::file_size(K_SELECTED) > K_MAX_REQUEST_BYTES) {
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "Request must be a file of at most 64 KiB");
        }
        const auto K_DIGEST = sha256_file(K_SELECTED, K_MAX_REQUEST_BYTES, stop);
        if (!K_DIGEST) {
            return std::unexpected(K_DIGEST.error());
        }
        const auto K_CHECKED = read_reconstruction_job(K_SELECTED, *K_DIGEST);
        if (!K_CHECKED) {
            return std::unexpected(K_CHECKED.error());
        }
        if (stop.stop_requested()) {
            return fail(ErrorCode::CANCELLED, "Request review cancelled");
        }
        const auto& job = *K_CHECKED;
        request_summary summary { K_SELECTED, *K_DIGEST, {}, {} };
        std::size_t text_bytes {};
        const auto K_ADD = [&](std::string label, std::string value, bool advanced = false) {
            constexpr std::size_t K_MAX_TEXT { 128uz * 1024uz };
            const auto K_BYTES = label.size() + value.size();
            if (summary.rows.size() + summary.advanced.size() >= 32 ||
                K_BYTES > K_MAX_TEXT - text_bytes) {
                throw std::length_error("Request summary exceeds presentation budget");
            }
            text_bytes += K_BYTES;
            (advanced ? summary.advanced : summary.rows)
                .push_back({ std::move(label), std::move(value) });
        };
        K_ADD("Recipe", "Local gsplat development recipe; quality unqualified");
        K_ADD("Video", display_path(job.m_video));
        K_ADD("Scene output", display_path(job.m_scene_output));
        K_ADD("Pose action",
              job.m_fresh_poses ? "Solve selected frames (reuse identical completed solve)"
                                : "Adopt completed poses");
        K_ADD("Appearance action",
              job.m_action == FitAction::START    ? "Start a new fit"
              : job.m_action == FitAction::RESUME ? "Resume optimizer checkpoint"
                                                  : "Reuse completed fit; optimization skipped");
        K_ADD("Inspection",
              job.m_automatic_inspection
                  ? "Inspect video locally (metadata pending; compatible cache allowed)"
              : job.m_inspection ? "Adopt selected completed inspection"
                                 : "Not selected (legacy request)");
        std::optional<std::uint64_t> minimum_free_bytes;
        if (job.m_automatic_inspection) {
            minimum_free_bytes = job.m_automatic_inspection->m_minimum_free_bytes;
        } else if (job.m_inspection) {
            minimum_free_bytes = job.m_inspection->m_minimum_free_bytes;
        }
        if (minimum_free_bytes) {
            K_ADD("Minimum free space",
                  std::format("{} bytes at each output; rechecked when running",
                              *minimum_free_bytes));
            auto map_directory = job.m_map_directory;
            if (map_directory.empty()) {
                map_directory = job.m_fit_directory;
                map_directory += ".map";
            }
            const std::vector<std::filesystem::path> K_OUTPUTS {
                map_directory, job.m_fit_directory, job.m_scene_output };
            std::vector<OutputStorageReport> storage;
            if (job.m_inspection) {
                const auto K_PREFLIGHT =
                    preflight_video({ job.m_video, *job.m_inspection, K_OUTPUTS }, stop);
                if (!K_PREFLIGHT) {
                    return std::unexpected(K_PREFLIGHT.error());
                }
                K_ADD("Checked video",
                      std::format("{}; {} x {}; {:.3f} s; {} source bytes",
                                  K_PREFLIGHT->m_codec_name,
                                  K_PREFLIGHT->m_width,
                                  K_PREFLIGHT->m_height,
                                  K_PREFLIGHT->m_duration_seconds,
                                  K_PREFLIGHT->m_source_bytes));
                K_ADD("Source timing",
                      std::format("Original time base {}/{}; display rotation {} degrees",
                                  K_PREFLIGHT->m_time_base_numerator,
                                  K_PREFLIGHT->m_time_base_denominator,
                                  K_PREFLIGHT->m_rotation_degrees));
                K_ADD("Appearance and scale",
                      "sRGB captured radiance; arbitrary scale; gravity unknown");
                storage = K_PREFLIGHT->m_storage;
            } else {
                const auto K_CHECKED_STORAGE =
                    inspect_output_storage(K_OUTPUTS, *minimum_free_bytes, stop);
                if (!K_CHECKED_STORAGE) {
                    return std::unexpected(K_CHECKED_STORAGE.error());
                }
                storage = *K_CHECKED_STORAGE;
            }
            // These jobs can live on different volumes from the final outputs.
            std::vector<std::filesystem::path> stage_outputs;
            if (job.m_automatic_inspection) {
                stage_outputs.push_back(job.m_automatic_inspection->m_job_directory);
            }
            if (job.m_automatic_decode) {
                stage_outputs.push_back(job.m_automatic_decode->m_job_directory);
            }
            if (job.m_fresh_poses) {
                stage_outputs.push_back(job.m_pose_job);
            }
            if (!stage_outputs.empty()) {
                const auto K_CHECKED_STORAGE =
                    inspect_output_storage(stage_outputs, *minimum_free_bytes, stop);
                if (!K_CHECKED_STORAGE) {
                    return std::unexpected(K_CHECKED_STORAGE.error());
                }
                storage.insert(storage.end(), K_CHECKED_STORAGE->begin(), K_CHECKED_STORAGE->end());
            }
            std::string display =
                "Available at review; no reservation or whole-job peak estimate.\n";
            for (const auto& output : storage) {
                display += std::format("{}: {} bytes available\n",
                                       display_path(output.m_output),
                                       output.m_available_bytes);
            }
            K_ADD("Current output storage", std::move(display));
        }
        if (job.m_automatic_decode) {
            const auto& config = *job.m_automatic_decode;
            K_ADD(
                "Selected frames",
                config.m_frame_selection != FrameSelection::TIMESTAMP_SAMPLING_V1
                    ? std::format("Candidate quality selection v{} (unqualified): {} candidates/s, "
                                  "at most {} keyframes",
                                  config.m_frame_selection ==
                                          FrameSelection::QUALITY_KEYFRAMES_CANDIDATE_V2
                                      ? 2
                                      : 1,
                                  config.m_sample_fps,
                                  config.m_max_frames)
                    : std::format("Timestamp sampling: {} frames/s, at most {} frames",
                                  config.m_sample_fps,
                                  config.m_max_frames));
            K_ADD("Decoded output cap", std::format("{} bytes", config.m_max_output_bytes));
            K_ADD("Decode CPU threads", std::to_string(config.m_cpu_threads));
        } else if (job.m_fresh_poses) {
            K_ADD("Selected frames", "Adopt selected completed decode; no new frame selection");
        }
        if (job.m_fresh_poses) {
            const auto& config = *job.m_fresh_poses;
            K_ADD("Pose matching",
                  config.m_recipe == PoseRecipe::SEQUENTIAL ? "Sequential" : "Exhaustive guided");
            K_ADD("Pose CPU threads", std::to_string(config.m_cpu_threads));
            K_ADD("RGB journal cap", std::format("{} bytes", config.m_journal_bytes.m_bytes));
        }
        const auto K_COMPATIBILITY =
            job.m_action == FitAction::REUSE_COMPLETED ? " (fit compatibility)" : "";
        K_ADD("Fit steps", std::to_string(job.m_steps) + K_COMPATIBILITY);
        K_ADD("Training image cap",
              std::format(
                  "{} pixels on the longest side{}", job.m_max_image_dimension, K_COMPATIBILITY));
        K_ADD("Transient splat cap",
              std::format("{} splats{}; not final scene size", job.m_max_splats, K_COMPATIBILITY));
        if (job.m_action == FitAction::RESUME) {
            K_ADD("Checkpoint", display_path(job.m_checkpoint));
        }
        K_ADD("Recipe identifier", "local_gsplat_v1", true);
        K_ADD("Pose job", display_path(job.m_pose_job), true);
        K_ADD("Appearance job", display_path(job.m_fit_directory), true);
        if (job.m_action == FitAction::REUSE_COMPLETED) {
            K_ADD("Completed fit identity", job.m_completed_fit_identity, true);
        } else {
            K_ADD("Fit Python", job.m_fitting_worker.m_python, true);
            K_ADD("Fit worker", display_path(job.m_fitting_worker.m_script), true);
            K_ADD("Local backend source", display_path(job.m_gsplat_source), true);
        }
        if (job.m_automatic_inspection) {
            K_ADD("Inspection Python", job.m_automatic_inspection->m_worker.m_python, true);
            K_ADD("Inspection worker",
                  display_path(job.m_automatic_inspection->m_worker.m_script),
                  true);
        }
        if (job.m_automatic_decode) {
            K_ADD("Decode Python", job.m_automatic_decode->m_worker.m_python, true);
            K_ADD("Decode worker", display_path(job.m_automatic_decode->m_worker.m_script), true);
        }
        if (job.m_fresh_poses) {
            K_ADD("Pose worker Python", job.m_fresh_poses->m_worker.m_python, true);
            K_ADD("Pose Python", job.m_fresh_poses->m_pose_python, true);
            K_ADD("Pose worker", display_path(job.m_fresh_poses->m_worker.m_script), true);
        }
        if (stop.stop_requested()) {
            return fail(ErrorCode::CANCELLED, "Request review cancelled");
        }
        const auto K_FINAL_DIGEST = sha256_file(K_SELECTED, K_MAX_REQUEST_BYTES, stop);
        if (!K_FINAL_DIGEST) {
            return std::unexpected(K_FINAL_DIGEST.error());
        }
        if (*K_FINAL_DIGEST != *K_DIGEST || std::filesystem::canonical(path) != K_SELECTED) {
            return fail(ErrorCode::INVALID_DATA, "Request changed during review; review again");
        }
        return summary;
    } catch (const std::length_error& error) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, error.what());
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, "Request review failed: " + std::string(error.what()));
    }
}

}  // namespace world_explorer::viewer
