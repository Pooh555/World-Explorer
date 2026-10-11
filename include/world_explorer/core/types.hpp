#ifndef WORLD_EXPLORER_CORE_TYPES_HPP
#define WORLD_EXPLORER_CORE_TYPES_HPP

#include <compare>
#include <cstddef>
#include <cstdint>

namespace world_explorer {
template <class tag> struct typed_id {
    std::uint64_t value{};
    auto operator<=>(const typed_id&) const = default;
};
using capture_id = typed_id<struct capture_tag>;
using frame_id = typed_id<struct frame_tag>;
using calibration_id = typed_id<struct calibration_tag>;
using submap_id = typed_id<struct submap_tag>;
using scene_id = typed_id<struct scene_tag>;
using chunk_id = typed_id<struct chunk_tag>;
using operation_id = typed_id<struct operation_tag>;
using scene_revision_id = typed_id<struct scene_revision_tag>;
using map_revision_id = typed_id<struct map_revision_tag>;
using world_epoch = typed_id<struct world_epoch_tag>;
using update_ticket = typed_id<struct update_ticket_tag>;
using observation_ticket = typed_id<struct observation_ticket_tag>;
struct byte_budget {
    std::size_t bytes{};
};
struct event_budget {
    std::size_t count{};
};
enum class scale_status : std::uint8_t { ARBITRARY, METRIC_MEASURED };
struct source_timestamp {
    std::int64_t pts{};
    std::int32_t time_base_numerator{1};
    std::int32_t time_base_denominator{1};
};
} // namespace world_explorer

#endif // WORLD_EXPLORER_CORE_TYPES_HPP
