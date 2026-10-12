#ifndef WORLD_EXPLORER_APPLICATION_POSE_JOB_HPP
#define WORLD_EXPLORER_APPLICATION_POSE_JOB_HPP
#include "world_explorer/application/reconstruction_job.hpp"
#include "world_explorer/core/file_lock.hpp"

namespace world_explorer::job_detail {
// Holds the intent lock through map/fit/scene publication. Matching completed
// solves are checked/reused; interrupted input is replayed without overwriting
// admitted RGB assets. Caller then uses normal durable pose preparation.
result<std::unique_ptr<file_lock>> prepare_pose_job(const reconstruction_job& job,
                                                    const decoded_video& decoded,
                                                    const video_preflight_report& preflight,
                                                    const std::stop_token& stop,
                                                    const log_callback& log);
} // namespace world_explorer::job_detail
#endif
