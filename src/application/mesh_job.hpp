#pragma once

#include "world_explorer/application/commands.hpp"

namespace world_explorer::job_detail {

Result<std::filesystem::path> run_mesh_job(const std::filesystem::path& request,
                                           const std::string& expected_sha256,
                                           const std::filesystem::path& destination,
                                           const std::stop_token& stop,
                                           const LogCallback& log);

}  // namespace world_explorer::job_detail
