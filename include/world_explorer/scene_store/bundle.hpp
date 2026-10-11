#ifndef WORLD_EXPLORER_SCENE_STORE_BUNDLE_HPP
#define WORLD_EXPLORER_SCENE_STORE_BUNDLE_HPP

#include "world_explorer/scene_store/scene.hpp"
#include <filesystem>

namespace world_explorer {
struct bundle_limits {
    scene_limits scene;
    std::size_t max_manifest_bytes{std::size_t{1024} * 1024};
    std::size_t max_ply_header_bytes{65536};
};
// Immutable content-addressed chunks are flushed before manifest.json is replaced.
// Existing readers continue using the old manifest/assets after failed publication.
result<void> write_bundle(const std::filesystem::path& directory, const scene_snapshot& snapshot);
result<scene_snapshot> load_bundle(const std::filesystem::path& directory,
                                   bundle_limits limits = {});
// Only the explicitly annotated gsplat-1.5.3 ASCII interchange profile is accepted.
result<scene_snapshot> load_gsplat_ply(const std::filesystem::path& path, scene_id id,
                                       bundle_limits limits = {});
} // namespace world_explorer

#endif // WORLD_EXPLORER_SCENE_STORE_BUNDLE_HPP
