#ifndef WORLD_EXPLORER_APPLICATION_VIDEO_DECODE_HPP
#define WORLD_EXPLORER_APPLICATION_VIDEO_DECODE_HPP
#include "world_explorer/application/video_preflight.hpp"
#include "world_explorer/reconstruction_adapters/decoded_frame_source.hpp"

namespace world_explorer {
struct retained_video_decode {
    std::filesystem::path stage;
    std::string completion_sha256;
};
struct automatic_video_decode {
    std::filesystem::path job_directory;
    worker_configuration worker;
    double sample_fps{3};
    std::uint32_t max_frames{120}, cpu_threads{4};
    std::uint64_t max_output_bytes{32ULL * 1024 * 1024 * 1024};
};
struct video_decode_request {
    std::filesystem::path video;
    retained_video_inspection inspection;
    automatic_video_decode configuration;
    std::vector<std::filesystem::path> outputs;
};
struct decoded_video {
    retained_video_decode selected;
    decoded_frame_catalog catalog;
};
// Explicit bounded timestamp selection, immutable requests and owned output.
// Worker capabilities gate START; final inventory/source/metadata are checked.
// No solver/optimizer runs. Compatible worker stages are cached on restart.
result<decoded_video> decode_video(const video_decode_request& request, const std::stop_token& stop,
                                   const log_callback& log = {},
                                   const worker_event_callback& observed = {});
result<decoded_video> adopt_video_decode(const retained_video_decode& selected,
                                         const std::string& source_sha256,
                                         const std::stop_token& stop);
} // namespace world_explorer
#endif
