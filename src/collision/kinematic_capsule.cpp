#include "world_explorer/collision/kinematic_capsule.hpp"

#include "jolt_internal.hpp"

#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace world_explorer {
namespace {

constexpr std::uint64_t K_CLOCK_HZ { 120 };
constexpr std::uint64_t K_TICK_UNITS { 1000000000 };
constexpr std::uint64_t K_MAX_STEPS { 8 };
constexpr float K_STEP_SECONDS { 1.F / 120.F };
constexpr float K_PADDING { 0.005F };

class StaticLayer final : public JPH::BroadPhaseLayerInterface {
  public:
    JPH::uint GetNumBroadPhaseLayers() const override {
        return 1;
    }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer) const override {
        return JPH::BroadPhaseLayer { 0 };
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer) const override {
        return "static";
    }
#endif
};

class StaticBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
  public:
    bool ShouldCollide(JPH::ObjectLayer, JPH::BroadPhaseLayer) const override {
        return true;
    }
};

class StaticPairFilter final : public JPH::ObjectLayerPairFilter {
  public:
    bool ShouldCollide(JPH::ObjectLayer, JPH::ObjectLayer) const override {
        return false;
    }
};

}  // namespace

struct SyntheticCapsuleController::Impl {
    StaticLayer m_layers;
    StaticBroadPhaseFilter m_broad_phase_filter;
    StaticPairFilter m_pair_filter;
    JPH::PhysicsSystem m_world;
    JPH::CharacterVirtualSettings m_character_settings;
    JPH::TempAllocatorMalloc m_allocator;
};

SyntheticCapsuleController::SyntheticCapsuleController(AdmissionToken) { }

SyntheticCapsuleController::~SyntheticCapsuleController() = default;

SyntheticCapsuleState SyntheticCapsuleController::state() const {
    return m_state;
}

Result<std::unique_ptr<SyntheticCapsuleController>>
SyntheticCapsuleController::create(std::shared_ptr<const JoltStaticCollision> shape,
                                   const SceneState& current,
                                   SyntheticCapsuleSettings settings,
                                   Vec3d feet,
                                   const std::stop_token& stop) {
    if (!shape || !std::isfinite(settings.m_radius) || !std::isfinite(settings.m_height) ||
        settings.m_radius < 0.05 || settings.m_radius > 1 ||
        settings.m_height < 2 * settings.m_radius + 0.01 || settings.m_height > 3) {
        return fail(ErrorCode::INVALID_ARGUMENT, "Invalid synthetic capsule dimensions or shape");
    }
    const auto K_VALID = shape->validate_source(current, stop);
    if (!K_VALID) {
        return std::unexpected(K_VALID.error());
    }
    if (shape->m_cook->parameters().m_format_version == 4U) {
        return fail(ErrorCode::WOULD_BLOCK,
                    "Synthetic step controller motion has not been certified");
    }
    const auto K_FEET = collision_detail::checked_vec(feet, shape->m_impl->m_precision_tolerance);
    if (!K_FEET) {
        return std::unexpected(K_FEET.error());
    }
    const auto K_CLEAR =
        shape->m_cook->probe(current, { feet, feet, settings.m_radius, settings.m_height }, stop);
    if (!K_CLEAR) {
        return std::unexpected(K_CLEAR.error());
    }
    if (K_CLEAR->m_state != ClearanceState::CLEAR_DIAGNOSTIC) {
        return fail(ErrorCode::WOULD_BLOCK, "Synthetic capsule spawn lacks clear supported space");
    }
    try {
        auto controller = std::make_unique<SyntheticCapsuleController>(AdmissionToken {});
        controller->m_shape = std::move(shape);
        controller->m_settings = settings;
        controller->m_state.m_feet = feet;
        controller->m_impl = std::make_unique<Impl>();
        auto& impl = *controller->m_impl;
        // Jolt's contact hash tables require at least four buckets, even with
        // only one static body and no rigid-body simulation.
        impl.m_world.Init(1, 0, 4, 4, impl.m_layers, impl.m_broad_phase_filter, impl.m_pair_filter);
        const JPH::BodyCreationSettings K_BODY { controller->m_shape->m_impl->m_shape,
                                                JPH::RVec3::sZero(),
                                                JPH::Quat::sIdentity(),
                                                JPH::EMotionType::Static,
                                                0 };
        const auto K_BODY_ID = impl.m_world.GetBodyInterface().CreateAndAddBody(
            K_BODY, JPH::EActivation::DontActivate);
        if (K_BODY_ID.IsInvalid()) {
            return fail(ErrorCode::RESOURCE_EXHAUSTED,
                        "Synthetic collision body allocation failed");
        }
        auto& character = impl.m_character_settings;
        character.mShape =
            new JPH::CapsuleShape(static_cast<float>(settings.m_height / 2 - settings.m_radius),
                                  static_cast<float>(settings.m_radius));
        character.mShapeOffset = { 0, static_cast<float>(settings.m_height / 2), 0 };
        character.mMaxSlopeAngle = JPH::DegreesToRadians(45.F);
        character.mSupportingVolume = { JPH::Vec3::sAxisY(), -static_cast<float>(settings.m_radius) };
        character.mCharacterPadding = K_PADDING;
        character.mBackFaceMode = JPH::EBackFaceMode::IgnoreBackFaces;
        if (stop.stop_requested()) {
            return fail(ErrorCode::CANCELLED, "Synthetic capsule creation cancelled");
        }
        return controller;
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Synthetic capsule allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::BACKEND_FAILURE,
                    std::string("Jolt capsule creation: ") + error.what());
    }
}

Result<SyntheticCapsuleAdvance>
SyntheticCapsuleController::advance(const SceneState& current,
                                    Vec3d horizontal_velocity,
                                    std::chrono::nanoseconds elapsed,
                                    const std::stop_token& stop) {
    const auto K_VALID = m_shape->validate_source(current, stop);
    if (!K_VALID) {
        return std::unexpected(K_VALID.error());
    }
    if (elapsed < std::chrono::nanoseconds::zero() || elapsed > std::chrono::seconds(60) ||
        !std::isfinite(horizontal_velocity.m_x) || !std::isfinite(horizontal_velocity.m_z) ||
        horizontal_velocity.m_y != 0 ||
        std::hypot(horizontal_velocity.m_x, horizontal_velocity.m_z) > 10) {
        return fail(ErrorCode::INVALID_ARGUMENT,
                    "Invalid synthetic capsule velocity or elapsed time");
    }
    const auto K_VELOCITY = collision_detail::checked_vec(horizontal_velocity, 1e-5);
    if (!K_VELOCITY) {
        return std::unexpected(K_VELOCITY.error());
    }
    const auto K_UNITS = m_clock_units + static_cast<std::uint64_t>(elapsed.count()) * K_CLOCK_HZ;
    const auto K_TICKS = K_UNITS / K_TICK_UNITS;
    const auto K_STEPS = std::min(K_MAX_STEPS, K_TICKS);
    if (K_STEPS > std::numeric_limits<std::uint64_t>::max() - m_state.m_simulated_ticks) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Synthetic capsule tick counter exhausted");
    }
    SyntheticCapsuleAdvance result {
        m_state, static_cast<std::uint32_t>(K_STEPS), K_TICKS - K_STEPS };
    try {
        // All Jolt state is speculative. Only the checked project state below is
        // committed, so an error or cancellation cannot leak a partial advance.
        const auto K_START =
            collision_detail::checked_vec(m_state.m_feet, m_shape->m_impl->m_precision_tolerance);
        if (!K_START) {
            return std::unexpected(K_START.error());
        }
        JPH::CharacterVirtual character { &m_impl->m_character_settings,
                                         JPH::RVec3(*K_START),
                                         JPH::Quat::sIdentity(),
                                         &m_impl->m_world };
        const JPH::BroadPhaseLayerFilter K_BROAD_PHASE;
        const JPH::ObjectLayerFilter K_LAYER;
        character.RefreshContacts(K_BROAD_PHASE, K_LAYER, {}, {}, m_impl->m_allocator);
        JPH::CharacterVirtual::ExtendedUpdateSettings update;
        update.mWalkStairsStepUp = { 0, 0.30F, 0 };
        update.mStickToFloorStepDown = { 0, -0.30F, 0 };
        for (std::uint64_t tick {}; tick < K_STEPS; ++tick) {
            if (stop.stop_requested()) {
                return fail(ErrorCode::CANCELLED, "Synthetic capsule advance cancelled");
            }
            const bool K_RAMP { m_shape->m_cook->parameters().m_format_version == 3U };
            const auto K_GRADIENT = m_shape->m_cook->floor_gradient();
            const float K_VERTICAL {
                K_RAMP ? static_cast<float>(dot(K_GRADIENT, horizontal_velocity)) : 0.F };
            // V3 is grounded on one admitted plane. Its downward support sweep
            // replaces free-fall integration, avoiding idle downslope creep.
            const float K_GRAVITY_STEP { K_RAMP ? 0.F : 9.81F * K_STEP_SECONDS };
            character.SetLinearVelocity(*K_VELOCITY + JPH::Vec3(0, K_VERTICAL - K_GRAVITY_STEP, 0));
            character.ExtendedUpdate(K_STEP_SECONDS,
                                     { 0, -9.81F, 0 },
                                     update,
                                     K_BROAD_PHASE,
                                     K_LAYER,
                                     {},
                                     {},
                                     m_impl->m_allocator);
            const bool K_UPDATE_OVERFLOW { character.GetMaxHitsExceeded() };
            if (K_RAMP) {
                // A wall can remove horizontal travel while tangent input still
                // requests upward travel. Sweep back to supported geometry before
                // certifying the final segment; never enlarge contact tolerance.
                character.StickToFloor(update.mStickToFloorStepDown,
                                       K_BROAD_PHASE,
                                       K_LAYER,
                                       {},
                                       {},
                                       m_impl->m_allocator);
            }
            if (K_UPDATE_OVERFLOW || character.GetMaxHitsExceeded()) {
                return fail(ErrorCode::RESOURCE_EXHAUSTED, "Jolt capsule contact budget exhausted");
            }
            const auto K_POSITION = character.GetPosition();
            Vec3d candidate { K_POSITION.GetX(), m_state.m_feet.m_y, K_POSITION.GetZ() };
            if (K_RAMP) {
                // Normalize only bounded contact rounding to the admitted plane.
                // The physical capsule's lowest point rests above an incline;
                // Jolt adds padding internally in both its up offset and sweep.
                const double K_EXTRA { (m_settings.m_radius + K_PADDING) *
                                      (std::sqrt(1 + dot(K_GRADIENT, K_GRADIENT)) - 1) };
                candidate.m_y = m_shape->m_cook->floor_height(candidate) + K_EXTRA + 1e-6;
            }
            if (!std::isfinite(K_POSITION.GetY()) ||
                std::abs(K_POSITION.GetY() - candidate.m_y) > 0.002) {
                result.m_clearance = ClearanceState::UNSUPPORTED_FLOOR;
                break;
            }
            const auto K_PROBE = m_shape->m_cook->probe(
                current,
                { result.m_state.m_feet, candidate, m_settings.m_radius, m_settings.m_height },
                stop);
            if (!K_PROBE) {
                return std::unexpected(K_PROBE.error());
            }
            result.m_clearance = K_PROBE->m_state;
            if (result.m_clearance != ClearanceState::CLEAR_DIAGNOSTIC) {
                break;
            }
            result.m_state.m_feet = candidate;
        }
        if (stop.stop_requested()) {
            return fail(ErrorCode::CANCELLED, "Synthetic capsule advance cancelled");
        }
        result.m_state.m_simulated_ticks += K_STEPS;
        m_state = result.m_state;
        m_clock_units = K_UNITS % K_TICK_UNITS;
        return result;
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Synthetic capsule update allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::BACKEND_FAILURE,
                    std::string("Jolt capsule advance: ") + error.what());
    }
}

}  // namespace world_explorer
