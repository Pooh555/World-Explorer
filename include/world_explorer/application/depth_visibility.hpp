#ifndef WORLD_EXPLORER_APPLICATION_DEPTH_VISIBILITY_HPP
#define WORLD_EXPLORER_APPLICATION_DEPTH_VISIBILITY_HPP

#include "world_explorer/core/result.hpp"

#include <filesystem>
#include <stop_token>
#include <string>

namespace world_explorer {

// Pinned request describes independently checked resized inputs and 1-18 depth
// assets. Copies admitted assets/RGB/dependencies into a new owned directory,
// publishing manifest.json last. Failure can leave uncommitted owned files;
// callers must not consume them without the terminal receipt. No scene update,
// metric scale, fusion or reconstruction-quality certification is performed.
Result<std::string> export_depth_visibility_cache(const std::filesystem::path& request,
                                                  const std::string& request_sha256,
                                                  const std::filesystem::path& destination,
                                                  const std::stop_token& stop = {});

}  // namespace world_explorer
#endif
