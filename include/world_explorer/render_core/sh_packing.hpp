#pragma once

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
    if (!std::isfinite(value) || std::abs(value) > 65504.F) return std::nullopt;
    const auto K_BITS = std::bit_cast<std::uint32_t>(value);
    const auto K_SIGN = (K_BITS >> 16U) & 0x8000U;
    const auto K_EXPONENT = (K_BITS >> 23U) & 0xffU;
    const auto K_MANTISSA = K_BITS & 0x7fffffU;
    if (K_EXPONENT < 102) return static_cast<std::uint16_t>(K_SIGN);
    const auto K_SHIFT = K_EXPONENT < 113 ? 126U - K_EXPONENT : 13U;
    const auto K_SIGNIFICAND = K_EXPONENT < 113 ? K_MANTISSA | 0x800000U : K_MANTISSA;
    auto rounded = K_SIGNIFICAND >> K_SHIFT;
    const auto K_REMAINDER = K_SIGNIFICAND & ((1U << K_SHIFT) - 1);
    const auto K_HALFWAY = 1U << (K_SHIFT - 1U);
    if (K_REMAINDER > K_HALFWAY || (K_REMAINDER == K_HALFWAY && (rounded & 1U))) ++rounded;
    const auto K_ENCODED = K_EXPONENT < 113 ? rounded : ((K_EXPONENT - 112) << 10U) + rounded;
    return static_cast<std::uint16_t>(K_SIGN | K_ENCODED);
}

// Coefficient-major RGB order is unchanged. First coefficient occupies the low
// 16 bits, matching GLSL unpackHalf2x16; all decode and SH evaluation use FP32.
inline Result<std::array<std::uint32_t, 24>> pack_sh_binary16(const std::array<float, 48>& sh) {
    std::array<std::uint32_t, 24> packed {};
    for (std::size_t index { 0uz }; index < sh.size(); ++index) {
        const auto K_ENCODED = encode_finite_binary16(sh[index]);
        if (!K_ENCODED)
            return fail(ErrorCode::INVALID_ARGUMENT,
                        "SH coefficient exceeds finite binary16 range");
        packed[index / 2] |= static_cast<std::uint32_t>(*K_ENCODED) << (16 * (index % 2));
    }
    return packed;
}

}  // namespace world_explorer
