#ifndef WORLD_EXPLORER_APPLICATION_VIDEO_PREFLIGHT_HPP
#define WORLD_EXPLORER_APPLICATION_VIDEO_PREFLIGHT_HPP

#include "world_explorer/application/commands.hpp"
#include <cstdint>

namespace world_explorer {
// Explicit read-only adoption of an immutable metadata inspection. The digest
// selects completion.json, not a mutable job pointer or a directory search.
struct retained_video_inspection {
    std::filesystem::path stage;
    std::string completion_sha256;
    // Current available space required at each output, not a disk reservation
    // or a guarantee of whole-job peak storage. Must be explicitly selected.
    std::uint64_t minimum_free_bytes{};
};
struct video_preflight_request {
    std::filesystem::path video;
    retained_video_inspection inspection;
    std::vector<std::filesystem::path> outputs;
};
struct video_preflight_report {
    std::string source_sha256;
    std::string inspection_completion_sha256;
    std::uint64_t source_bytes{};
    std::uint32_t width{}, height{};
    double duration_seconds{};
    std::int32_t rotation_degrees{};
    std::int64_t time_base_numerator{}, time_base_denominator{};
};
struct automatic_video_inspection {
    std::filesystem::path job_directory;
    worker_configuration worker;
    std::uint64_t minimum_free_bytes{};
};
struct video_inspection_request {
    std::filesystem::path video;
    automatic_video_inspection configuration;
    std::vector<std::filesystem::path> outputs;
};
struct inspected_video {
    retained_video_inspection inspection;
    video_preflight_report report;
};
// Creates a fresh owned metadata job or restarts the same source/worker identity.
// Capabilities gate START, verified completion selects a contained immutable
// inspection, and preflight checks it before return. No pose solve or RGB decode.
result<inspected_video> inspect_video(const video_inspection_request& request,
                                      const std::stop_token& stop, const log_callback& log = {},
                                      const worker_event_callback& observed = {});
// Bounded verification, no writes, pixel decoding or backend dependency. Checks
// source/metadata identity and current storage; does not certify codec decoding,
// frame selection quality, color conversion, geometry or reconstruction quality.
result<video_preflight_report> preflight_video(const video_preflight_request& request,
                                               const std::stop_token& stop,
                                               const log_callback& log = {});
} // namespace world_explorer
#endif
