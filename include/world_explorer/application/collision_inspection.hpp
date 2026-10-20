#pragma once

#include "world_explorer/core/result.hpp"

#include <filesystem>
#include <stop_token>

namespace world_explorer {

// Publishes a synthetic color-coded support bundle and a pinned probe report.
// No measured collision or walking capability is produced.
Result<void> inspect_collision_support(const std::filesystem::path& request,
                                       const std::string& expected_sha256,
                                       const std::filesystem::path& destination,
                                       const std::stop_token& stop = {});

}  // namespace world_explorer
