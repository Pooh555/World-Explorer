#pragma once

#include "world_explorer/scene_store/scene.hpp"

#include <string_view>

namespace world_explorer {

constexpr std::size_t K_WORLD_FRAME_DOCUMENT_BYTES { 8192uz };

// Pins exact source content/epoch/revision and resolves three mesh anchors.
// Synthetic declarations are accepted only on explicitly synthetic scenes.
Result<WorldFrameAlignment>
admit_world_frame(const SceneState& source, std::string_view document, std::string_view sha256);
Result<void> validate_world_frame(const SceneState& aligned);
SimilarityTransform scene_world_from_chunk(const SceneState& scene, const SceneChunk& chunk);

enum class WalkingBlockReason : std::uint8_t {
    MISSING_MEASUREMENT,
    SYNTHETIC_DIAGNOSTIC,
    SUPPORTED_COLLISION_UNAVAILABLE
};

// No collision producer is implemented. Even an admitted measured frame cannot
// authorize walking or certify Q5/N2.
WalkingBlockReason walking_block_reason(const SceneState& scene);
std::string_view world_frame_label(const SceneState& scene);

}  // namespace world_explorer
