#ifndef WORLD_EXPLORER_APPLICATION_RECONSTRUCTION_JOB_HPP
#define WORLD_EXPLORER_APPLICATION_RECONSTRUCTION_JOB_HPP

#include "world_explorer/application/commands.hpp"
#include "world_explorer/application/video_decode.hpp"
#include "world_explorer/application/video_preflight.hpp"
#include "world_explorer/reconstruction_adapters/batch_poses.hpp"
#include <cstdint>
#include <optional>

namespace world_explorer {
enum class fit_action : std::uint8_t { START, RESUME, REUSE_COMPLETED };
struct fresh_pose_configuration {
    worker_configuration worker;
    std::string pose_python;
    pose_recipe recipe{pose_recipe::SEQUENTIAL};
    byte_budget journal_bytes{std::size_t{2} * 1024 * 1024 * 1024};
    std::uint32_t cpu_threads{4};
};

// This development command explicitly solves or continues a local pose job. It never silently
// changes recipes, restarts poses, downloads dependencies or adopts another fit.
struct reconstruction_job {
    std::filesystem::path video;
    std::filesystem::path pose_job;
    std::filesystem::path gsplat_source;
    std::filesystem::path fit_directory;
    std::filesystem::path scene_output;
    worker_configuration fitting_worker;
    std::uint32_t steps{7000};
    std::uint32_t max_image_dimension{1280};
    std::uint32_t max_splats{2000000};
    fit_action action{fit_action::START};
    std::filesystem::path checkpoint;
    std::string completed_fit_identity;
    // Empty map_directory selects the sibling FIT_DIRECTORY.map. Explicit
    // decode_stage selects a retained stage without rewriting a stale pointer.
    std::filesystem::path map_directory;
    std::filesystem::path decode_stage;
    // Schema 2 explicitly adopts checked metadata before retained pose preparation.
    // Absent in legacy schema 1; those requests do not claim video preflight.
    std::optional<retained_video_inspection> inspection;
    std::optional<automatic_video_inspection> automatic_inspection;
    // Filled by the reader so writable job outputs cannot contain the request.
    std::filesystem::path request_file;
    // Present only for an explicit schema-2 solve action. Decode selection is
    // separate from decode_stage, which selects the pose job's retained domain.
    std::optional<fresh_pose_configuration> fresh_poses;
    std::optional<automatic_video_decode> automatic_decode;
    std::optional<retained_video_decode> selected_decode;
};

// Strict, bounded schema; relative paths resolve against the request's directory.
result<reconstruction_job> read_reconstruction_job(const std::filesystem::path& request);
// One synchronous command, called off the UI thread. Logs run on the calling
// thread. Cancellation before manifest publication retains the previous scene;
// the final atomic publication is a commit boundary. Concurrent worker execution
// is excluded by the fit worker's lock. Completed reuse requires an explicit SHA.
result<std::filesystem::path> reconstruct(const reconstruction_job& job,
                                          const std::stop_token& stop, const log_callback& log);
} // namespace world_explorer

#endif // WORLD_EXPLORER_APPLICATION_RECONSTRUCTION_JOB_HPP
