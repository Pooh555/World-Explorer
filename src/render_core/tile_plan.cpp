#include "world_explorer/render_core/tile_plan.hpp"

#include <algorithm>

namespace world_explorer {

Result<std::size_t> TileRegionPlan::begin(TileGrid grid, bool reuse_cache) {
    if (grid.m_width == 0 || grid.m_height == 0) {
        return fail(ErrorCode::INVALID_ARGUMENT, "tile plan needs a nonempty grid");
    }
    m_grid = grid;
    m_active.reset();
    m_accepted_count = 0;
    m_accepted_area = 0;
    m_cacheable = true;
    m_running = true;
    const auto K_REUSED = reuse_cache && grid == m_cached_grid ? m_cached_count : 0;
    m_pending_count = K_REUSED ? K_REUSED : 1;
    if (K_REUSED) {
        for (std::size_t index { 0uz }; index < K_REUSED; ++index) {
            m_pending[index] = m_cached[K_REUSED - index - 1];
        }
    } else {
        m_pending[0] = { 0, 0, grid.m_width, grid.m_height };
    }
    return K_REUSED;
}

Result<std::optional<TileRectangle>> TileRegionPlan::next() {
    if (!m_running || m_active) {
        return fail(ErrorCode::INVALID_ARGUMENT,
                    "resolve the active tile rectangle before advancing");
    }
    if (m_pending_count == 0) {
        return std::optional<TileRectangle> {};
    }
    m_active = m_pending[--m_pending_count];
    return m_active;
}

Result<void> TileRegionPlan::accept_current() {
    if (!m_running || !m_active) {
        return fail(ErrorCode::INVALID_ARGUMENT, "tile plan has no active rectangle to accept");
    }
    const auto K_AREA = static_cast<std::uint64_t>(m_active->m_width) * m_active->m_height;
    const auto K_TOTAL_AREA = static_cast<std::uint64_t>(m_grid.m_width) * m_grid.m_height;
    if (K_AREA > K_TOTAL_AREA - m_accepted_area) {
        return fail(ErrorCode::INVALID_ARGUMENT, "tile plan exceeds its grid area");
    }
    m_accepted_area += K_AREA;
    if (m_accepted_count < m_accepted.size()) {
        m_accepted[m_accepted_count++] = *m_active;
    } else {
        m_cacheable = false;
    }
    m_active.reset();
    return {};
}

Result<void> TileRegionPlan::split_current() {
    if (!m_running || !m_active) {
        return fail(ErrorCode::INVALID_ARGUMENT, "tile plan has no active rectangle to split");
    }
    if (m_active->m_width == 1 && m_active->m_height == 1) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED,
                    "one tile exceeds reference/residency limits before emission; no contributors "
                    "were dropped");
    }
    if (m_pending_count > m_pending.size() - 2) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "tile plan split stack exceeds its bound");
    }
    auto first = *m_active, remainder = *m_active;
    if (first.m_width >= first.m_height && first.m_width > 1) {
        first.m_width /= 2;
        remainder.m_x += first.m_width;
        remainder.m_width -= first.m_width;
    } else {
        first.m_height /= 2;
        remainder.m_y += first.m_height;
        remainder.m_height -= first.m_height;
    }
    m_pending[m_pending_count++] = remainder;
    m_pending[m_pending_count++] = first;
    m_active.reset();
    return {};
}

Result<void> TileRegionPlan::commit() {
    const auto K_TOTAL_AREA = static_cast<std::uint64_t>(m_grid.m_width) * m_grid.m_height;
    if (!m_running || m_active || m_pending_count != 0 || m_accepted_area != K_TOTAL_AREA) {
        return fail(ErrorCode::INVALID_ARGUMENT, "cannot cache an incomplete tile partition");
    }
    m_cached_count = m_cacheable ? m_accepted_count : 0;
    m_cached_grid = m_grid;
    std::copy_n(m_accepted.begin(), m_cached_count, m_cached.begin());
    m_running = false;
    return {};
}

void TileRegionPlan::reset() noexcept {
    m_active.reset();
    m_pending_count = m_accepted_count = m_cached_count = 0;
    m_accepted_area = 0;
    m_grid = m_cached_grid = {};
    m_running = m_cacheable = false;
}

}  // namespace world_explorer
