#ifndef WORLD_EXPLORER_RENDER_CORE_RADIX_HPP
#define WORLD_EXPLORER_RENDER_CORE_RADIX_HPP
#include <bit>
#include <cstdint>

namespace world_explorer {

// Four-bit stable passes, rounded to pairs so ping-pong output returns to
// its original buffer. A zero-only key retains the emitted input order.
constexpr std::uint32_t tile_radix_passes(std::uint32_t maximum_key) {
    const auto K_BITS = static_cast<std::uint32_t>(std::bit_width(maximum_key));
    return (K_BITS + 7U) / 8U * 2U;
}

}  // namespace world_explorer
#endif  // WORLD_EXPLORER_RENDER_CORE_RADIX_HPP
