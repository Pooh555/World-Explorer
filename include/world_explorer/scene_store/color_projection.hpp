#ifndef WORLD_EXPLORER_SCENE_STORE_COLOR_PROJECTION_HPP
#define WORLD_EXPLORER_SCENE_STORE_COLOR_PROJECTION_HPP

#include "world_explorer/scene_store/depth_asset.hpp"

namespace world_explorer {

enum class ColorProjectionStatus : std::uint8_t {
    ACCEPTED,
    INVALID_SAMPLE,
    BEHIND_CAMERA,
    OUTSIDE_IMAGE,
    UNKNOWN_DEPTH,
    UNSUPPORTED_DEPTH,
    GRAZING,
    DEPTH_MISMATCH,
    COUNT
};

struct ColorProjection {
    ColorProjectionStatus m_status { ColorProjectionStatus::INVALID_SAMPLE };
    std::array<float, 3> m_linear_rgb {};
    double m_score {};
};

// Owns checked depth/RGB. A full bilinear footprint must be valid, supported and
// depth-consistent. Color is captured sRGB decoded to linear light, never albedo.
class ColorProjectionView {
  public:
    static Result<ColorProjectionView> create(DepthImageAsset depth,
                                              std::vector<std::byte> rgb,
                                              SimilarityTransform world_from_camera,
                                              double world_voxel_size,
                                              const std::stop_token& stop = {});
    ColorProjection project_color(Vec3d world_point, Vec3d world_normal) const;

    const DepthAssetMetadata& metadata() const noexcept { return m_depth.m_metadata; }

  private:
    ColorProjectionView() = default;
    DepthImageAsset m_depth;
    std::vector<std::byte> m_rgb;
    SimilarityTransform m_world_from_camera;
    double m_world_voxel_size {};
};

}  // namespace world_explorer

#endif  // WORLD_EXPLORER_SCENE_STORE_COLOR_PROJECTION_HPP
