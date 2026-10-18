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

struct PoseAssetProvenance {
    std::string m_source_sha256, m_decode_completion_sha256, m_pose_completion_sha256;
    std::optional<std::string> m_journal_sha256;
};

// Immutable pose/calibration interchange, not a map-store recovery log. V1
// supports arbitrary monocular scale and unknown gravity only; neither is
// inferred from alignment. Does not contain image pixels or file paths.
struct CanonicalPoseAsset {
    MapSnapshot m_snapshot;
    PoseAssetProvenance m_provenance;
};

struct PoseAssetLimits {
    ByteBudget m_encoded_bytes { 256uz * 1024 * 1024 };
    MapStoreLimits m_records {};
};

Result<std::vector<std::byte>> encode_pose_asset(const CanonicalPoseAsset& asset,
                                                 PoseAssetLimits limits = {},
                                                 const std::stop_token& stop = {});
// Verify checksum on the same bytes subsequently parsed. All versions, enums,
// dependencies, counts and numeric conventions are checked before publication.
Result<CanonicalPoseAsset> decode_pose_asset(std::span<const std::byte> bytes,
                                             const std::string& expected_sha256,
                                             PoseAssetLimits limits = {},
                                             const std::stop_token& stop = {});
// Requires an existing destination parent. Publishes a closed/flushed immutable
// file without overwriting any existing entry; returns its SHA-256. Cancellation
// before publication leaves no destination. A failure after publication may
// leave a complete file (e.g. directory flush failure); caller can verify it.
Result<std::string> write_pose_asset(const std::filesystem::path& destination,
                                     const CanonicalPoseAsset& asset,
                                     PoseAssetLimits limits = {},
                                     const std::stop_token& stop = {});
Result<CanonicalPoseAsset> read_pose_asset(const std::filesystem::path& path,
                                           const std::string& expected_sha256,
                                           PoseAssetLimits limits = {},
                                           const std::stop_token& stop = {});
Result<std::string> inspect_pose_asset(const CanonicalPoseAsset& asset,
                                       PoseAssetLimits limits = {});

}  // namespace world_explorer
#endif
