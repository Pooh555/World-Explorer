#ifndef WORLD_EXPLORER_RECONSTRUCTION_ADAPTERS_COLMAP_IMAGES_HPP
#define WORLD_EXPLORER_RECONSTRUCTION_ADAPTERS_COLMAP_IMAGES_HPP
#include "world_explorer/core/math.hpp"
#include <cstdint>
#include <filesystem>
#include <stop_token>
#include <string>
#include <vector>
namespace world_explorer {
struct colmap_image_pose {
    std::uint32_t image_id{}, camera_id{};
    std::string name;
    // Canonical world = Rx(pi) * COLMAP world; optical axes unchanged.
    similarity_transform world_from_camera;
};
// Bounded images.bin interchange, not a solver/SDK. Checks all track coordinates
// even though tracks are not retained. At most 120 images / 2M points per image.
result<std::vector<colmap_image_pose>> read_colmap_image_poses(const std::filesystem::path& file,
                                                               const std::string& expected_sha256,
                                                               const std::stop_token& stop = {});
} // namespace world_explorer
#endif
