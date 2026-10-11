#ifndef WORLD_EXPLORER_RECONSTRUCTION_ADAPTERS_DECODED_FRAME_SOURCE_HPP
#define WORLD_EXPLORER_RECONSTRUCTION_ADAPTERS_DECODED_FRAME_SOURCE_HPP
#include "world_explorer/capture/frame_source.hpp"
#include <filesystem>
#include <string>

namespace world_explorer {
struct decoded_source_config {
    std::filesystem::path stage_directory;
    std::string source_sha256;
    std::vector<std::string> decoder_arguments{"ffmpeg"};
    byte_budget max_image_bytes{std::size_t{64} * 1024 * 1024};
};
// Replay a committed timestamp_sampling_v1 decode stage. This does not decode
// or reconstruct the source video again. One cancellable decoder runs per image;
// callers own immutable RGB8 leases. Seeking emits a reset before the next frame.
result<std::unique_ptr<frame_source>> open_decoded_frame_source(decoded_source_config config,
                                                                const std::stop_token& stop = {});
} // namespace world_explorer
#endif
