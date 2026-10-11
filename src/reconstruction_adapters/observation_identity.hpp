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
inline std::string header_digest(const frame_packet& frame) {
    const std::array values{
        frame.id.value,
        frame.capture.value,
        frame.calibration.value,
        static_cast<std::uint64_t>(frame.capture_time.count()),
        static_cast<std::uint64_t>(frame.original_timestamp.pts),
        static_cast<std::uint64_t>(frame.original_timestamp.time_base_numerator),
        static_cast<std::uint64_t>(frame.original_timestamp.time_base_denominator),
        static_cast<std::uint64_t>(frame.pixels.width),
        static_cast<std::uint64_t>(frame.pixels.height),
        static_cast<std::uint64_t>(frame.pixels.row_stride),
        static_cast<std::uint64_t>(frame.pixels.format),
        static_cast<std::uint64_t>(frame.measured_depth.has_value()),
        static_cast<std::uint64_t>(frame.imu_samples.size())};
    std::array<std::byte, values.size() * 8> bytes{};
    std::size_t offset{};
    for (auto value : values)
        for (int index = 0; index < 8; ++index) {
            bytes[offset++] = static_cast<std::byte>(value & 255ULL);
            value >>= 8U;
        }
    return sha256(bytes);
}
inline std::uint64_t identity(const std::string& source, std::string_view kind,
                              std::string_view external) {
    const auto input = "world_explorer.observation.v1/" + source + "/" + std::string(kind) + "/" +
                       std::string(external);
    const auto digest = sha256(std::as_bytes(std::span(input.data(), input.size())));
    std::uint64_t value{};
    const auto parsed = std::from_chars(digest.data(), digest.data() + 16, value, 16);
    if (parsed.ec != std::errc{} || value == 0)
        throw std::invalid_argument("Invalid observation identity");
    return value;
}
} // namespace world_explorer::adapter_detail
#endif
