#include "world_explorer/render_core/camera.hpp"

#include <limits>

namespace world_explorer {

Result<std::array<float, 4>>
raster_intrinsics(const CameraView& camera, std::uint32_t width, std::uint32_t height) {
    const auto K_UNIT = [](Vec3d axis) {
        return finite(axis) && std::abs(dot(axis, axis) - 1) < 1e-4;
    };
    if (!width || !height || !finite(camera.m_position) || !K_UNIT(camera.m_right) ||
        !K_UNIT(camera.m_down) || !K_UNIT(camera.m_forward) ||
        std::abs(dot(camera.m_right, camera.m_down)) >= 1e-4 ||
        std::abs(dot(camera.m_right, camera.m_forward)) >= 1e-4 ||
        std::abs(dot(camera.m_down, camera.m_forward)) >= 1e-4 ||
        dot(cross(camera.m_right, camera.m_down), camera.m_forward) < 1 - 1e-4 ||
        !std::isfinite(camera.m_near_plane) || !std::isfinite(camera.m_far_plane) ||
        camera.m_near_plane <= 0 || camera.m_far_plane <= camera.m_near_plane) {
        return fail(ErrorCode::INVALID_ARGUMENT, "invalid render camera basis, extent or clipping");
    }
    PinholeCamera intrinsics;
    if (camera.m_calibration) {
        if (camera.m_calibration->m_width != width || camera.m_calibration->m_height != height) {
            return fail(ErrorCode::INVALID_ARGUMENT,
                        "calibration does not match framebuffer extent");
        }
        intrinsics = camera.m_calibration->m_intrinsics;
    } else {
        if (!std::isfinite(camera.m_vertical_fov_radians) ||
            camera.m_vertical_fov_radians <= 0.05 || camera.m_vertical_fov_radians >= 3.1) {
            return fail(ErrorCode::INVALID_ARGUMENT, "invalid spectator field of view");
        }
        const auto K_FOCAL = 0.5 * height / std::tan(camera.m_vertical_fov_radians * 0.5);
        intrinsics = { K_FOCAL, K_FOCAL, (width - 1) * 0.5, (height - 1) * 0.5 };
    }
    if (!project(intrinsics, { 0, 0, 1 })) {
        return fail(ErrorCode::INVALID_ARGUMENT, "invalid calibrated intrinsics");
    }
    const std::array<double, 4> K_VALUES {
        intrinsics.m_fx, intrinsics.m_fy, intrinsics.m_cx + 0.5, intrinsics.m_cy + 0.5 };
    std::array<float, 4> result {};
    for (std::size_t index { 0uz }; index < K_VALUES.size(); ++index) {
        if (std::abs(K_VALUES[index]) > std::numeric_limits<float>::max()) {
            return fail(ErrorCode::INVALID_ARGUMENT, "render intrinsics exceed float range");
        }
        result[index] = static_cast<float>(K_VALUES[index]);
    }
    if (result[0] <= 0 || result[1] <= 0) {
        return fail(ErrorCode::INVALID_ARGUMENT, "render focal length underflows float range");
    }
    return result;
}

}  // namespace world_explorer
