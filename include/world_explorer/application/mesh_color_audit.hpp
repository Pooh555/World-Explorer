#pragma once

#include "world_explorer/core/result.hpp"

#include <filesystem>
#include <stop_token>

namespace world_explorer {

// Read-only source admission/projection audit. Publishes diagnostic samples,
// coverage/seam ledgers and an atlas contact sheet to a new directory. No scene
// update, material, usable texture export or geometry qualification is implied.
Result<std::string> audit_mesh_colors(const std::filesystem::path& request,
                                      const std::string& expected_sha256,
                                      const std::filesystem::path& destination,
                                      const std::stop_token& stop = {});

}  // namespace world_explorer
