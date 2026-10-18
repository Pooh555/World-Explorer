#ifndef WORLD_EXPLORER_CORE_TYPES_HPP
#define WORLD_EXPLORER_CORE_TYPES_HPP

#include <compare>
#include <cstddef>
#include <cstdint>

namespace world_explorer {

template <class Tag> struct TypedId {
    std::uint64_t m_value {};
    auto operator<=>(const TypedId&) const = default;
};
using CaptureId = TypedId<struct capture_tag>;
using FrameId = TypedId<struct frame_tag>;
using CalibrationId = TypedId<struct calibration_tag>;
using SubmapId = TypedId<struct submap_tag>;
using SceneId = TypedId<struct scene_tag>;
using ChunkId = TypedId<struct chunk_tag>;
using OperationId = TypedId<struct operation_tag>;
using SceneRevisionId = TypedId<struct scene_revision_tag>;
using MapRevisionId = TypedId<struct map_revision_tag>;
using WorldEpoch = TypedId<struct world_epoch_tag>;
using UpdateTicket = TypedId<struct update_ticket_tag>;
using ObservationTicket = TypedId<struct observation_ticket_tag>;

struct ByteBudget {
    std::size_t m_bytes {};
};

struct EventBudget {
    std::size_t m_count {};
};
enum class ScaleStatus : std::uint8_t { ARBITRARY, METRIC_MEASURED };

struct SourceTimestamp {
    std::int64_t m_pts {};
    std::int32_t m_time_base_numerator { 1 };
    std::int32_t m_time_base_denominator { 1 };
};

}  // namespace world_explorer

#endif  // WORLD_EXPLORER_CORE_TYPES_HPP
