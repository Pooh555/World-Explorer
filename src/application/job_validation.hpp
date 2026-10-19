#pragma once

#include "world_explorer/application/reconstruction_job.hpp"
#include "world_explorer/reconstruction_adapters/completed_poses.hpp"

#include <nlohmann/json.hpp>

#include <map>

namespace world_explorer::job_detail {

struct pose_input {
    std::filesystem::path directory;
    std::filesystem::path training;
    std::string completion_hash;
    std::map<std::string, std::string> file_hashes;
};

Result<nlohmann::json> read_json(const std::filesystem::path& path,
                                 std::size_t maximum,
                                 const std::string& expected_sha256 = {});
bool valid_digest(const std::string& digest);
Result<std::filesystem::path> contained_file(const std::filesystem::path& root,
                                             const std::string& relative);
Result<pose_input> verify_poses(const ReconstructionJob& job,
                                const std::string& source_hash,
                                const std::stop_token& stop);
Result<std::string> verify_fit(const ReconstructionJob& job,
                               const pose_input& poses,
                               const std::string& source_hash,
                               const CompletedPoseDataset& dataset);

}  // namespace world_explorer::job_detail
