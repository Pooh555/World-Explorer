#pragma once

#include "world_explorer/core/result.hpp"

#include <filesystem>
#include <stop_token>

namespace world_explorer {

// Uses the existing pinned color/cache request. Fixed V1 recipe covers the full
// source bounds with at most 32 cells per axis; no solve or measured collision.
Result<void> inspect_depth_space(const std::filesystem::path& request,
                                 const std::string& expected_sha256,
                                 const std::filesystem::path& destination,
                                 const std::stop_token& stop = {});

}  // namespace world_explorer
