#pragma once

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

struct DecodedSourceConfig {
    std::filesystem::path m_stage_directory;
    std::string m_source_sha256;
    std::vector<std::string> m_decoder_arguments { "ffmpeg" };
    ByteBudget m_max_image_bytes { 64uz * 1024 * 1024 };
};

struct DecodedObservation {
    FrameId m_id;
    CalibrationId m_calibration;
    std::string m_external_frame_id, m_external_calibration_id;
    SourceTimestamp m_original_timestamp;
    std::chrono::nanoseconds m_capture_time;
    std::filesystem::path m_image_path;
    std::string m_image_sha256;
    std::size_t m_encoded_bytes {};
    std::uint32_t m_width {}, m_height {};
};

struct DecodedFrameCatalog {
    CaptureId m_capture;
    std::string m_completion_sha256;
    std::vector<DecodedObservation> m_observations;
    std::optional<std::string> m_journal_sha256;
};

// Inspects the same bounded, checked metadata as open; no decoder is launched.
// Used image bytes still require checksum verification before consumption.
Result<DecodedFrameCatalog> inspect_decoded_frame_source(DecodedSourceConfig config,
                                                         const std::stop_token& stop = {});
// Replay a committed timestamp_sampling_v1 or native_rgb_journal_v1 decode stage. This does not
// decode or reconstruct the source video again. One cancellable decoder runs per image; callers own
// immutable RGB8 leases. Seeking emits a reset before the next frame.
Result<std::unique_ptr<FrameSource>> open_decoded_frame_source(DecodedSourceConfig config,
                                                               const std::stop_token& stop = {});

}  // namespace world_explorer
