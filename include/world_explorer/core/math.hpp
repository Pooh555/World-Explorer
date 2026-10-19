#pragma once

#include "world_explorer/core/result.hpp"

#include <array>
#include <cmath>

namespace world_explorer {

struct Vec3d {
    double m_x {}, m_y {}, m_z {};
    auto operator<=>(const Vec3d&) const = default;
};

inline Vec3d operator+(Vec3d a, Vec3d b) {
    return { a.m_x + b.m_x, a.m_y + b.m_y, a.m_z + b.m_z };
}

inline Vec3d operator-(Vec3d a, Vec3d b) {
    return { a.m_x - b.m_x, a.m_y - b.m_y, a.m_z - b.m_z };
}

inline Vec3d operator*(Vec3d a, double s) {
    return { a.m_x * s, a.m_y * s, a.m_z * s };
}

inline double dot(Vec3d a, Vec3d b) {
    return a.m_x * b.m_x + a.m_y * b.m_y + a.m_z * b.m_z;
}

inline Vec3d cross(Vec3d a, Vec3d b) {
    return { a.m_y * b.m_z - a.m_z * b.m_y,
            a.m_z * b.m_x - a.m_x * b.m_z,
            a.m_x * b.m_y - a.m_y * b.m_x };
}

inline bool finite(Vec3d value) {
    return std::isfinite(value.m_x) && std::isfinite(value.m_y) && std::isfinite(value.m_z);
}

struct Quatd {
    double m_w { 1 }, m_x {}, m_y {}, m_z {};
};

inline bool valid(Quatd q) {
    const double K_LENGTH = q.m_w * q.m_w + q.m_x * q.m_x + q.m_y * q.m_y + q.m_z * q.m_z;
    return std::isfinite(K_LENGTH) && std::abs(K_LENGTH - 1.0) < 1e-4;
}

inline Vec3d rotate(Quatd q, Vec3d v) {
    const Vec3d K_AXIS { q.m_x, q.m_y, q.m_z };
    const auto K_TWICE_CROSS = cross(K_AXIS, v) * 2;
    return v + K_TWICE_CROSS * q.m_w + cross(K_AXIS, K_TWICE_CROSS);
}

struct SimilarityTransform {
    Quatd m_rotation;
    Vec3d m_translation;
    double m_scale { 1 };
};

inline bool valid(const SimilarityTransform& transform) {
    return valid(transform.m_rotation) && finite(transform.m_translation) &&
           std::isfinite(transform.m_scale) && transform.m_scale > 0;
}

inline Vec3d transform_point(const SimilarityTransform& transform, Vec3d point) {
    return rotate(transform.m_rotation, point) * transform.m_scale + transform.m_translation;
}

inline Vec3d inverse_transform_point(const SimilarityTransform& transform, Vec3d point) {
    auto q = transform.m_rotation;
    return rotate({ q.m_w, -q.m_x, -q.m_y, -q.m_z },
                  (point - transform.m_translation) * (1 / transform.m_scale));
}

struct Bounds3d {
    Vec3d m_minimum, m_maximum;
};

inline bool valid(const Bounds3d& bounds) {
    return finite(bounds.m_minimum) && finite(bounds.m_maximum) &&
           bounds.m_minimum.m_x <= bounds.m_maximum.m_x &&
           bounds.m_minimum.m_y <= bounds.m_maximum.m_y &&
           bounds.m_minimum.m_z <= bounds.m_maximum.m_z;
}

struct PinholeCamera {
    double m_fx {}, m_fy {}, m_cx {}, m_cy {};
};

inline Result<std::array<double, 2>> project(const PinholeCamera& camera, Vec3d point) {
    if (!finite(point) || point.m_z <= 0 || !std::isfinite(camera.m_fx) ||
        !std::isfinite(camera.m_fy) || camera.m_fx <= 0 || camera.m_fy <= 0 ||
        !std::isfinite(camera.m_cx) || !std::isfinite(camera.m_cy)) {
        return fail(ErrorCode::INVALID_ARGUMENT, "invalid camera or point behind near plane");
    }
    return std::array { camera.m_fx * point.m_x / point.m_z + camera.m_cx,
                       camera.m_fy * point.m_y / point.m_z + camera.m_cy };
}

inline Result<Vec3d>
unproject(const PinholeCamera& camera, std::array<double, 2> pixel, double z_depth) {
    if (!project(camera, { 0, 0, z_depth }) || !std::isfinite(pixel[0]) || !std::isfinite(pixel[1])) {
        return fail(ErrorCode::INVALID_ARGUMENT, "invalid unprojection input");
    }
    return Vec3d { (pixel[0] - camera.m_cx) * z_depth / camera.m_fx,
                  (pixel[1] - camera.m_cy) * z_depth / camera.m_fy,
                  z_depth };
}

}  // namespace world_explorer
