#pragma once

#include "world_explorer/core/result.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace world_explorer {

enum class GpuTimingStage : std::uint8_t {
    UPLOAD,
    PROJECTION,
    DEPTH_SORT,
    COUNT_SCAN,
    TILE_INDEX,
    COMPOSITION,
    PRESENTATION,
    STAGE_COUNT
};
constexpr auto K_GPU_TIMING_STAGE_COUNT { static_cast<std::size_t>(GpuTimingStage::STAGE_COUNT) };

struct RendererFrameTiming {
    std::uint64_t m_sequence {};  // Submitted render frame, published only after completion.
    std::uint32_t m_width {}, m_height {};
    bool m_gpu_valid {};
    std::array<double, K_GPU_TIMING_STAGE_COUNT> m_gpu_ms {};
    double m_cpu_frame_wait_ms {}, m_cpu_count_wait_ms {}, m_cpu_acquire_ms {}, m_cpu_present_ms {};
    std::uint64_t m_count_submissions {}, m_tile_batches {};
    std::uint32_t m_tile_sort_passes {};
    std::uint32_t m_reused_tile_regions {};
    std::size_t m_host_copy_bytes {}, m_resident_copy_bytes {};
};

// One wrap is representable; the measured interval must be shorter than the
// device's counter range. Bits above valid_bits are intentionally ignored.
inline Result<double> timestamp_duration_ms(std::uint64_t begin,
                                            std::uint64_t end,
                                            std::uint32_t valid_bits,
                                            double period_ns) {
    if (valid_bits == 0 || valid_bits > 64 || !std::isfinite(period_ns) || period_ns <= 0)
        return fail(ErrorCode::INVALID_ARGUMENT, "invalid GPU timestamp scale or bit width");
    const auto K_MASK = std::numeric_limits<std::uint64_t>::max() >> (64 - valid_bits);
    const auto K_DURATION = static_cast<double>((end - begin) & K_MASK) * period_ns / 1000000;
    if (!std::isfinite(K_DURATION))
        return fail(ErrorCode::INVALID_ARGUMENT, "GPU timestamp duration exceeds finite range");
    return K_DURATION;
}

}  // namespace world_explorer
