#pragma once

#include "world_explorer/core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <stop_token>
#include <string>

namespace world_explorer {

struct VerifiedWorkerAsset {
    std::uint64_t m_bytes {};
    std::string m_sha256;
};

struct VerifiedWorkerStage {
    std::filesystem::path m_directory;
    std::string m_completion_sha256;
    std::map<std::string, VerifiedWorkerAsset> m_assets;
};

struct WorkerStageLimits {
    std::uint64_t m_maximum_bytes { 32ULL * 1024 * 1024 * 1024 };
    std::size_t m_maximum_files { 4096uz };
    std::size_t m_maximum_manifest_bytes { 16uz * 1024uz * 1024uz };
};

// Verify every listed file, exact inventory and unchanged completion. No symlinks
// or escaping paths. This does not authenticate the producer or execute workers.
Result<VerifiedWorkerStage> verify_committed_stage(const std::filesystem::path& directory,
                                                   const std::string& key,
                                                   const std::stop_token& stop = {},
                                                   WorkerStageLimits limits = {});

}  // namespace world_explorer
