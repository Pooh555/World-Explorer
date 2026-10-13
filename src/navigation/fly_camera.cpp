#include "world_explorer/navigation/fly_camera.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace world_explorer {
vec3d fly_camera::reference_right() const {
    const auto axis = std::abs(up_axis.x) < 0.9 ? vec3d{1, 0, 0} : vec3d{0, 0, 1};
    const auto projected = axis - up_axis * dot(axis, up_axis);
    return projected * (1 / std::sqrt(dot(projected, projected)));
}
vec3d fly_camera::reference_forward() const { return cross(up_axis, reference_right()); }
vec3d fly_camera::forward() const {
    return (reference_right() * std::sin(yaw) + reference_forward() * std::cos(yaw)) *
               std::cos(pitch) +
           up_axis * std::sin(pitch);
}
vec3d fly_camera::right() const {
    return reference_right() * std::cos(yaw) - reference_forward() * std::sin(yaw);
}
vec3d fly_camera::down() const { return cross(forward(), right()); }
void fly_camera::update(const fly_input& input, double elapsed_seconds) {
    if (!input.focused || !input.captured || input.text_input || !std::isfinite(elapsed_seconds) ||
        elapsed_seconds <= 0 || !std::isfinite(input.mouse_x) || !std::isfinite(input.mouse_y) ||
        !std::isfinite(input.wheel) || !std::isfinite(input.forward) ||
        !std::isfinite(input.right) || !std::isfinite(input.up))
        return;
    yaw = std::remainder(yaw + input.mouse_x * sensitivity, 2 * std::numbers::pi);
    pitch = std::clamp(pitch - input.mouse_y * sensitivity, -1.55334, 1.55334);
    speed = std::clamp(speed * std::exp(std::clamp(input.wheel, -10., 10.) * 0.15), 0.01, 10000.);
    auto direction = forward() * input.forward + right() * input.right + up_axis * input.up;
    const auto magnitude = std::sqrt(dot(direction, direction));
    if (magnitude > 1)
        direction = direction * (1 / magnitude);
    // Ignore most of a long application stall rather than launching through the scene.
    const auto delta = std::min(elapsed_seconds, 0.1) * speed * (input.accelerate ? 4 : 1);
    position = position + direction * delta;
}
void fly_camera::fit(const bounds3d& bounds) {
    if (!valid(bounds))
        return;
    const auto centre = (bounds.minimum + bounds.maximum) * 0.5;
    const auto diagonal = bounds.maximum - bounds.minimum;
    const auto extent = std::max(0.1, std::sqrt(dot(diagonal, diagonal)));
    position = centre - reference_forward() * extent;
    yaw = pitch = 0;
    speed = std::clamp(extent * 0.2, 0.01, 10000.);
}
result<fly_camera> restore_bookmark(const camera_bookmark& bookmark, scene_id scene,
                                    world_epoch epoch) {
    if (bookmark.scene != scene || bookmark.epoch != epoch)
        return fail(error_code::WRONG_EPOCH, "Bookmark belongs to another scene or world epoch");
    const auto& camera = bookmark.camera;
    if (!finite(camera.position) || !std::isfinite(camera.yaw) || !std::isfinite(camera.pitch) ||
        !std::isfinite(camera.speed) || camera.speed <= 0 || camera.speed > 10000 ||
        !finite(camera.up_axis) || std::abs(dot(camera.up_axis, camera.up_axis) - 1) > 1e-5)
        return fail(error_code::INVALID_DATA, "Bookmark contains an invalid camera");
    return camera;
}
} // namespace world_explorer
