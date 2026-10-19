#pragma once

#include "world_explorer/core/math.hpp"
#include "world_explorer/core/types.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <stop_token>
#include <variant>
#include <vector>

namespace world_explorer {

enum class PixelFormat : std::uint8_t { RGB8, GRAY8, DEPTH_FLOAT32 };

struct ImageLease {
    std::shared_ptr<const std::vector<std::byte>> m_bytes;
    std::uint32_t m_width {}, m_height {}, m_row_stride {};
    PixelFormat m_format { PixelFormat::RGB8 };
};

struct ImuSample {
    std::chrono::nanoseconds m_capture_time;
    Vec3d m_acceleration, m_angular_velocity;
};

struct FramePacket {
    FrameId m_id;
    CaptureId m_capture;
    std::chrono::nanoseconds m_capture_time;
    SourceTimestamp m_original_timestamp;
    CalibrationId m_calibration;
    ImageLease m_pixels;
    std::optional<ImageLease> m_measured_depth;
    std::vector<ImuSample> m_imu_samples;
};

struct FrameGap {
    FrameId m_after;
    std::chrono::nanoseconds m_duration;
};

struct SourceReset {
    CaptureId m_capture;
    WorldEpoch m_epoch;
};

struct SourceEnd { };

using SourceEvent = std::variant<FramePacket, FrameGap, SourceReset, SourceEnd>;

struct SourceCapabilities {
    bool m_seekable {};
    bool m_measured_depth {};
    bool m_imu {};
    ByteBudget m_max_image_bytes;
};

// One source owner calls read/seek. stop interrupts I/O. Pixels remain alive through
// immutable leases. Capture times are monotonic within each source_reset segment.
class FrameSource {
  public:
    virtual ~FrameSource() = default;
    virtual SourceCapabilities capabilities() const noexcept = 0;
    virtual Result<SourceEvent> read_next(std::stop_token stop) = 0;
    virtual Result<void> seek(SourceTimestamp target) = 0;
};

}  // namespace world_explorer
