#ifndef WORLD_EXPLORER_APPLICATION_MAP_COMMANDS_HPP
#define WORLD_EXPLORER_APPLICATION_MAP_COMMANDS_HPP
#include "world_explorer/map_store/map_store.hpp"
namespace world_explorer {
result<map_snapshot> initialize_persistent_map(const std::filesystem::path& directory,
                                               const std::stop_token& stop = {});
result<map_snapshot> import_canonical_pose_map(const std::filesystem::path& asset,
                                               const std::string& hash,
                                               const std::filesystem::path& directory,
                                               const std::stop_token& stop = {});
result<map_snapshot> inspect_persistent_map(const std::filesystem::path& directory,
                                            const std::stop_token& stop = {});
} // namespace world_explorer
#endif
