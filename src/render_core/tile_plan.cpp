#include "world_explorer/render_core/tile_plan.hpp"
#include <algorithm>

namespace world_explorer {
result<std::size_t> tile_region_plan::begin(tile_grid grid, bool reuse_cache) {
    if (grid.width == 0 || grid.height == 0)
        return fail(error_code::INVALID_ARGUMENT, "tile plan needs a nonempty grid");
    grid_ = grid;
    active_.reset();
    accepted_count_ = 0;
    accepted_area_ = 0;
    cacheable_ = true;
    running_ = true;
    const auto reused = reuse_cache && grid == cached_grid_ ? cached_count_ : 0;
    pending_count_ = reused ? reused : 1;
    if (reused)
        for (std::size_t index = 0; index < reused; ++index)
            pending_[index] = cached_[reused - index - 1];
    else
        pending_[0] = {0, 0, grid.width, grid.height};
    return reused;
}
result<std::optional<tile_rectangle>> tile_region_plan::next() {
    if (!running_ || active_)
        return fail(error_code::INVALID_ARGUMENT,
                    "resolve the active tile rectangle before advancing");
    if (pending_count_ == 0)
        return std::optional<tile_rectangle>{};
    active_ = pending_[--pending_count_];
    return active_;
}
result<void> tile_region_plan::accept_current() {
    if (!running_ || !active_)
        return fail(error_code::INVALID_ARGUMENT, "tile plan has no active rectangle to accept");
    const auto area = static_cast<std::uint64_t>(active_->width) * active_->height;
    const auto total_area = static_cast<std::uint64_t>(grid_.width) * grid_.height;
    if (area > total_area - accepted_area_)
        return fail(error_code::INVALID_ARGUMENT, "tile plan exceeds its grid area");
    accepted_area_ += area;
    if (accepted_count_ < accepted_.size())
        accepted_[accepted_count_++] = *active_;
    else
        cacheable_ = false;
    active_.reset();
    return {};
}
result<void> tile_region_plan::split_current() {
    if (!running_ || !active_)
        return fail(error_code::INVALID_ARGUMENT, "tile plan has no active rectangle to split");
    if (active_->width == 1 && active_->height == 1)
        return fail(error_code::RESOURCE_EXHAUSTED,
                    "one tile exceeds reference/residency limits before emission; no contributors "
                    "were dropped");
    if (pending_count_ > pending_.size() - 2)
        return fail(error_code::RESOURCE_EXHAUSTED, "tile plan split stack exceeds its bound");
    auto first = *active_, remainder = *active_;
    if (first.width >= first.height && first.width > 1) {
        first.width /= 2;
        remainder.x += first.width;
        remainder.width -= first.width;
    } else {
        first.height /= 2;
        remainder.y += first.height;
        remainder.height -= first.height;
    }
    pending_[pending_count_++] = remainder;
    pending_[pending_count_++] = first;
    active_.reset();
    return {};
}
result<void> tile_region_plan::commit() {
    const auto total_area = static_cast<std::uint64_t>(grid_.width) * grid_.height;
    if (!running_ || active_ || pending_count_ != 0 || accepted_area_ != total_area)
        return fail(error_code::INVALID_ARGUMENT, "cannot cache an incomplete tile partition");
    cached_count_ = cacheable_ ? accepted_count_ : 0;
    cached_grid_ = grid_;
    std::copy_n(accepted_.begin(), cached_count_, cached_.begin());
    running_ = false;
    return {};
}
void tile_region_plan::reset() noexcept {
    active_.reset();
    pending_count_ = accepted_count_ = cached_count_ = 0;
    accepted_area_ = 0;
    grid_ = cached_grid_ = {};
    running_ = cacheable_ = false;
}
} // namespace world_explorer
