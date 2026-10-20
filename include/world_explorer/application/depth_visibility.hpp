#pragma once

#include "world_explorer/core/result.hpp"

#include <filesystem>
#include <stop_token>
#include <string>

namespace world_explorer {

// Schema 1 admits 1-18 depth assets. Schema 2 accounts for every input frame
// (at most 120), publishing a union of independently bounded schema-1 caches.
// Copies admitted assets/RGB/dependencies into a new owned directory,
// publishing manifest.json last. Failure can leave uncommitted owned files;
// callers must not consume them without the terminal receipt. No scene update,
// metric scale, fusion or reconstruction-quality certification is performed.
Result<std::string> export_depth_visibility_cache(const std::filesystem::path& request,
                                                  const std::string& request_sha256,
                                                  const std::filesystem::path& destination,
                                                  const std::stop_token& stop = {});

}  // namespace world_explorer
