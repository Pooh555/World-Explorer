#ifndef WORLD_EXPLORER_RECONSTRUCTION_API_MAP_RECORDS_HPP
#define WORLD_EXPLORER_RECONSTRUCTION_API_MAP_RECORDS_HPP
#include "world_explorer/core/math.hpp"
#include "world_explorer/core/types.hpp"
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>

namespace world_explorer {
enum class calibration_model : std::uint8_t { PINHOLE, OPENCV };
struct calibration_record {
    calibration_id id;
    // Integer pixel centers. This is the linear intrinsic matrix, not a claim
    // that a distorted image can be projected without the model coefficients.
    pinhole_camera camera;
    std::uint32_t width{}, height{};
    calibration_model model{calibration_model::PINHOLE};
    // Brown-Conrady k1, k2, p1, p2 for OPENCV; all zero for PINHOLE.
    std::array<double, 4> distortion{};
    // Canonical crop/resize: output_pixel = scale * input_pixel + offset.
    std::array<double, 4> image_transform{1, 1, 0, 0};
};
inline bool valid(const calibration_record& value) {
    if (!value.id.value || !value.width || !value.height || !project(value.camera, {0, 0, 1}) ||
        (value.model != calibration_model::PINHOLE && value.model != calibration_model::OPENCV))
        return false;
    for (double coefficient : value.distortion)
        if (!std::isfinite(coefficient) ||
            (value.model == calibration_model::PINHOLE && coefficient != 0))
            return false;
    for (double coefficient : value.image_transform)
        if (!std::isfinite(coefficient))
            return false;
    return value.image_transform[0] > 0 && value.image_transform[1] > 0;
}
struct observation_record {
    frame_id id;
    capture_id capture;
    calibration_id calibration;
    source_timestamp timestamp;
    std::chrono::nanoseconds capture_time;
    std::string image_sha256;
};
} // namespace world_explorer
#endif
