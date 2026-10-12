#ifndef WORLD_EXPLORER_MAP_STORE_MAP_CODEC_HPP
#define WORLD_EXPLORER_MAP_STORE_MAP_CODEC_HPP
#include "poses_generated.h"
#include "world_explorer/map_store/pose_asset.hpp"

namespace world_explorer::map_codec {
similarity_transform compose(const similarity_transform& a, const similarity_transform& b);
bool digest_valid(std::string_view value);
bool limits_valid(const pose_asset_limits& limits);
result<void> validate_state(const map_state& map, const pose_asset_limits& limits,
                            const std::stop_token& stop);
struct record_offsets {
    std::vector<flatbuffers::Offset<pose_wire::calibration>> calibrations;
    std::vector<flatbuffers::Offset<pose_wire::observation>> observations;
    std::vector<flatbuffers::Offset<pose_wire::alignment>> alignments;
    std::vector<flatbuffers::Offset<pose_wire::pose>> poses;
    std::vector<std::uint64_t> invalidated;
};
struct record_view {
    std::uint64_t epoch, revision;
    const flatbuffers::Vector<flatbuffers::Offset<pose_wire::calibration>>* calibrations;
    const flatbuffers::Vector<flatbuffers::Offset<pose_wire::observation>>* observations;
    const flatbuffers::Vector<flatbuffers::Offset<pose_wire::alignment>>* alignments;
    const flatbuffers::Vector<flatbuffers::Offset<pose_wire::pose>>* poses;
    const flatbuffers::Vector<std::uint64_t>* invalidated_frames;
};
result<record_offsets> encode_records(flatbuffers::FlatBufferBuilder& builder, const map_state& map,
                                      const pose_asset_limits& limits, const std::stop_token& stop);
result<map_snapshot> decode_records(record_view records, const pose_asset_limits& limits,
                                    const std::stop_token& stop);
} // namespace world_explorer::map_codec
#endif
