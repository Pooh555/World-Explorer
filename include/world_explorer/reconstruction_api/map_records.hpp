#pragma once

#include "world_explorer/core/math.hpp"
#include "world_explorer/core/types.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>

namespace world_explorer {

enum class CalibrationModel : std::uint8_t { PINHOLE, OPENCV };

struct CalibrationRecord {
    CalibrationId m_id;
    // Integer pixel centers. This is the linear intrinsic matrix, not a claim
    // that a distorted image can be projected without the model coefficients.
    PinholeCamera m_camera;
    std::uint32_t m_width {}, m_height {};
    CalibrationModel m_model { CalibrationModel::PINHOLE };
    // Brown-Conrady k1, k2, p1, p2 for OPENCV; all zero for PINHOLE.
    std::array<double, 4> m_distortion {};
    // Canonical crop/resize: output_pixel = scale * input_pixel + offset.
    std::array<double, 4> m_image_transform { 1, 1, 0, 0 };
};

inline bool valid(const CalibrationRecord& value) {
    if (!value.m_id.m_value || !value.m_width || !value.m_height ||
        !project(value.m_camera, {0, 0, 1}) ||
        (value.m_model != CalibrationModel::PINHOLE && value.m_model != CalibrationModel::OPENCV))
        return false;
    for (double coefficient : value.m_distortion)
        if (!std::isfinite(coefficient) ||
            (value.m_model == CalibrationModel::PINHOLE && coefficient != 0))
            return false;
    for (double coefficient : value.m_image_transform)
        if (!std::isfinite(coefficient)) return false;
    return value.m_image_transform[0] > 0 && value.m_image_transform[1] > 0;
}

struct ObservationRecord {
    FrameId m_id;
    CaptureId m_capture;
    CalibrationId m_calibration;
    SourceTimestamp m_timestamp;
    std::chrono::nanoseconds m_capture_time;
    std::string m_image_sha256;
};

}  // namespace world_explorer
