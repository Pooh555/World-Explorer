#include "world_explorer/navigation/fly_camera.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace world_explorer {

Vec3d FlyCamera::reference_right() const {
    const auto K_AXIS = std::abs(m_up_axis.m_x) < 0.9 ? Vec3d { 1, 0, 0 } : Vec3d { 0, 0, 1 };
    const auto K_PROJECTED = K_AXIS - m_up_axis * dot(K_AXIS, m_up_axis);
    return K_PROJECTED * (1 / std::sqrt(dot(K_PROJECTED, K_PROJECTED)));
}

Vec3d FlyCamera::reference_forward() const {
    return cross(m_up_axis, reference_right());
}

Vec3d FlyCamera::forward() const {
    return (reference_right() * std::sin(m_yaw) + reference_forward() * std::cos(m_yaw)) *
               std::cos(m_pitch) +
           m_up_axis * std::sin(m_pitch);
}

Vec3d FlyCamera::right() const {
    return reference_right() * std::cos(m_yaw) - reference_forward() * std::sin(m_yaw);
}

Vec3d FlyCamera::down() const {
    return cross(forward(), right());
}

void FlyCamera::update(const FlyInput& input, double elapsed_seconds) {
    if (!input.m_focused || !input.m_captured || input.m_text_input ||
        !std::isfinite(elapsed_seconds) || elapsed_seconds <= 0 ||
        !std::isfinite(input.m_mouse_x) || !std::isfinite(input.m_mouse_y) ||
        !std::isfinite(input.m_wheel) || !std::isfinite(input.m_forward) ||
        !std::isfinite(input.m_right) || !std::isfinite(input.m_up))
        return;
    m_yaw = std::remainder(m_yaw + input.m_mouse_x * m_sensitivity, 2 * std::numbers::pi);
    m_pitch = std::clamp(m_pitch - input.m_mouse_y * m_sensitivity, -1.55334, 1.55334);
    m_speed =
        std::clamp(m_speed * std::exp(std::clamp(input.m_wheel, -10., 10.) * 0.15), 0.01, 10000.);
    auto direction = forward() * input.m_forward + right() * input.m_right + m_up_axis * input.m_up;
    const auto K_MAGNITUDE = std::sqrt(dot(direction, direction));
    if (K_MAGNITUDE > 1) direction = direction * (1 / K_MAGNITUDE);
    // Ignore most of a long application stall rather than launching through the scene.
    const auto K_DELTA = std::min(elapsed_seconds, 0.1) * m_speed * (input.m_accelerate ? 4 : 1);
    m_position = m_position + direction * K_DELTA;
}

void FlyCamera::fit(const Bounds3d& bounds) {
    if (!valid(bounds)) return;
    const auto K_CENTRE = (bounds.m_minimum + bounds.m_maximum) * 0.5;
    const auto K_DIAGONAL = bounds.m_maximum - bounds.m_minimum;
    const auto K_EXTENT = std::max(0.1, std::sqrt(dot(K_DIAGONAL, K_DIAGONAL)));
    m_position = K_CENTRE - reference_forward() * K_EXTENT;
    m_yaw = m_pitch = 0;
    m_speed = std::clamp(K_EXTENT * 0.2, 0.01, 10000.);
}

Result<FlyCamera>
restore_bookmark(const CameraBookmark& bookmark, SceneId scene, WorldEpoch epoch) {
    if (bookmark.m_scene != scene || bookmark.m_epoch != epoch)
        return fail(ErrorCode::WRONG_EPOCH, "Bookmark belongs to another scene or world epoch");
    const auto& camera = bookmark.m_camera;
    if (!finite(camera.m_position) || !std::isfinite(camera.m_yaw) ||
        !std::isfinite(camera.m_pitch) || !std::isfinite(camera.m_speed) || camera.m_speed <= 0 ||
        camera.m_speed > 10000 || !finite(camera.m_up_axis) ||
        std::abs(dot(camera.m_up_axis, camera.m_up_axis) - 1) > 1e-5)
        return fail(ErrorCode::INVALID_DATA, "Bookmark contains an invalid camera");
    return camera;
}

}  // namespace world_explorer
