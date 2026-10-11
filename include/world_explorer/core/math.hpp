#ifndef WORLD_EXPLORER_CORE_MATH_HPP
#define WORLD_EXPLORER_CORE_MATH_HPP

#include "world_explorer/core/result.hpp"
#include <array>
#include <cmath>

namespace world_explorer {
struct vec3d {
    double x{}, y{}, z{};
    auto operator<=>(const vec3d&) const = default;
};
inline vec3d operator+(vec3d a, vec3d b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline vec3d operator-(vec3d a, vec3d b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline vec3d operator*(vec3d a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline double dot(vec3d a, vec3d b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline vec3d cross(vec3d a, vec3d b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline bool finite(vec3d value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
struct quatd {
    double w{1}, x{}, y{}, z{};
};
inline bool valid(quatd q) {
    const double length = q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z;
    return std::isfinite(length) && std::abs(length - 1.0) < 1e-4;
}
inline vec3d rotate(quatd q, vec3d v) {
    const vec3d axis{q.x, q.y, q.z};
    const auto twice_cross = cross(axis, v) * 2;
    return v + twice_cross * q.w + cross(axis, twice_cross);
}
struct similarity_transform {
    quatd rotation;
    vec3d translation;
    double scale{1};
};
inline bool valid(const similarity_transform& transform) {
    return valid(transform.rotation) && finite(transform.translation) &&
           std::isfinite(transform.scale) && transform.scale > 0;
}
inline vec3d transform_point(const similarity_transform& transform, vec3d point) {
    return rotate(transform.rotation, point) * transform.scale + transform.translation;
}
inline vec3d inverse_transform_point(const similarity_transform& transform, vec3d point) {
    auto q = transform.rotation;
    return rotate({q.w, -q.x, -q.y, -q.z}, (point - transform.translation) * (1 / transform.scale));
}
struct bounds3d {
    vec3d minimum, maximum;
};
inline bool valid(const bounds3d& bounds) {
    return finite(bounds.minimum) && finite(bounds.maximum) &&
           bounds.minimum.x <= bounds.maximum.x && bounds.minimum.y <= bounds.maximum.y &&
           bounds.minimum.z <= bounds.maximum.z;
}
struct pinhole_camera {
    double fx{}, fy{}, cx{}, cy{};
};
inline result<std::array<double, 2>> project(const pinhole_camera& camera, vec3d point) {
    if (!finite(point) || point.z <= 0 || !std::isfinite(camera.fx) || !std::isfinite(camera.fy) ||
        camera.fx <= 0 || camera.fy <= 0 || !std::isfinite(camera.cx) || !std::isfinite(camera.cy))
        return fail(error_code::INVALID_ARGUMENT, "invalid camera or point behind near plane");
    return std::array{camera.fx * point.x / point.z + camera.cx,
                      camera.fy * point.y / point.z + camera.cy};
}
inline result<vec3d> unproject(const pinhole_camera& camera, std::array<double, 2> pixel,
                               double z_depth) {
    if (!project(camera, {0, 0, z_depth}) || !std::isfinite(pixel[0]) || !std::isfinite(pixel[1]))
        return fail(error_code::INVALID_ARGUMENT, "invalid unprojection input");
    return vec3d{(pixel[0] - camera.cx) * z_depth / camera.fx,
                 (pixel[1] - camera.cy) * z_depth / camera.fy, z_depth};
}
} // namespace world_explorer

#endif // WORLD_EXPLORER_CORE_MATH_HPP
