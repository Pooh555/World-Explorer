#include "world_explorer/scene_store/color_projection.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace world_explorer {

Result<ColorProjectionView> ColorProjectionView::create(DepthImageAsset depth,
                                                        std::vector<std::byte> rgb,
                                                        SimilarityTransform world_from_camera,
                                                        double world_voxel_size,
                                                        const std::stop_token& stop) {
    const auto K_VALID = validate_depth_asset(depth, {}, stop);
    if (!K_VALID) return std::unexpected(K_VALID.error());
    const auto K_PIXELS = depth.m_valid.size();
    if (depth.m_metadata.m_width < 2 || depth.m_metadata.m_height < 2 ||
        K_PIXELS > std::numeric_limits<std::size_t>::max() / 3uz || rgb.size() != K_PIXELS * 3uz ||
        !valid(world_from_camera) || !std::isfinite(world_voxel_size) || world_voxel_size <= 0 ||
        !std::isfinite(2 * world_voxel_size / world_from_camera.m_scale))
        return fail(ErrorCode::INVALID_ARGUMENT, "Invalid color projection view");
    ColorProjectionView view;
    view.m_depth = std::move(depth);
    view.m_rgb = std::move(rgb);
    view.m_world_from_camera = world_from_camera;
    view.m_world_voxel_size = world_voxel_size;
    return view;
}

ColorProjection ColorProjectionView::project_color(Vec3d world_point, Vec3d world_normal) const {
    const auto K_REJECT = [](ColorProjectionStatus status) {
        return ColorProjection { .m_status = status };
    };
    const auto K_NORMAL_LENGTH = std::sqrt(dot(world_normal, world_normal));
    if (!finite(world_point) || !finite(world_normal) || !std::isfinite(K_NORMAL_LENGTH) ||
        K_NORMAL_LENGTH <= 0)
        return K_REJECT(ColorProjectionStatus::INVALID_SAMPLE);
    const auto K_CAMERA = inverse_transform_point(m_world_from_camera, world_point);
    if (!finite(K_CAMERA) || K_CAMERA.m_z <= 0)
        return K_REJECT(ColorProjectionStatus::BEHIND_CAMERA);
    const auto& metadata = m_depth.m_metadata;
    const auto K_PIXEL = project(metadata.m_camera, K_CAMERA);
    if (!K_PIXEL || !std::isfinite((*K_PIXEL)[0]) || !std::isfinite((*K_PIXEL)[1]) ||
        (*K_PIXEL)[0] < 0 || (*K_PIXEL)[1] < 0 || (*K_PIXEL)[0] >= metadata.m_width - 1.0 ||
        (*K_PIXEL)[1] >= metadata.m_height - 1.0)
        return K_REJECT(ColorProjectionStatus::OUTSIDE_IMAGE);
    const auto K_X = static_cast<std::size_t>(std::floor((*K_PIXEL)[0]));
    const auto K_Y = static_cast<std::size_t>(std::floor((*K_PIXEL)[1]));
    const std::size_t K_WIDTH { metadata.m_width };
    const std::array K_INDICES {K_Y * K_WIDTH + K_X,
                                K_Y * K_WIDTH + K_X + 1uz,
                                (K_Y + 1uz) * K_WIDTH + K_X,
                                (K_Y + 1uz) * K_WIDTH + K_X + 1uz};
    for (auto index : K_INDICES)
        if (!m_depth.m_valid[index]) return K_REJECT(ColorProjectionStatus::UNKNOWN_DEPTH);
    for (auto index : K_INDICES)
        if (m_depth.m_support_count[index] < 2 || m_depth.m_confidence[index] <= 0)
            return K_REJECT(ColorProjectionStatus::UNSUPPORTED_DEPTH);
    const auto K_RAY = world_point - m_world_from_camera.m_translation;
    const auto K_RAY_LENGTH = std::sqrt(dot(K_RAY, K_RAY));
    const auto K_COSINE = std::abs(dot(world_normal, K_RAY)) / (K_NORMAL_LENGTH * K_RAY_LENGTH);
    if (!std::isfinite(K_COSINE) || K_COSINE < .1) return K_REJECT(ColorProjectionStatus::GRAZING);
    // Fixed diagnostic envelope: two world voxels plus 1% axial depth. Require
    // agreement at every footprint corner; never interpolate through unknowns.
    const auto K_TOLERANCE =
        2 * m_world_voxel_size / m_world_from_camera.m_scale + .01 * K_CAMERA.m_z;
    if (!std::isfinite(K_TOLERANCE) || K_TOLERANCE <= 0)
        return K_REJECT(ColorProjectionStatus::INVALID_SAMPLE);
    double residual {};
    for (auto index : K_INDICES)
        residual = std::max(residual, std::abs(m_depth.m_axial_depth[index] - K_CAMERA.m_z));
    if (residual > K_TOLERANCE) return K_REJECT(ColorProjectionStatus::DEPTH_MISMATCH);
    const auto K_U = (*K_PIXEL)[0] - static_cast<double>(K_X);
    const auto K_V = (*K_PIXEL)[1] - static_cast<double>(K_Y);
    const std::array K_WEIGHTS { (1 - K_U) * (1 - K_V), K_U * (1 - K_V), (1 - K_U) * K_V, K_U * K_V };
    ColorProjection result { .m_status = ColorProjectionStatus::ACCEPTED };
    for (std::size_t channel { 0uz }; channel < 3uz; ++channel) {
        double color {};
        for (std::size_t corner { 0uz }; corner < 4uz; ++corner) {
            const auto K_SRGB =
                std::to_integer<unsigned char>(m_rgb[K_INDICES[corner] * 3uz + channel]) / 255.0;
            const auto K_LINEAR =
                K_SRGB <= .04045 ? K_SRGB / 12.92 : std::pow((K_SRGB + .055) / 1.055, 2.4);
            color += K_WEIGHTS[corner] * K_LINEAR;
        }
        result.m_linear_rgb[channel] = static_cast<float>(color);
    }
    result.m_score = std::clamp(K_COSINE, 0.0, 1.0) / (1 + residual / K_TOLERANCE);
    return result;
}

}  // namespace world_explorer
