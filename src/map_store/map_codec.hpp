#ifndef WORLD_EXPLORER_MAP_STORE_MAP_CODEC_HPP
#define WORLD_EXPLORER_MAP_STORE_MAP_CODEC_HPP
#include "poses_generated.h"
#include "world_explorer/map_store/pose_asset.hpp"

namespace world_explorer::map_codec {

SimilarityTransform compose(const SimilarityTransform& a, const SimilarityTransform& b);
bool digest_valid(std::string_view value);
bool limits_valid(const PoseAssetLimits& limits);
Result<void>
validate_state(const MapState& map, const PoseAssetLimits& limits, const std::stop_token& stop);

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

Result<record_offsets> encode_records(flatbuffers::FlatBufferBuilder& builder,
                                      const MapState& map,
                                      const PoseAssetLimits& limits,
                                      const std::stop_token& stop);
Result<MapSnapshot>
decode_records(record_view records, const PoseAssetLimits& limits, const std::stop_token& stop);

}  // namespace world_explorer::map_codec
#endif
