#pragma once

#include "world_explorer/core/math.hpp"

#include <cstdint>
#include <filesystem>
#include <stop_token>
#include <string>
#include <vector>

namespace world_explorer {

struct ColmapImagePose {
    std::uint32_t m_image_id {}, m_camera_id {};
    std::string m_name;
    // Canonical world = Rx(pi) * COLMAP world; optical axes unchanged.
    SimilarityTransform m_world_from_camera;
};

// Bounded images.bin interchange, not a solver/SDK. Checks all track coordinates
// even though tracks are not retained. At most 120 images / 2M points per image.
Result<std::vector<ColmapImagePose>> read_colmap_image_poses(const std::filesystem::path& file,
                                                             const std::string& expected_sha256,
                                                             const std::stop_token& stop = {});

}  // namespace world_explorer
