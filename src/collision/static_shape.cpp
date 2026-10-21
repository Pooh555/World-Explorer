#include "world_explorer/collision/static_shape.hpp"

#include "jolt_internal.hpp"

#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/SubShapeID.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace world_explorer {
namespace collision_detail {

class JoltRuntime {
  public:
    JoltRuntime() {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = &m_factory;
        JPH::RegisterTypes();
    }

    ~JoltRuntime() {
        JPH::UnregisterTypes();
        JPH::Factory::sInstance = nullptr;
    }

    JoltRuntime(const JoltRuntime&) = delete;
    JoltRuntime& operator=(const JoltRuntime&) = delete;

  private:
    JPH::Factory m_factory;
};

void ensure_jolt_runtime() {
    static JoltRuntime runtime;
}

Result<float> checked_float(double value, double tolerance) {
    if (!std::isfinite(value) || std::abs(value) > 1e6) {
        return fail(ErrorCode::INVALID_DATA, "Jolt coordinate exceeds bounded synthetic range");
    }
    const auto K_FLOAT = static_cast<float>(value);
    if (!std::isfinite(K_FLOAT) || std::abs(static_cast<double>(K_FLOAT) - value) > tolerance) {
        return fail(ErrorCode::INVALID_DATA, "Jolt float coordinate loses source precision");
    }
    return K_FLOAT;
}

Result<JPH::Vec3> checked_vec(Vec3d point, double tolerance) {
    const auto K_X = checked_float(point.m_x, tolerance);
    const auto K_Y = checked_float(point.m_y, tolerance);
    const auto K_Z = checked_float(point.m_z, tolerance);
    if (!K_X) {
        return std::unexpected(K_X.error());
    }
    if (!K_Y) {
        return std::unexpected(K_Y.error());
    }
    if (!K_Z) {
        return std::unexpected(K_Z.error());
    }
    return JPH::Vec3 { *K_X, *K_Y, *K_Z };
}

}  // namespace collision_detail

JoltStaticCollision::JoltStaticCollision(AdmissionToken) { }

JoltStaticCollision::~JoltStaticCollision() = default;

const std::string& JoltStaticCollision::canonical_digest() const {
    return m_cook->digest();
}

std::size_t JoltStaticCollision::triangle_count() const {
    return m_impl->m_triangle_count;
}

Result<void> JoltStaticCollision::validate_source(const SceneState& current,
                                                  const std::stop_token& stop) const {
    return m_cook->validate_source(current, stop);
}

Result<std::optional<double>> JoltStaticCollision::ray_cast(const SceneState& current,
                                                            Vec3d start,
                                                            Vec3d end,
                                                            const std::stop_token& stop) const {
    const auto K_VALID = validate_source(current, stop);
    if (!K_VALID) {
        return std::unexpected(K_VALID.error());
    }
    const auto K_START = collision_detail::checked_vec(start, m_impl->m_precision_tolerance);
    const auto K_END = collision_detail::checked_vec(end, m_impl->m_precision_tolerance);
    if (!K_START) {
        return std::unexpected(K_START.error());
    }
    if (!K_END) {
        return std::unexpected(K_END.error());
    }
    const auto K_DIRECTION = *K_END - *K_START;
    if (K_DIRECTION.LengthSq() <= 0) {
        return fail(ErrorCode::INVALID_ARGUMENT, "Jolt ray must have nonzero length");
    }
    if (stop.stop_requested()) {
        return fail(ErrorCode::CANCELLED, "Jolt ray cast cancelled");
    }
    JPH::RayCastResult hit;
    const auto K_LOCAL_START = *K_START - m_impl->m_shape->GetCenterOfMass();
    if (!m_impl->m_shape->CastRay({ K_LOCAL_START, K_DIRECTION }, {}, hit)) {
        return std::optional<double> {};
    }
    return std::optional<double> { static_cast<double>(hit.mFraction) };
}

Result<std::shared_ptr<const JoltStaticCollision>>
build_jolt_static_collision(std::shared_ptr<const CanonicalCollisionMesh> cook,
                            const SceneState& current,
                            const std::stop_token& stop) {
    if (!cook) {
        return fail(ErrorCode::INVALID_ARGUMENT, "Jolt shape requires canonical collision input");
    }
    const auto K_VALID = cook->validate_source(current, stop);
    if (!K_VALID) {
        return std::unexpected(K_VALID.error());
    }
    try {
        collision_detail::ensure_jolt_runtime();
        const double K_TOLERANCE { std::min(1e-4, cook->cell_size() * 1e-3) };
        JPH::TriangleList triangles;
        triangles.reserve(static_cast<std::uint32_t>(cook->triangles().size()));
        for (const auto& source : cook->triangles()) {
            if (stop.stop_requested()) {
                return fail(ErrorCode::CANCELLED, "Jolt shape build cancelled");
            }
            const auto K_A =
                collision_detail::checked_vec(source.m_world_vertices[0uz], K_TOLERANCE);
            const auto K_B =
                collision_detail::checked_vec(source.m_world_vertices[1uz], K_TOLERANCE);
            const auto K_C =
                collision_detail::checked_vec(source.m_world_vertices[2uz], K_TOLERANCE);
            if (!K_A) {
                return std::unexpected(K_A.error());
            }
            if (!K_B) {
                return std::unexpected(K_B.error());
            }
            if (!K_C) {
                return std::unexpected(K_C.error());
            }
            triangles.emplace_back(*K_A, *K_B, *K_C);
        }
        JPH::MeshShapeSettings settings { triangles };
        if (settings.mIndexedTriangles.size() != triangles.size()) {
            return fail(ErrorCode::INVALID_DATA, "Jolt sanitizer discarded selected triangles");
        }
        const auto K_SHAPE = settings.Create();
        if (K_SHAPE.HasError()) {
            const auto& message = K_SHAPE.GetError();
            return fail(ErrorCode::INVALID_DATA,
                        std::string("Jolt shape build failed: ") +
                            std::string(message.data(), message.size()));
        }
        if (!K_SHAPE.IsValid()) {
            return fail(ErrorCode::INVALID_DATA, "Jolt returned an empty shape result");
        }
        if (stop.stop_requested()) {
            return fail(ErrorCode::CANCELLED, "Jolt shape build cancelled");
        }
        auto owner = std::make_shared<JoltStaticCollision>(JoltStaticCollision::AdmissionToken {});
        owner->m_cook = std::move(cook);
        owner->m_impl = std::make_unique<JoltStaticCollision::Impl>();
        owner->m_impl->m_shape = K_SHAPE.Get();
        owner->m_impl->m_triangle_count = triangles.size();
        owner->m_impl->m_precision_tolerance = K_TOLERANCE;
        return std::shared_ptr<const JoltStaticCollision> { std::move(owner) };
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Jolt shape allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA,
                    std::string("Jolt shape build failed: ") + error.what());
    }
}

}  // namespace world_explorer
