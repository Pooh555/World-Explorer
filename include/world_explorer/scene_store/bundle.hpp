#pragma once

#include "world_explorer/scene_store/scene.hpp"

#include <filesystem>
#include <string_view>

namespace world_explorer {

struct BundleLimits {
    SceneLimits m_scene;
    std::size_t m_max_manifest_bytes { 1024uz * 1024uz };
    std::size_t m_max_ply_header_bytes { 65536uz };
};

// Splat-only scenes retain schema 1 bytes; mesh snapshots use schema 2 chunks.
// Immutable content-addressed chunks are flushed before manifest.json is replaced.
// Existing readers continue using the old manifest/assets after failed publication.
Result<void> write_bundle(const std::filesystem::path& directory, const SceneSnapshot& snapshot);
// An optional external pin checks the same manifest bytes that are decoded.
Result<SceneSnapshot> load_bundle(const std::filesystem::path& directory,
                                  BundleLimits limits = {},
                                  std::string_view expected_manifest_sha256 = {});
// Only the explicitly annotated gsplat-1.5.3 ASCII interchange profile is accepted.
Result<SceneSnapshot>
load_gsplat_ply(const std::filesystem::path& path, SceneId id, BundleLimits limits = {});

}  // namespace world_explorer
