#ifndef WORLD_EXPLORER_APPLICATION_RECONSTRUCTION_JOB_HPP
#define WORLD_EXPLORER_APPLICATION_RECONSTRUCTION_JOB_HPP

#include "world_explorer/application/commands.hpp"
#include "world_explorer/application/reconstruction_progress.hpp"
#include "world_explorer/application/video_decode.hpp"
#include "world_explorer/application/video_preflight.hpp"
#include "world_explorer/reconstruction_adapters/batch_poses.hpp"

#include <cstdint>
#include <optional>

namespace world_explorer {

enum class FitAction : std::uint8_t { START, RESUME, REUSE_COMPLETED };

struct FreshPoseConfiguration {
    WorkerConfiguration m_worker;
    std::string m_pose_python;
    PoseRecipe m_recipe { PoseRecipe::SEQUENTIAL };
    ByteBudget m_journal_bytes { 2uz * 1024 * 1024 * 1024 };
    std::uint32_t m_cpu_threads { 4 };
};

// This development command explicitly solves or continues a local pose job. It never silently
// changes recipes, restarts poses, downloads dependencies or adopts another fit.
struct ReconstructionJob {
    std::filesystem::path m_video;
    std::filesystem::path m_pose_job;
    std::filesystem::path m_gsplat_source;
    std::filesystem::path m_fit_directory;
    std::filesystem::path m_scene_output;
    WorkerConfiguration m_fitting_worker;
    std::uint32_t m_steps { 7000 };
    std::uint32_t m_max_image_dimension { 1280 };
    std::uint32_t m_max_splats { 2000000 };
    FitAction m_action { FitAction::START };
    std::filesystem::path m_checkpoint;
    std::string m_completed_fit_identity;
    // Empty map_directory selects the sibling FIT_DIRECTORY.map. Explicit
    // decode_stage selects a retained stage without rewriting a stale pointer.
    std::filesystem::path m_map_directory;
    std::filesystem::path m_decode_stage;
    // Schema 2 explicitly adopts checked metadata before retained pose preparation.
    // Absent in legacy schema 1; those requests do not claim video preflight.
    std::optional<RetainedVideoInspection> m_inspection;
    std::optional<AutomaticVideoInspection> m_automatic_inspection;
    // Filled by the reader so writable job outputs cannot contain the request.
    std::filesystem::path m_request_file;
    // Present only for an explicit schema-2 solve action. Decode selection is
    // separate from decode_stage, which selects the pose job's retained domain.
    std::optional<FreshPoseConfiguration> m_fresh_poses;
    std::optional<AutomaticVideoDecode> m_automatic_decode;
    std::optional<RetainedVideoDecode> m_selected_decode;
};

// Strict, bounded schema; relative paths resolve against the request's directory.
// An optional digest binds the exact bytes parsed, for reviewed-request launch.
Result<ReconstructionJob> read_reconstruction_job(const std::filesystem::path& request,
                                                  const std::string& expected_sha256 = {});
// One synchronous command, called off the UI thread. Logs run on the calling
// thread, including checked progress callbacks. Cancellation before manifest publication retains
// the previous scene; the final atomic publication is a commit boundary. Concurrent worker
// execution is excluded by the fit worker's lock. Completed reuse requires an explicit SHA.
Result<std::filesystem::path> reconstruct(const ReconstructionJob& job,
                                          const std::stop_token& stop,
                                          const LogCallback& log,
                                          const ReconstructionProgressCallback& progress = {});

}  // namespace world_explorer

#endif  // WORLD_EXPLORER_APPLICATION_RECONSTRUCTION_JOB_HPP
