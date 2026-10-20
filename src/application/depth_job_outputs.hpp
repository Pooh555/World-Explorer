#pragma once

#include "world_explorer/application/dense_inputs.hpp"

#include <nlohmann/json.hpp>

#include <span>

namespace world_explorer::job_detail {

// Read-only terminal admission against independently prepared native frames.
// Checks all solved support assets, but selects each reference only once.
// Returns the visibility schema and ordered outputs; publishes no caller data.
Result<nlohmann::json> depth_job_outputs(const std::filesystem::path& job,
                                         const std::string& request_sha256,
                                         const std::filesystem::path& request,
                                         std::span<const PreparedDenseFrame> frames,
                                         const std::stop_token& stop = {});

}  // namespace world_explorer::job_detail
