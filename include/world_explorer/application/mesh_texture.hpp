#pragma once

#include "world_explorer/core/result.hpp"

#include <filesystem>
#include <stop_token>

namespace world_explorer {

// The same pinned request as the projection audit. Replays source admission,
// builds captured unlit appearance, then atomically publishes a new native bundle.
Result<void> bake_mesh_texture(const std::filesystem::path& request,
                               const std::string& expected_sha256,
                               const std::filesystem::path& destination,
                               const std::stop_token& stop = {});

}  // namespace world_explorer
