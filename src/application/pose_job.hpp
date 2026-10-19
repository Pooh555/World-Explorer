#pragma once

#include "world_explorer/application/reconstruction_job.hpp"
#include "world_explorer/core/file_lock.hpp"

namespace world_explorer::job_detail {

// Holds the intent lock through map/fit/scene publication. Matching completed
// solves are checked/reused; interrupted input is replayed without overwriting
// admitted RGB assets. Caller then uses normal durable pose preparation.
Result<std::unique_ptr<FileLock>> prepare_pose_job(const ReconstructionJob& job,
                                                   const DecodedVideo& decoded,
                                                   const VideoPreflightReport& preflight,
                                                   const std::stop_token& stop,
                                                   const LogCallback& log,
                                                   const WorkerEventCallback& observed);

}  // namespace world_explorer::job_detail
