#ifndef WORLD_EXPLORER_CAPTURE_FRAME_SOURCE_HPP
#define WORLD_EXPLORER_CAPTURE_FRAME_SOURCE_HPP
#include <cstdint>

#include "world_explorer/core/math.hpp"
#include "world_explorer/core/types.hpp"
#include <chrono>
#include <memory>
#include <optional>
#include <stop_token>
#include <variant>
#include <vector>

namespace world_explorer {
enum class pixel_format : std::uint8_t { RGB8, GRAY8, DEPTH_FLOAT32 };
struct image_lease {
    std::shared_ptr<const std::vector<std::byte>> bytes;
    std::uint32_t width{}, height{}, row_stride{};
    pixel_format format{pixel_format::RGB8};
};
struct imu_sample {
    std::chrono::nanoseconds capture_time;
    vec3d acceleration, angular_velocity;
};
struct frame_packet {
    frame_id id;
    capture_id capture;
    std::chrono::nanoseconds capture_time;
    source_timestamp original_timestamp;
    calibration_id calibration;
    image_lease pixels;
    std::optional<image_lease> measured_depth;
    std::vector<imu_sample> imu_samples;
};
struct frame_gap {
    frame_id after;
    std::chrono::nanoseconds duration;
};
struct source_reset {
    capture_id capture;
    world_epoch epoch;
};
struct source_end {};
using source_event = std::variant<frame_packet, frame_gap, source_reset, source_end>;
struct source_capabilities {
    bool seekable{};
    bool measured_depth{};
    bool imu{};
    byte_budget max_image_bytes;
};
// One source owner calls read/seek. stop interrupts I/O. Pixels remain alive through
// immutable leases. Capture times are monotonic within each source_reset segment.
class frame_source {
  public:
    virtual ~frame_source() = default;
    virtual source_capabilities capabilities() const noexcept = 0;
    virtual result<source_event> read_next(std::stop_token stop) = 0;
    virtual result<void> seek(source_timestamp target) = 0;
};
} // namespace world_explorer

#endif // WORLD_EXPLORER_CAPTURE_FRAME_SOURCE_HPP
