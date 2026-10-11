#ifndef WORLD_EXPLORER_MAP_STORE_POSE_ASSET_HPP
#define WORLD_EXPLORER_MAP_STORE_POSE_ASSET_HPP
#include "world_explorer/map_store/map_store.hpp"
#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace world_explorer {
struct pose_asset_provenance {
    std::string source_sha256, decode_completion_sha256, pose_completion_sha256;
    std::optional<std::string> journal_sha256;
};
// Immutable pose/calibration interchange, not a map-store recovery log. V1
// supports arbitrary monocular scale and unknown gravity only; neither is
// inferred from alignment. Does not contain image pixels or file paths.
struct canonical_pose_asset {
    map_snapshot snapshot;
    pose_asset_provenance provenance;
};
struct pose_asset_limits {
    byte_budget encoded_bytes{std::size_t{256} * 1024 * 1024};
    map_store_limits records{};
};
result<std::vector<std::byte>> encode_pose_asset(const canonical_pose_asset& asset,
                                                 pose_asset_limits limits = {},
                                                 const std::stop_token& stop = {});
// Verify checksum on the same bytes subsequently parsed. All versions, enums,
// dependencies, counts and numeric conventions are checked before publication.
result<canonical_pose_asset> decode_pose_asset(std::span<const std::byte> bytes,
                                               const std::string& expected_sha256,
                                               pose_asset_limits limits = {},
                                               const std::stop_token& stop = {});
// Requires an existing destination parent. Publishes a closed/flushed immutable
// file without overwriting any existing entry; returns its SHA-256. Cancellation
// before publication leaves no destination. A failure after publication may
// leave a complete file (e.g. directory flush failure); caller can verify it.
result<std::string> write_pose_asset(const std::filesystem::path& destination,
                                     const canonical_pose_asset& asset,
                                     pose_asset_limits limits = {},
                                     const std::stop_token& stop = {});
result<canonical_pose_asset> read_pose_asset(const std::filesystem::path& path,
                                             const std::string& expected_sha256,
                                             pose_asset_limits limits = {},
                                             const std::stop_token& stop = {});
result<std::string> inspect_pose_asset(const canonical_pose_asset& asset,
                                       pose_asset_limits limits = {});
} // namespace world_explorer
#endif
