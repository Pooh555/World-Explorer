#ifndef WORLD_EXPLORER_RECONSTRUCTION_ADAPTERS_TRAINING_CAMERA_HPP
#define WORLD_EXPLORER_RECONSTRUCTION_ADAPTERS_TRAINING_CAMERA_HPP
#include "world_explorer/core/math.hpp"
#include <cstdint>
#include <filesystem>
#include <stop_token>
#include <string>

namespace world_explorer {
struct training_camera {
    // Undistorted calibration; canonical integer pixel centers. This is distinct
    // from the distorted capture calibration and its image domain.
    pinhole_camera camera;
    std::uint32_t width{}, height{};
};
// Checked, bounded COLMAP binary interchange: one SIMPLE_PINHOLE/PINHOLE
// camera only. No solver/SDK linkage, side effects or fallback to capture intrinsics.
result<training_camera> read_colmap_training_camera(const std::filesystem::path& file,
                                                    const std::string& expected_sha256,
                                                    const std::stop_token& stop = {});
} // namespace world_explorer
#endif
