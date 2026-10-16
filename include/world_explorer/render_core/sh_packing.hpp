#ifndef WORLD_EXPLORER_RENDER_CORE_SH_PACKING_HPP
#define WORLD_EXPLORER_RENDER_CORE_SH_PACKING_HPP

#include "world_explorer/core/result.hpp"
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace world_explorer {
// IEEE binary16, round to nearest with ties to even. Reject values outside the
// finite storage range; never silently saturate radiance or generate infinity.
inline std::optional<std::uint16_t> encode_finite_binary16(float value) {
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
    if (!std::isfinite(value) || std::abs(value) > 65504.F)
        return std::nullopt;
    const auto bits = std::bit_cast<std::uint32_t>(value);
    const auto sign = (bits >> 16U) & 0x8000U;
    const auto exponent = (bits >> 23U) & 0xffU;
    const auto mantissa = bits & 0x7fffffU;
    if (exponent < 102)
        return static_cast<std::uint16_t>(sign);
    const auto shift = exponent < 113 ? 126U - exponent : 13U;
    const auto significand = exponent < 113 ? mantissa | 0x800000U : mantissa;
    auto rounded = significand >> shift;
    const auto remainder = significand & ((1U << shift) - 1);
    const auto halfway = 1U << (shift - 1U);
    if (remainder > halfway || (remainder == halfway && (rounded & 1U)))
        ++rounded;
    const auto encoded = exponent < 113 ? rounded : ((exponent - 112) << 10U) + rounded;
    return static_cast<std::uint16_t>(sign | encoded);
}

// Coefficient-major RGB order is unchanged. First coefficient occupies the low
// 16 bits, matching GLSL unpackHalf2x16; all decode and SH evaluation use FP32.
inline result<std::array<std::uint32_t, 24>> pack_sh_binary16(const std::array<float, 48>& sh) {
    std::array<std::uint32_t, 24> packed{};
    for (std::size_t index = 0; index < sh.size(); ++index) {
        const auto encoded = encode_finite_binary16(sh[index]);
        if (!encoded)
            return fail(error_code::INVALID_ARGUMENT,
                        "SH coefficient exceeds finite binary16 range");
        packed[index / 2] |= static_cast<std::uint32_t>(*encoded) << (16 * (index % 2));
    }
    return packed;
}
} // namespace world_explorer

#endif // WORLD_EXPLORER_RENDER_CORE_SH_PACKING_HPP
