#ifndef WORLD_EXPLORER_APPLICATION_JOB_VALIDATION_HPP
#define WORLD_EXPLORER_APPLICATION_JOB_VALIDATION_HPP

#include "world_explorer/application/reconstruction_job.hpp"
#include "world_explorer/reconstruction_adapters/completed_poses.hpp"
#include <map>
#include <nlohmann/json.hpp>

namespace world_explorer::job_detail {
struct pose_input {
    std::filesystem::path directory;
    std::filesystem::path training;
    std::string completion_hash;
    std::map<std::string, std::string> file_hashes;
};
result<nlohmann::json> read_json(const std::filesystem::path& path, std::size_t maximum,
                                 const std::string& expected_sha256 = {});
bool valid_digest(const std::string& digest);
result<std::filesystem::path> contained_file(const std::filesystem::path& root,
                                             const std::string& relative);
result<pose_input> verify_poses(const reconstruction_job& job, const std::string& source_hash,
                                const std::stop_token& stop);
result<std::string> verify_fit(const reconstruction_job& job, const pose_input& poses,
                               const std::string& source_hash,
                               const completed_pose_dataset& dataset);
} // namespace world_explorer::job_detail

#endif // WORLD_EXPLORER_APPLICATION_JOB_VALIDATION_HPP
