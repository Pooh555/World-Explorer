#ifndef WORLD_EXPLORER_VIEWER_REQUEST_SUMMARY_HPP
#define WORLD_EXPLORER_VIEWER_REQUEST_SUMMARY_HPP
#include "world_explorer/core/result.hpp"

#include <filesystem>
#include <stop_token>
#include <string>
#include <vector>

namespace world_explorer::viewer {

struct request_summary_row {
    std::string label, value;
};

// At most 32 rows / 128 KiB text. No media, stage payloads or backend objects.
struct request_summary {
    std::filesystem::path request;
    std::string request_sha256;
    std::vector<request_summary_row> rows;
    std::vector<request_summary_row> advanced;
};

// Read-only; shared schema reader, checked retained metadata/source and current
// output storage. Automatic inspection remains pending; never a worker or decoder.
// Canonical path and exact parsed-byte digest bind the subsequent GUI launch.
Result<request_summary> review_reconstruction_request(const std::filesystem::path& path,
                                                      const std::stop_token& stop = {});

}  // namespace world_explorer::viewer
#endif
