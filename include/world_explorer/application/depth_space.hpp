#pragma once

#include "world_explorer/core/result.hpp"
#include "world_explorer/scene_store/depth_space.hpp"

#include <filesystem>
#include <stop_token>

namespace world_explorer {

// Uses the existing pinned color/cache request. Fixed V1 recipe covers the full
// source bounds with at most 32 cells per axis; no solve or measured collision.
Result<void> inspect_depth_space(const std::filesystem::path& request,
                                 const std::string& expected_sha256,
                                 const std::filesystem::path& destination,
                                 const std::stop_token& stop = {});

struct DepthSpaceOpenRequest {
    std::filesystem::path m_source_bundle, m_directory;
    std::string m_source_manifest_sha256, m_report_sha256, m_request_sha256;
};

Result<std::shared_ptr<const DepthSpaceEvidence>>
open_depth_space(const DepthSpaceOpenRequest& request, const std::stop_token& stop = {});

}  // namespace world_explorer
