#ifndef WORLD_EXPLORER_SCENE_STORE_DEPTH_ASSET_HPP
#define WORLD_EXPLORER_SCENE_STORE_DEPTH_ASSET_HPP

#include "world_explorer/core/math.hpp"
#include "world_explorer/core/types.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace world_explorer {

enum class DepthConvention : std::uint8_t { AXIAL_CAMERA_Z = 1, RAY_RANGE = 2 };
enum class DepthProducerKind : std::uint8_t { SYNTHETIC_DIAGNOSTIC = 1, MULTIVIEW_STEREO = 2 };

struct DepthDependency {
    WorldEpoch m_epoch;
    MapRevisionId m_pose_revision;
    FrameId m_frame;
    CalibrationId m_calibration;
    std::string m_pose_asset_sha256, m_observation_image_sha256;
    bool operator==(const DepthDependency&) const = default;
};

struct DepthAssetMetadata {
    DepthDependency m_dependency;
    std::uint32_t m_width {}, m_height {};
    // Undistorted depth grid, integer pixel centers, optical +X right/+Y down/+Z forward.
    PinholeCamera m_camera;
    std::string m_calibrated_image_sha256, m_recipe_sha256;
    DepthConvention m_convention { DepthConvention::AXIAL_CAMERA_Z };
    DepthProducerKind m_producer { DepthProducerKind::SYNTHETIC_DIAGNOSTIC };
    // V1 uses the camera units of the bound pose asset. Its world-from-camera
    // similarity supplies camera-to-world scale; measured provenance is not yet supported.
    ScaleStatus m_scale { ScaleStatus::ARBITRARY };
};

struct DepthImageAsset {
    DepthAssetMetadata m_metadata;
    // Row-major planes. Validity is explicit 0/1; invalid entries are finite +0
    // in every plane. Confidence is recipe-defined weight in [0,1], not probability.
    std::vector<float> m_axial_depth;
    std::vector<std::uint8_t> m_valid;
    std::vector<float> m_confidence;
    std::vector<std::uint32_t> m_support_count;
    // Absent means unavailable, never zero uncertainty. If present, same camera
    // units as axial_depth. Support counts are actual observations, not confidence.
    std::optional<std::vector<float>> m_standard_deviation;
};

struct DepthAssetLimits {
    std::uint32_t m_max_dimension { 4096 };
    std::size_t m_max_pixels { 4096uz * 4096uz };
    ByteBudget m_encoded_bytes { 256uz * 1024 * 1024 };
    ByteBudget m_payload_bytes { 256uz * 1024 * 1024 };
    std::uint32_t m_max_support_count { 1000000 };
};

// Validates format/numerics, not reconstruction quality or collision eligibility.
Result<std::size_t> validate_depth_asset(const DepthImageAsset& asset,
                                         DepthAssetLimits limits = {},
                                         const std::stop_token& stop = {});
Result<void> admit_depth_asset(const DepthImageAsset& asset,
                               const DepthAssetMetadata& expected,
                               DepthAssetLimits limits = {},
                               const std::stop_token& stop = {});
// V1 is an uncompressed little-endian interchange file. No inferred ray-range
// conversion, metric scale, normal estimates, compression or walking readiness.
Result<std::vector<std::byte>> encode_depth_asset(const DepthImageAsset& asset,
                                                  DepthAssetLimits limits = {},
                                                  const std::stop_token& stop = {});
Result<DepthImageAsset> decode_depth_asset(std::span<const std::byte> bytes,
                                           const std::string& expected_sha256,
                                           DepthAssetLimits limits = {},
                                           const std::stop_token& stop = {});
// Closed/flushed, exclusive immutable publication. A directory-flush error may
// leave the published file present; callers must inspect/retry with its digest.
Result<std::string> write_depth_asset(const std::filesystem::path& path,
                                      const DepthImageAsset& asset,
                                      DepthAssetLimits limits = {},
                                      const std::stop_token& stop = {});
// Checks the checksum and dependency on the parsed bytes before returning data.
Result<DepthImageAsset> read_depth_asset(const std::filesystem::path& path,
                                         const std::string& expected_sha256,
                                         const DepthAssetMetadata& expected,
                                         DepthAssetLimits limits = {},
                                         const std::stop_token& stop = {});

}  // namespace world_explorer
#endif
