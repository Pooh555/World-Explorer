#ifndef WORLD_EXPLORER_RECONSTRUCTION_ADAPTERS_OBSERVATION_IDENTITY_HPP
#define WORLD_EXPLORER_RECONSTRUCTION_ADAPTERS_OBSERVATION_IDENTITY_HPP
#include "world_explorer/capture/frame_source.hpp"
#include "world_explorer/core/hash.hpp"

#include <array>
#include <charconv>
#include <cstdint>
#include <stdexcept>
#include <string_view>

namespace world_explorer::adapter_detail {

inline std::string header_digest(const FramePacket& frame) {
    const std::array values {
        frame.m_id.m_value,
        frame.m_capture.m_value,
        frame.m_calibration.m_value,
        static_cast<std::uint64_t>(frame.m_capture_time.count()),
        static_cast<std::uint64_t>(frame.m_original_timestamp.m_pts),
        static_cast<std::uint64_t>(frame.m_original_timestamp.m_time_base_numerator),
        static_cast<std::uint64_t>(frame.m_original_timestamp.m_time_base_denominator),
        static_cast<std::uint64_t>(frame.m_pixels.m_width),
        static_cast<std::uint64_t>(frame.m_pixels.m_height),
        static_cast<std::uint64_t>(frame.m_pixels.m_row_stride),
        static_cast<std::uint64_t>(frame.m_pixels.m_format),
        static_cast<std::uint64_t>(frame.m_measured_depth.has_value()),
        static_cast<std::uint64_t>(frame.m_imu_samples.size())};
    std::array<std::byte, values.size() * 8> bytes {};
    std::size_t offset {};
    for (auto value : values)
        for (int index { 0 }; index < 8; ++index) {
            bytes[offset++] = static_cast<std::byte>(value & 255ULL);
            value >>= 8U;
        }
    return sha256(bytes);
}

inline std::uint64_t
identity(const std::string& source, std::string_view kind, std::string_view external) {
    const auto input = "world_explorer.observation.v1/" + source + "/" + std::string(kind) + "/" +
                       std::string(external);
    const auto digest = sha256(std::as_bytes(std::span(input.data(), input.size())));
    std::uint64_t value {};
    const auto parsed = std::from_chars(digest.data(), digest.data() + 16, value, 16);
    if (parsed.ec != std::errc {} || value == 0)
        throw std::invalid_argument("Invalid observation identity");
    return value;
}

}  // namespace world_explorer::adapter_detail
#endif
