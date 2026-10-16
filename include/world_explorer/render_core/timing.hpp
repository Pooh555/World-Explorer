#ifndef WORLD_EXPLORER_RENDER_CORE_TIMING_HPP
#define WORLD_EXPLORER_RENDER_CORE_TIMING_HPP

#include "world_explorer/core/result.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace world_explorer {
enum class gpu_timing_stage : std::uint8_t {
    UPLOAD,
    PROJECTION,
    DEPTH_SORT,
    COUNT_SCAN,
    TILE_INDEX,
    COMPOSITION,
    PRESENTATION,
    STAGE_COUNT
};
constexpr auto GPU_TIMING_STAGE_COUNT = static_cast<std::size_t>(gpu_timing_stage::STAGE_COUNT);
struct renderer_frame_timing {
    std::uint64_t sequence{}; // Submitted render frame, published only after completion.
    std::uint32_t width{}, height{};
    bool gpu_valid{};
    std::array<double, GPU_TIMING_STAGE_COUNT> gpu_ms{};
    double cpu_frame_wait_ms{}, cpu_count_wait_ms{}, cpu_acquire_ms{}, cpu_present_ms{};
    std::uint64_t count_submissions{}, tile_batches{};
    std::uint32_t tile_sort_passes{};
    std::uint32_t reused_tile_regions{};
    std::size_t host_copy_bytes{}, resident_copy_bytes{};
};
// One wrap is representable; the measured interval must be shorter than the
// device's counter range. Bits above valid_bits are intentionally ignored.
inline result<double> timestamp_duration_ms(std::uint64_t begin, std::uint64_t end,
                                            std::uint32_t valid_bits, double period_ns) {
    if (valid_bits == 0 || valid_bits > 64 || !std::isfinite(period_ns) || period_ns <= 0)
        return fail(error_code::INVALID_ARGUMENT, "invalid GPU timestamp scale or bit width");
    const auto mask = std::numeric_limits<std::uint64_t>::max() >> (64 - valid_bits);
    const auto duration = static_cast<double>((end - begin) & mask) * period_ns / 1000000;
    if (!std::isfinite(duration))
        return fail(error_code::INVALID_ARGUMENT, "GPU timestamp duration exceeds finite range");
    return duration;
}
} // namespace world_explorer
#endif // WORLD_EXPLORER_RENDER_CORE_TIMING_HPP
