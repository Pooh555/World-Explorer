#pragma once

#include "world_explorer/application/commands.hpp"

#include <cstdint>

namespace world_explorer {

// Explicit read-only adoption of an immutable metadata inspection. The digest
// selects completion.json, not a mutable job pointer or a directory search.
struct RetainedVideoInspection {
    std::filesystem::path m_stage;
    std::string m_completion_sha256;
    // Current available space required at each output, not a disk reservation
    // or a guarantee of whole-job peak storage. Must be explicitly selected.
    std::uint64_t m_minimum_free_bytes {};
};

struct VideoPreflightRequest {
    std::filesystem::path m_video;
    RetainedVideoInspection m_inspection;
    std::vector<std::filesystem::path> m_outputs;
};

struct OutputStorageReport {
    std::filesystem::path m_output;
    std::filesystem::path m_existing_directory;
    std::uintmax_t m_available_bytes {};
};

// Read-only snapshot for at most four destinations, using their nearest existing
// directory. Unknown/insufficient space fails; no reservation or aggregate peak estimate.
Result<std::vector<OutputStorageReport>>
inspect_output_storage(const std::vector<std::filesystem::path>& outputs,
                       std::uint64_t minimum_free_bytes,
                       const std::stop_token& stop = {});

struct VideoPreflightReport {
    std::string m_source_sha256;
    std::string m_inspection_completion_sha256;
    std::uint64_t m_source_bytes {};
    std::uint32_t m_width {}, m_height {};
    double m_duration_seconds {};
    std::int32_t m_rotation_degrees {};
    std::int64_t m_time_base_numerator {}, m_time_base_denominator {};
    std::string m_codec_name;
    std::vector<OutputStorageReport> m_storage;
};

struct AutomaticVideoInspection {
    std::filesystem::path m_job_directory;
    WorkerConfiguration m_worker;
    std::uint64_t m_minimum_free_bytes {};
};

struct VideoInspectionRequest {
    std::filesystem::path m_video;
    AutomaticVideoInspection m_configuration;
    std::vector<std::filesystem::path> m_outputs;
};

struct InspectedVideo {
    RetainedVideoInspection m_inspection;
    VideoPreflightReport m_report;
};

// Creates a fresh owned metadata job or restarts the same source/worker identity.
// Capabilities gate START, verified completion selects a contained immutable
// inspection, and preflight checks it before return. No pose solve or RGB decode.
Result<InspectedVideo> inspect_video(const VideoInspectionRequest& request,
                                     const std::stop_token& stop,
                                     const LogCallback& log = {},
                                     const WorkerEventCallback& observed = {});
// Bounded verification, no writes, pixel decoding or backend dependency. Checks
// source/metadata identity and current storage; does not certify codec decoding,
// frame selection quality, color conversion, geometry or reconstruction quality.
Result<VideoPreflightReport> preflight_video(const VideoPreflightRequest& request,
                                             const std::stop_token& stop,
                                             const LogCallback& log = {});

}  // namespace world_explorer
