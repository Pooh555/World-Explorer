#pragma once

#include "world_explorer/map_store/map_store.hpp"

namespace world_explorer {

Result<MapSnapshot> initialize_persistent_map(const std::filesystem::path& directory,
                                              const std::stop_token& stop = {});
Result<MapSnapshot> import_canonical_pose_map(const std::filesystem::path& asset,
                                              const std::string& hash,
                                              const std::filesystem::path& directory,
                                              const std::stop_token& stop = {});
Result<MapSnapshot> inspect_persistent_map(const std::filesystem::path& directory,
                                           const std::stop_token& stop = {});

}  // namespace world_explorer
