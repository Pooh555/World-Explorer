#pragma once

#include "world_explorer/application/video_preflight.hpp"
#include "world_explorer/reconstruction_adapters/decoded_frame_source.hpp"

namespace world_explorer {

struct RetainedVideoDecode {
    std::filesystem::path m_stage;
    std::string m_completion_sha256;
};

enum class FrameSelection : std::uint8_t {
    TIMESTAMP_SAMPLING_V1,
    QUALITY_KEYFRAMES_CANDIDATE_V1,
    QUALITY_KEYFRAMES_CANDIDATE_V2
};

struct AutomaticVideoDecode {
    std::filesystem::path m_job_directory;
    WorkerConfiguration m_worker;
    double m_sample_fps { 3 };
    std::uint32_t m_max_frames { 120 }, m_cpu_threads { 4 };
    std::uint64_t m_max_output_bytes { 32ULL * 1024 * 1024 * 1024 };
    FrameSelection m_frame_selection { FrameSelection::TIMESTAMP_SAMPLING_V1 };
};

struct VideoDecodeRequest {
    std::filesystem::path m_video;
    RetainedVideoInspection m_inspection;
    AutomaticVideoDecode m_configuration;
    std::vector<std::filesystem::path> m_outputs;
};

struct DecodedVideo {
    RetainedVideoDecode m_selected;
    DecodedFrameCatalog m_catalog;
};

// Explicit bounded selection, immutable requests and owned output. Candidate
// quality thresholds are unqualified and never selected implicitly.
// Worker capabilities gate START; final inventory/source/metadata are checked.
// No solver/optimizer runs. Compatible worker stages are cached on restart.
Result<DecodedVideo> decode_video(const VideoDecodeRequest& request,
                                  const std::stop_token& stop,
                                  const LogCallback& log = {},
                                  const WorkerEventCallback& observed = {});
Result<DecodedVideo> adopt_video_decode(const RetainedVideoDecode& selected,
                                        const std::string& source_sha256,
                                        const std::stop_token& stop);

}  // namespace world_explorer
