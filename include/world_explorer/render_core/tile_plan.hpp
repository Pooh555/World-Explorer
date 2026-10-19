#pragma once

#include "world_explorer/core/result.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace world_explorer {

struct TileGrid {
    std::uint32_t m_width {}, m_height {};
    bool operator==(const TileGrid&) const = default;
};

struct TileRectangle {
    std::uint32_t m_x {}, m_y {}, m_width {}, m_height {};
    bool operator==(const TileRectangle&) const = default;
};

// Stores geometry only. Every candidate still requires current-frame GPU count
// and resource admission. Splitting the owned active rectangle preserves exact
// coverage; callers cannot insert unvalidated rectangles into the cache.
class TileRegionPlan {
  public:
    static constexpr std::size_t K_MAX_CACHED_REGIONS { 128uz };
    Result<std::size_t> begin(TileGrid grid, bool reuse_cache = true);
    Result<std::optional<TileRectangle>> next();
    Result<void> accept_current();
    Result<void> split_current();
    // Commit only after the caller successfully submits the whole frame.
    // Excessive fragmentation discards the cache, never the remaining work.
    Result<void> commit();
    void reset() noexcept;

    std::size_t cached_region_count() const noexcept { return m_cached_count; }

  private:
    // At most 64 binary splits along a root-to-leaf path of uint32 dimensions.
    std::array<TileRectangle, K_MAX_CACHED_REGIONS + 64> m_pending {};
    std::array<TileRectangle, K_MAX_CACHED_REGIONS> m_accepted {}, m_cached {};
    std::optional<TileRectangle> m_active;
    TileGrid m_grid {}, m_cached_grid {};
    std::size_t m_pending_count {}, m_accepted_count {}, m_cached_count {};
    std::uint64_t m_accepted_area {};
    bool m_running {}, m_cacheable {};
};

}  // namespace world_explorer
