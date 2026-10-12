#include "world_explorer/render_core/camera.hpp"
#include <limits>

namespace world_explorer {
result<std::array<float, 4>> raster_intrinsics(const camera_view& camera, std::uint32_t width,
                                               std::uint32_t height) {
    const auto unit = [](vec3d axis) {
        return finite(axis) && std::abs(dot(axis, axis) - 1) < 1e-4;
    };
    if (!width || !height || !finite(camera.position) || !unit(camera.right) ||
        !unit(camera.down) || !unit(camera.forward) ||
        std::abs(dot(camera.right, camera.down)) >= 1e-4 ||
        std::abs(dot(camera.right, camera.forward)) >= 1e-4 ||
        std::abs(dot(camera.down, camera.forward)) >= 1e-4 ||
        dot(cross(camera.right, camera.down), camera.forward) < 1 - 1e-4 ||
        !std::isfinite(camera.near_plane) || !std::isfinite(camera.far_plane) ||
        camera.near_plane <= 0 || camera.far_plane <= camera.near_plane)
        return fail(error_code::INVALID_ARGUMENT,
                    "invalid render camera basis, extent or clipping");
    pinhole_camera intrinsics;
    if (camera.calibration) {
        if (camera.calibration->width != width || camera.calibration->height != height)
            return fail(error_code::INVALID_ARGUMENT,
                        "calibration does not match framebuffer extent");
        intrinsics = camera.calibration->intrinsics;
    } else {
        if (!std::isfinite(camera.vertical_fov_radians) || camera.vertical_fov_radians <= 0.05 ||
            camera.vertical_fov_radians >= 3.1)
            return fail(error_code::INVALID_ARGUMENT, "invalid spectator field of view");
        const auto focal = 0.5 * height / std::tan(camera.vertical_fov_radians * 0.5);
        intrinsics = {focal, focal, (width - 1) * 0.5, (height - 1) * 0.5};
    }
    if (!project(intrinsics, {0, 0, 1}))
        return fail(error_code::INVALID_ARGUMENT, "invalid calibrated intrinsics");
    const std::array<double, 4> values{intrinsics.fx, intrinsics.fy, intrinsics.cx + 0.5,
                                       intrinsics.cy + 0.5};
    std::array<float, 4> result{};
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (std::abs(values[index]) > std::numeric_limits<float>::max())
            return fail(error_code::INVALID_ARGUMENT, "render intrinsics exceed float range");
        result[index] = static_cast<float>(values[index]);
    }
    if (result[0] <= 0 || result[1] <= 0)
        return fail(error_code::INVALID_ARGUMENT, "render focal length underflows float range");
    return result;
}
} // namespace world_explorer
