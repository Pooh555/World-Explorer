#include "request_summary.hpp"
#include "world_explorer/application/reconstruction_job.hpp"
#include "world_explorer/core/hash.hpp"
#include <format>
#include <stdexcept>

namespace world_explorer::viewer {
namespace {
std::string display_path(const std::filesystem::path& path) {
    const auto utf8 = path.generic_u8string();
    return {utf8.begin(), utf8.end()};
}
} // namespace
result<request_summary> review_reconstruction_request(const std::filesystem::path& path,
                                                      const std::stop_token& stop) {
    try {
        if (stop.stop_requested())
            return fail(error_code::CANCELLED, "Request review cancelled");
        const auto selected = std::filesystem::canonical(path);
        constexpr std::size_t MAX_REQUEST_BYTES = std::size_t{64} * 1024;
        if (!std::filesystem::is_regular_file(selected) ||
            std::filesystem::file_size(selected) > MAX_REQUEST_BYTES)
            return fail(error_code::RESOURCE_EXHAUSTED, "Request must be a file of at most 64 KiB");
        const auto digest = sha256_file(selected, MAX_REQUEST_BYTES, stop);
        if (!digest)
            return std::unexpected(digest.error());
        const auto checked = read_reconstruction_job(selected, *digest);
        if (!checked)
            return std::unexpected(checked.error());
        if (stop.stop_requested())
            return fail(error_code::CANCELLED, "Request review cancelled");
        const auto& job = *checked;
        request_summary summary{selected, *digest, {}, {}};
        std::size_t text_bytes{};
        const auto add = [&](std::string label, std::string value, bool advanced = false) {
            constexpr std::size_t MAX_TEXT = std::size_t{128} * 1024;
            const auto bytes = label.size() + value.size();
            if (summary.rows.size() + summary.advanced.size() >= 32 ||
                bytes > MAX_TEXT - text_bytes)
                throw std::length_error("Request summary exceeds presentation budget");
            text_bytes += bytes;
            (advanced ? summary.advanced : summary.rows)
                .push_back({std::move(label), std::move(value)});
        };
        add("Recipe", "Local gsplat development recipe; quality unqualified");
        add("Video", display_path(job.video));
        add("Scene output", display_path(job.scene_output));
        add("Pose action", job.fresh_poses
                               ? "Solve selected frames (reuse identical completed solve)"
                               : "Adopt completed poses");
        add("Appearance action", job.action == fit_action::START ? "Start a new fit"
                                 : job.action == fit_action::RESUME
                                     ? "Resume optimizer checkpoint"
                                     : "Reuse completed fit; optimization skipped");
        add("Inspection", job.automatic_inspection
                              ? "Inspect video locally (metadata pending; compatible cache allowed)"
                          : job.inspection ? "Adopt selected completed inspection"
                                           : "Not selected (legacy request)");
        std::optional<std::uint64_t> minimum_free_bytes;
        if (job.automatic_inspection)
            minimum_free_bytes = job.automatic_inspection->minimum_free_bytes;
        else if (job.inspection)
            minimum_free_bytes = job.inspection->minimum_free_bytes;
        if (minimum_free_bytes) {
            add("Minimum free space", std::format("{} bytes at each output; rechecked when running",
                                                  *minimum_free_bytes));
            auto map_directory = job.map_directory;
            if (map_directory.empty()) {
                map_directory = job.fit_directory;
                map_directory += ".map";
            }
            const std::vector<std::filesystem::path> outputs{map_directory, job.fit_directory,
                                                             job.scene_output};
            std::vector<output_storage_report> storage;
            if (job.inspection) {
                const auto preflight = preflight_video({job.video, *job.inspection, outputs}, stop);
                if (!preflight)
                    return std::unexpected(preflight.error());
                add("Checked video",
                    std::format("{}; {} x {}; {:.3f} s; {} source bytes", preflight->codec_name,
                                preflight->width, preflight->height, preflight->duration_seconds,
                                preflight->source_bytes));
                add("Source timing",
                    std::format("Original time base {}/{}; display rotation {} degrees",
                                preflight->time_base_numerator, preflight->time_base_denominator,
                                preflight->rotation_degrees));
                add("Appearance and scale",
                    "sRGB captured radiance; arbitrary scale; gravity unknown");
                storage = preflight->storage;
            } else {
                const auto checked_storage =
                    inspect_output_storage(outputs, *minimum_free_bytes, stop);
                if (!checked_storage)
                    return std::unexpected(checked_storage.error());
                storage = *checked_storage;
            }
            // These jobs can live on different volumes from the final outputs.
            std::vector<std::filesystem::path> stage_outputs;
            if (job.automatic_inspection)
                stage_outputs.push_back(job.automatic_inspection->job_directory);
            if (job.automatic_decode)
                stage_outputs.push_back(job.automatic_decode->job_directory);
            if (job.fresh_poses)
                stage_outputs.push_back(job.pose_job);
            if (!stage_outputs.empty()) {
                const auto checked_storage =
                    inspect_output_storage(stage_outputs, *minimum_free_bytes, stop);
                if (!checked_storage)
                    return std::unexpected(checked_storage.error());
                storage.insert(storage.end(), checked_storage->begin(), checked_storage->end());
            }
            std::string display =
                "Available at review; no reservation or whole-job peak estimate.\n";
            for (const auto& output : storage)
                display += std::format("{}: {} bytes available\n", display_path(output.output),
                                       output.available_bytes);
            add("Current output storage", std::move(display));
        }
        if (job.automatic_decode) {
            const auto& config = *job.automatic_decode;
            add("Selected frames", std::format("Timestamp sampling: {} frames/s, at most {} frames",
                                               config.sample_fps, config.max_frames));
            add("Decoded output cap", std::format("{} bytes", config.max_output_bytes));
            add("Decode CPU threads", std::to_string(config.cpu_threads));
        } else if (job.fresh_poses)
            add("Selected frames", "Adopt selected completed decode; no new frame selection");
        if (job.fresh_poses) {
            const auto& config = *job.fresh_poses;
            add("Pose matching",
                config.recipe == pose_recipe::SEQUENTIAL ? "Sequential" : "Exhaustive guided");
            add("Pose CPU threads", std::to_string(config.cpu_threads));
            add("RGB journal cap", std::format("{} bytes", config.journal_bytes.bytes));
        }
        const auto compatibility =
            job.action == fit_action::REUSE_COMPLETED ? " (fit compatibility)" : "";
        add("Fit steps", std::to_string(job.steps) + compatibility);
        add("Training image cap",
            std::format("{} pixels on the longest side{}", job.max_image_dimension, compatibility));
        add("Transient splat cap",
            std::format("{} splats{}; not final scene size", job.max_splats, compatibility));
        if (job.action == fit_action::RESUME)
            add("Checkpoint", display_path(job.checkpoint));
        add("Recipe identifier", "local_gsplat_v1", true);
        add("Pose job", display_path(job.pose_job), true);
        add("Appearance job", display_path(job.fit_directory), true);
        if (job.action == fit_action::REUSE_COMPLETED)
            add("Completed fit identity", job.completed_fit_identity, true);
        else {
            add("Fit Python", job.fitting_worker.python, true);
            add("Fit worker", display_path(job.fitting_worker.script), true);
            add("Local backend source", display_path(job.gsplat_source), true);
        }
        if (job.automatic_inspection) {
            add("Inspection Python", job.automatic_inspection->worker.python, true);
            add("Inspection worker", display_path(job.automatic_inspection->worker.script), true);
        }
        if (job.automatic_decode) {
            add("Decode Python", job.automatic_decode->worker.python, true);
            add("Decode worker", display_path(job.automatic_decode->worker.script), true);
        }
        if (job.fresh_poses) {
            add("Pose worker Python", job.fresh_poses->worker.python, true);
            add("Pose Python", job.fresh_poses->pose_python, true);
            add("Pose worker", display_path(job.fresh_poses->worker.script), true);
        }
        if (stop.stop_requested())
            return fail(error_code::CANCELLED, "Request review cancelled");
        const auto final_digest = sha256_file(selected, MAX_REQUEST_BYTES, stop);
        if (!final_digest)
            return std::unexpected(final_digest.error());
        if (*final_digest != *digest || std::filesystem::canonical(path) != selected)
            return fail(error_code::INVALID_DATA, "Request changed during review; review again");
        return summary;
    } catch (const std::length_error& error) {
        return fail(error_code::RESOURCE_EXHAUSTED, error.what());
    } catch (const std::exception& error) {
        return fail(error_code::INVALID_DATA,
                    "Request review failed: " + std::string(error.what()));
    }
}
} // namespace world_explorer::viewer
