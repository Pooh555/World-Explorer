#include "world_explorer/application/map_commands.hpp"
namespace world_explorer {
result<map_snapshot> initialize_persistent_map(const std::filesystem::path& directory,
                                               const std::stop_token& stop) {
    const auto store =
        map_store::open_persistent({.directory = directory, .create_if_missing = true}, stop);
    if (!store)
        return std::unexpected(store.error());
    return (*store)->acquire_snapshot();
}
result<map_snapshot> import_canonical_pose_map(const std::filesystem::path& asset,
                                               const std::string& hash,
                                               const std::filesystem::path& directory,
                                               const std::stop_token& stop) {
    const auto store = map_store::open_persistent({.directory = directory}, stop);
    if (!store)
        return std::unexpected(store.error());
    const auto imported = (*store)->import_pose_asset(asset, hash, {1}, stop);
    if (!imported)
        return std::unexpected(imported.error());
    return (*store)->acquire_snapshot();
}
result<map_snapshot> inspect_persistent_map(const std::filesystem::path& directory,
                                            const std::stop_token& stop) {
    const auto store = map_store::open_persistent({.directory = directory}, stop);
    if (!store)
        return std::unexpected(store.error());
    return (*store)->acquire_snapshot();
}
} // namespace world_explorer
