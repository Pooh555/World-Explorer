#ifndef WORLD_EXPLORER_RECONSTRUCTION_ADAPTERS_DECODED_FRAME_SOURCE_HPP
#define WORLD_EXPLORER_RECONSTRUCTION_ADAPTERS_DECODED_FRAME_SOURCE_HPP
#include "world_explorer/capture/frame_source.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace world_explorer {
struct decoded_source_config {
    std::filesystem::path stage_directory;
    std::string source_sha256;
    std::vector<std::string> decoder_arguments{"ffmpeg"};
    byte_budget max_image_bytes{std::size_t{64} * 1024 * 1024};
};
struct decoded_observation {
    frame_id id;
    calibration_id calibration;
    std::string external_frame_id, external_calibration_id;
    source_timestamp original_timestamp;
    std::chrono::nanoseconds capture_time;
    std::filesystem::path image_path;
    std::string image_sha256;
    std::size_t encoded_bytes{};
    std::uint32_t width{}, height{};
};
struct decoded_frame_catalog {
    capture_id capture;
    std::string completion_sha256;
    std::vector<decoded_observation> observations;
    std::optional<std::string> journal_sha256;
};
// Inspects the same bounded, checked metadata as open; no decoder is launched.
// Used image bytes still require checksum verification before consumption.
result<decoded_frame_catalog> inspect_decoded_frame_source(decoded_source_config config,
                                                           const std::stop_token& stop = {});
// Replay a committed timestamp_sampling_v1 or native_rgb_journal_v1 decode stage. This does not
// decode or reconstruct the source video again. One cancellable decoder runs per image; callers own
// immutable RGB8 leases. Seeking emits a reset before the next frame.
result<std::unique_ptr<frame_source>> open_decoded_frame_source(decoded_source_config config,
                                                                const std::stop_token& stop = {});
} // namespace world_explorer
#endif
