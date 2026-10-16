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
enum class depth_convention : std::uint8_t { AXIAL_CAMERA_Z = 1, RAY_RANGE = 2 };
enum class depth_producer_kind : std::uint8_t { SYNTHETIC_DIAGNOSTIC = 1, MULTIVIEW_STEREO = 2 };
struct depth_dependency {
    world_epoch epoch;
    map_revision_id pose_revision;
    frame_id frame;
    calibration_id calibration;
    std::string pose_asset_sha256, observation_image_sha256;
    bool operator==(const depth_dependency&) const = default;
};
struct depth_asset_metadata {
    depth_dependency dependency;
    std::uint32_t width{}, height{};
    // Undistorted depth grid, integer pixel centers, optical +X right/+Y down/+Z forward.
    pinhole_camera camera;
    std::string calibrated_image_sha256, recipe_sha256;
    depth_convention convention{depth_convention::AXIAL_CAMERA_Z};
    depth_producer_kind producer{depth_producer_kind::SYNTHETIC_DIAGNOSTIC};
    // V1 uses the camera units of the bound pose asset. Its world-from-camera
    // similarity supplies camera-to-world scale; measured provenance is not yet supported.
    scale_status scale{scale_status::ARBITRARY};
};
struct depth_image_asset {
    depth_asset_metadata metadata;
    // Row-major planes. Validity is explicit 0/1; invalid entries are finite +0
    // in every plane. Confidence is recipe-defined weight in [0,1], not probability.
    std::vector<float> axial_depth;
    std::vector<std::uint8_t> valid;
    std::vector<float> confidence;
    std::vector<std::uint32_t> support_count;
    // Absent means unavailable, never zero uncertainty. If present, same camera
    // units as axial_depth. Support counts are actual observations, not confidence.
    std::optional<std::vector<float>> standard_deviation;
};
struct depth_asset_limits {
    std::uint32_t max_dimension{4096};
    std::size_t max_pixels{std::size_t{4096} * 4096};
    byte_budget encoded_bytes{std::size_t{256} * 1024 * 1024};
    byte_budget payload_bytes{std::size_t{256} * 1024 * 1024};
    std::uint32_t max_support_count{1000000};
};
// Validates format/numerics, not reconstruction quality or collision eligibility.
result<std::size_t> validate_depth_asset(const depth_image_asset& asset,
                                         depth_asset_limits limits = {},
                                         const std::stop_token& stop = {});
result<void> admit_depth_asset(const depth_image_asset& asset, const depth_asset_metadata& expected,
                               depth_asset_limits limits = {}, const std::stop_token& stop = {});
// V1 is an uncompressed little-endian interchange file. No inferred ray-range
// conversion, metric scale, normal estimates, compression or walking readiness.
result<std::vector<std::byte>> encode_depth_asset(const depth_image_asset& asset,
                                                  depth_asset_limits limits = {},
                                                  const std::stop_token& stop = {});
result<depth_image_asset> decode_depth_asset(std::span<const std::byte> bytes,
                                             const std::string& expected_sha256,
                                             depth_asset_limits limits = {},
                                             const std::stop_token& stop = {});
// Closed/flushed, exclusive immutable publication. A directory-flush error may
// leave the published file present; callers must inspect/retry with its digest.
result<std::string> write_depth_asset(const std::filesystem::path& path,
                                      const depth_image_asset& asset,
                                      depth_asset_limits limits = {},
                                      const std::stop_token& stop = {});
// Checks the checksum and dependency on the parsed bytes before returning data.
result<depth_image_asset> read_depth_asset(const std::filesystem::path& path,
                                           const std::string& expected_sha256,
                                           const depth_asset_metadata& expected,
                                           depth_asset_limits limits = {},
                                           const std::stop_token& stop = {});
} // namespace world_explorer
#endif
