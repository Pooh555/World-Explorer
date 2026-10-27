#pragma once

#include "world_explorer/collision/static_shape.hpp"

// Jolt's public math headers use std::is_trivial without including this header.
#include <type_traits>

// Jolt requires this before every other Jolt header.
// clang-format off
#include <Jolt/Jolt.h>
// clang-format on

#include <Jolt/Physics/Collision/Shape/Shape.h>

namespace world_explorer {

struct JoltStaticCollision::Impl {
    JPH::ShapeRefC m_shape;
    std::size_t m_triangle_count {};
    double m_precision_tolerance {};
};

namespace collision_detail {

void ensure_jolt_runtime();
Result<JPH::Vec3> checked_vec(Vec3d point, double tolerance);

}  // namespace collision_detail

}  // namespace world_explorer
