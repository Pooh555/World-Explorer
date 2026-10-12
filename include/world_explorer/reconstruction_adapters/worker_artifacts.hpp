#ifndef WORLD_EXPLORER_RECONSTRUCTION_ADAPTERS_WORKER_ARTIFACTS_HPP
#define WORLD_EXPLORER_RECONSTRUCTION_ADAPTERS_WORKER_ARTIFACTS_HPP
#include "world_explorer/core/result.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <stop_token>
#include <string>

namespace world_explorer {
struct verified_worker_asset {
    std::uint64_t bytes{};
    std::string sha256;
};
struct verified_worker_stage {
    std::filesystem::path directory;
    std::string completion_sha256;
    std::map<std::string, verified_worker_asset> assets;
};
struct worker_stage_limits {
    std::uint64_t maximum_bytes{32ULL * 1024 * 1024 * 1024};
    std::size_t maximum_files{4096};
    std::size_t maximum_manifest_bytes{std::size_t{16} * 1024 * 1024};
};
// Verify every listed file, exact inventory and unchanged completion. No symlinks
// or escaping paths. This does not authenticate the producer or execute workers.
result<verified_worker_stage> verify_committed_stage(const std::filesystem::path& directory,
                                                     const std::string& key,
                                                     const std::stop_token& stop = {},
                                                     worker_stage_limits limits = {});
} // namespace world_explorer
#endif
