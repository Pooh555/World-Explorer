#ifndef WORLD_EXPLORER_RENDER_CORE_TILE_PLAN_HPP
#define WORLD_EXPLORER_RENDER_CORE_TILE_PLAN_HPP
#include "world_explorer/core/result.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace world_explorer {
struct tile_grid {
    std::uint32_t width{}, height{};
    bool operator==(const tile_grid&) const = default;
};
struct tile_rectangle {
    std::uint32_t x{}, y{}, width{}, height{};
    bool operator==(const tile_rectangle&) const = default;
};
// Stores geometry only. Every candidate still requires current-frame GPU count
// and resource admission. Splitting the owned active rectangle preserves exact
// coverage; callers cannot insert unvalidated rectangles into the cache.
class tile_region_plan {
  public:
    static constexpr std::size_t MAX_CACHED_REGIONS = 128;
    result<std::size_t> begin(tile_grid grid, bool reuse_cache = true);
    result<std::optional<tile_rectangle>> next();
    result<void> accept_current();
    result<void> split_current();
    // Commit only after the caller successfully submits the whole frame.
    // Excessive fragmentation discards the cache, never the remaining work.
    result<void> commit();
    void reset() noexcept;
    std::size_t cached_region_count() const noexcept { return cached_count_; }

  private:
    // At most 64 binary splits along a root-to-leaf path of uint32 dimensions.
    std::array<tile_rectangle, MAX_CACHED_REGIONS + 64> pending_{};
    std::array<tile_rectangle, MAX_CACHED_REGIONS> accepted_{}, cached_{};
    std::optional<tile_rectangle> active_;
    tile_grid grid_{}, cached_grid_{};
    std::size_t pending_count_{}, accepted_count_{}, cached_count_{};
    std::uint64_t accepted_area_{};
    bool running_{}, cacheable_{};
};
} // namespace world_explorer
#endif // WORLD_EXPLORER_RENDER_CORE_TILE_PLAN_HPP
