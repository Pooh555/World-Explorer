#pragma once

#include "world_explorer/scene_store/scene.hpp"

#include <span>
#include <stop_token>

namespace world_explorer {

// Restricted, self-contained captured-unlit GLB profile. No external URIs, PBR,
// inferred color or measured units. Import accepts this producer profile only.
Result<std::vector<std::byte>> encode_baked_glb(const SceneSnapshot& scene,
                                                const std::stop_token& stop = {});
Result<SceneSnapshot> decode_baked_glb(std::span<const std::byte> bytes,
                                       const std::stop_token& stop = {});

}  // namespace world_explorer
