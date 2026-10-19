#include "world_explorer/application/map_commands.hpp"

namespace world_explorer {

Result<MapSnapshot> initialize_persistent_map(const std::filesystem::path& directory,
                                              const std::stop_token& stop) {
    const auto K_STORE =
        MapStore::open_persistent({ .m_directory = directory, .m_create_if_missing = true }, stop);
    if (!K_STORE) {
        return std::unexpected(K_STORE.error());
    }
    return (*K_STORE)->acquire_snapshot();
}

Result<MapSnapshot> import_canonical_pose_map(const std::filesystem::path& asset,
                                              const std::string& hash,
                                              const std::filesystem::path& directory,
                                              const std::stop_token& stop) {
    const auto K_STORE = MapStore::open_persistent({ .m_directory = directory }, stop);
    if (!K_STORE) {
        return std::unexpected(K_STORE.error());
    }
    const auto K_IMPORTED = (*K_STORE)->import_pose_asset(asset, hash, { 1 }, stop);
    if (!K_IMPORTED) {
        return std::unexpected(K_IMPORTED.error());
    }
    return (*K_STORE)->acquire_snapshot();
}

Result<MapSnapshot> inspect_persistent_map(const std::filesystem::path& directory,
                                           const std::stop_token& stop) {
    const auto K_STORE = MapStore::open_persistent({ .m_directory = directory }, stop);
    if (!K_STORE) {
        return std::unexpected(K_STORE.error());
    }
    return (*K_STORE)->acquire_snapshot();
}

}  // namespace world_explorer
