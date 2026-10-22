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

    Result<void> update(JPH::CharacterVirtual& character, JPH::Vec3 velocity, bool plane) {
        const JPH::BroadPhaseLayerFilter K_BROAD_PHASE;
        const JPH::ObjectLayerFilter K_LAYER;
        JPH::CharacterVirtual::ExtendedUpdateSettings settings;
        settings.mWalkStairsStepUp = { 0, .30F, 0 };
        settings.mStickToFloorStepDown = { 0, -.30F, 0 };
        // A nonzero minimum can add more horizontal travel than one tick's
        // requested displacement, especially at low speeds.
        settings.mWalkStairsMinStepForward = 0;
        character.SetLinearVelocity(velocity);
        character.ExtendedUpdate(
            K_STEP_SECONDS, { 0, -9.81F, 0 }, settings, K_BROAD_PHASE, K_LAYER, {}, {}, m_allocator);
        const bool K_OVERFLOW { character.GetMaxHitsExceeded() };
        if (plane) {
            // Settle tangent input after a wall removes horizontal travel.
            character.StickToFloor(
                settings.mStickToFloorStepDown, K_BROAD_PHASE, K_LAYER, {}, {}, m_allocator);
        }
        if (K_OVERFLOW || character.GetMaxHitsExceeded()) {
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "Jolt capsule contact budget exhausted");
        }
        return {};
    }
};

SyntheticCapsuleController::SyntheticCapsuleController(AdmissionToken) { }

SyntheticCapsuleController::~SyntheticCapsuleController() = default;

SyntheticCapsuleState SyntheticCapsuleController::state() const {
    return m_state;
}

SyntheticStepTransition::SyntheticStepTransition(AdmissionToken) { }

Result<SyntheticStepTransition>
SyntheticStepTransition::create(std::shared_ptr<const CanonicalCollisionMesh> cook,
                                const SceneState& current,
                                const CapsuleSweep& route,
                                const std::stop_token& stop) {
    if (!cook || !std::isfinite(route.m_radius) || !std::isfinite(route.m_height) ||
        route.m_radius < .05 || route.m_radius > 1 || route.m_height < 2 * route.m_radius + .01 ||
        route.m_height > 3) {
        return fail(ErrorCode::INVALID_ARGUMENT, "Invalid synthetic transition dimensions or cook");
    }
    const auto K_CERTIFICATE = cook->certify_transition(current, route, stop);
    if (!K_CERTIFICATE) {
        return std::unexpected(K_CERTIFICATE.error());
    }
    if (!K_CERTIFICATE->m_path) {
        return fail(ErrorCode::WOULD_BLOCK, "Synthetic transition lacks a complete clear path");
    }
    SyntheticStepTransition transition { AdmissionToken {} };
    transition.m_cook = std::move(cook);
    transition.m_route = route;
    transition.m_path = *K_CERTIFICATE->m_path;
    transition.m_state.m_feet = route.m_start_feet;
    for (std::size_t segment { 0uz }; segment < transition.m_lengths.size(); ++segment) {
        const auto K_DELTA = transition.m_path[segment + 1uz] - transition.m_path[segment];
        const double K_LENGTH { std::hypot(K_DELTA.m_x, K_DELTA.m_y, K_DELTA.m_z) };
        if (!std::isfinite(K_LENGTH)) {
            return fail(ErrorCode::INVALID_ARGUMENT, "Synthetic transition path length overflow");
        }
        transition.m_lengths[segment] = K_LENGTH;
    }
    if (stop.stop_requested()) {
        return fail(ErrorCode::CANCELLED, "Synthetic transition creation cancelled");
    }
    return transition;
}

SyntheticStepTransitionState SyntheticStepTransition::state() const {
    return m_state;
}

Result<CapsuleSweep> SyntheticStepTransition::advance_tick(const SceneState& current,
                                                           double path_speed,
                                                           const std::stop_token& stop) {
    const auto K_VALID = m_cook->validate_source(current, stop);
    if (!K_VALID) {
        return std::unexpected(K_VALID.error());
    }
    if (!std::isfinite(path_speed) || std::abs(path_speed) > 10) {
        return fail(ErrorCode::INVALID_ARGUMENT, "Invalid synthetic transition path speed");
    }
    CapsuleSweep sweep { m_state.m_feet, m_state.m_feet, m_route.m_radius, m_route.m_height };
    auto segment = m_state.m_segment;
    double offset { m_offset };
    if (path_speed > 0) {
        while (segment < m_lengths.size() && m_lengths[segment] == 0) {
            ++segment;
        }
    } else if (path_speed < 0) {
        while (offset == 0 && segment > 0uz) {
            --segment;
            offset = m_lengths[segment];
        }
    }
    if (path_speed == 0 || segment == m_lengths.size() ||
        (path_speed < 0 && segment == 0uz && offset == 0)) {
        return sweep;
    }
    const double K_LENGTH { m_lengths[segment] };
    // No leftover budget crosses a corner. Otherwise the final-position chord
    // could cut the riser even though both legs have complete certificates.
    const double K_TRAVEL {
        std::min(std::abs(path_speed) / 120, path_speed > 0 ? K_LENGTH - offset : offset) };
    offset = std::clamp(offset + (path_speed > 0 ? K_TRAVEL : -K_TRAVEL), 0., K_LENGTH);
    if (offset == 0) {
        sweep.m_end_feet = m_path[segment];
    } else if (offset == K_LENGTH) {
        sweep.m_end_feet = m_path[segment + 1uz];
    } else {
        const auto K_START = m_path[segment], K_END = m_path[segment + 1uz];
        const double K_FRACTION { offset / K_LENGTH };
        const auto K_INTERPOLATE = [K_FRACTION](double start, double end) {
            return std::clamp(
                start + (end - start) * K_FRACTION, std::min(start, end), std::max(start, end));
        };
        sweep.m_end_feet = { K_INTERPOLATE(K_START.m_x, K_END.m_x),
                            K_INTERPOLATE(K_START.m_y, K_END.m_y),
                            K_INTERPOLATE(K_START.m_z, K_END.m_z) };
    }
    if (path_speed > 0 && offset == K_LENGTH) {
        do {
            ++segment;
        } while (segment < m_lengths.size() && m_lengths[segment] == 0);
        offset = 0;
    }
    if (stop.stop_requested()) {
        return fail(ErrorCode::CANCELLED, "Synthetic transition tick cancelled");
    }
    m_state = { sweep.m_end_feet, segment, segment == m_lengths.size() };
    m_offset = offset;
    return sweep;
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

Result<std::vector<SyntheticStepProposal>>
SyntheticCapsuleController::inspect_step_motion(std::shared_ptr<const JoltStaticCollision> shape,
                                                const SceneState& current,
                                                SyntheticCapsuleSettings settings,
                                                Vec3d feet,
                                                Vec3d horizontal_velocity,
                                                std::size_t ticks,
                                                const std::stop_token& stop) {
    if (!shape || shape->m_cook->parameters().m_format_version != 4U || ticks == 0uz ||
        ticks > 600uz || !std::isfinite(horizontal_velocity.m_x) ||
        !std::isfinite(horizontal_velocity.m_z) || horizontal_velocity.m_y != 0 ||
        std::hypot(horizontal_velocity.m_x, horizontal_velocity.m_z) > 10) {
        return fail(ErrorCode::INVALID_ARGUMENT, "Invalid bounded synthetic step inspection");
    }
    const auto K_OWNER = create(std::move(shape), current, settings, feet, stop);
    if (!K_OWNER) {
        return std::unexpected(K_OWNER.error());
    }
    const auto K_VELOCITY = collision_detail::checked_vec(horizontal_velocity, 1e-5);
    if (!K_VELOCITY) {
        return std::unexpected(K_VELOCITY.error());
    }
    try {
        auto& owner = **K_OWNER;
        auto& impl = *owner.m_impl;
        const auto K_START =
            collision_detail::checked_vec(feet, owner.m_shape->m_impl->m_precision_tolerance);
        if (!K_START) {
            return std::unexpected(K_START.error());
        }
        JPH::CharacterVirtual character { &impl.m_character_settings,
                                         JPH::RVec3(*K_START),
                                         JPH::Quat::sIdentity(),
                                         &impl.m_world };
        const JPH::BroadPhaseLayerFilter K_BROAD_PHASE;
        const JPH::ObjectLayerFilter K_LAYER;
        character.RefreshContacts(K_BROAD_PHASE, K_LAYER, {}, {}, impl.m_allocator);
        const auto K_QUERY_FEET = [&](Vec3d raw) {
            const double K_FLOOR { owner.m_shape->m_cook->floor_height(raw) };
            if (std::abs(raw.m_y - K_FLOOR) <= .002) {
                raw.m_y = K_FLOOR;
            }
            return raw;
        };
        std::vector<SyntheticStepProposal> proposals;
        proposals.reserve(ticks);
        for (std::size_t tick { 0uz }; tick < ticks; ++tick) {
            if (stop.stop_requested()) {
                return fail(ErrorCode::CANCELLED, "Synthetic step inspection cancelled");
            }
            const auto K_UPDATE = impl.update(
                character, *K_VELOCITY + JPH::Vec3(0, -9.81F * K_STEP_SECONDS, 0), false);
            if (!K_UPDATE) {
                return std::unexpected(K_UPDATE.error());
            }
            const auto K_POSITION = character.GetPosition();
            const Vec3d K_RAW { K_POSITION.GetX(), K_POSITION.GetY(), K_POSITION.GetZ() };
            const auto K_CHECKED =
                collision_detail::checked_vec(K_RAW, owner.m_shape->m_impl->m_precision_tolerance);
            if (!K_CHECKED) {
                return std::unexpected(K_CHECKED.error());
            }
            const CapsuleSweep K_SWEEP {
                K_QUERY_FEET(feet), K_QUERY_FEET(K_RAW), settings.m_radius, settings.m_height };
            const auto K_STRAIGHT = owner.m_shape->m_cook->probe(current, K_SWEEP, stop);
            if (!K_STRAIGHT) {
                return std::unexpected(K_STRAIGHT.error());
            }
            const auto K_TRANSITION =
                owner.m_shape->m_cook->probe_transition(current, K_SWEEP, stop);
            if (!K_TRANSITION) {
                return std::unexpected(K_TRANSITION.error());
            }
            proposals.push_back({ K_RAW,
                                 std::hypot(K_RAW.m_x - feet.m_x, K_RAW.m_z - feet.m_z),
                                 character.IsSupported(),
                                 *K_STRAIGHT,
                                 *K_TRANSITION });
            feet = K_RAW;
        }
        if (stop.stop_requested()) {
            return fail(ErrorCode::CANCELLED, "Synthetic step inspection cancelled");
        }
        return proposals;
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Synthetic step inspection allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::BACKEND_FAILURE,
                    std::string("Jolt step inspection: ") + error.what());
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
        auto transition = m_transition;
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
        for (std::uint64_t tick {}; tick < K_STEPS; ++tick) {
            if (stop.stop_requested()) {
                return fail(ErrorCode::CANCELLED, "Synthetic capsule advance cancelled");
            }
            const bool K_STEP { m_shape->m_cook->parameters().m_format_version == 4U };
            if (K_STEP && !transition) {
                const auto K_FEET = result.m_state.m_feet;
                const auto K_ROUTE =
                    m_shape->m_cook->select_transition(current,
                                                       { K_FEET,
                                                        K_FEET + horizontal_velocity * (1. / 120),
                                                        m_settings.m_radius,
                                                        m_settings.m_height },
                                                       stop);
                if (!K_ROUTE) {
                    return std::unexpected(K_ROUTE.error());
                }
                if (K_ROUTE->has_value()) {
                    auto cursor =
                        SyntheticStepTransition::create(m_shape->m_cook, current, **K_ROUTE, stop);
                    if (!cursor) {
                        if (cursor.error().m_code != ErrorCode::WOULD_BLOCK) {
                            return std::unexpected(cursor.error());
                        }
                        const auto K_BLOCKED =
                            m_shape->m_cook->probe_transition(current, **K_ROUTE, stop);
                        if (!K_BLOCKED) {
                            return std::unexpected(K_BLOCKED.error());
                        }
                        result.m_clearance = K_BLOCKED->m_state;
                        break;
                    }
                    transition = StepTraversal { std::move(*cursor),
                                                K_FEET,
                                                (**K_ROUTE).m_end_feet.m_x > K_FEET.m_x ? 1. : -1. };
                }
            }
            if (transition) {
                auto& traversal = *transition;
                const auto K_SWEEP = traversal.m_cursor.advance_tick(
                    current, horizontal_velocity.m_x * traversal.m_direction, stop);
                if (!K_SWEEP) {
                    return std::unexpected(K_SWEEP.error());
                }
                const auto K_POSITION = collision_detail::checked_vec(
                    K_SWEEP->m_end_feet, m_shape->m_impl->m_precision_tolerance);
                if (!K_POSITION) {
                    return std::unexpected(K_POSITION.error());
                }
                // The complete certificate owns these single-leg sweeps,
                // including airborne phases. Keep Jolt's contact state at the
                // accepted project point; do not apply stick-down to a lift.
                character.SetPosition(JPH::RVec3(*K_POSITION));
                character.RefreshContacts(K_BROAD_PHASE, K_LAYER, {}, {}, m_impl->m_allocator);
                if (character.GetMaxHitsExceeded()) {
                    return fail(ErrorCode::RESOURCE_EXHAUSTED,
                                "Jolt capsule contact budget exhausted");
                }
                result.m_state.m_feet = K_SWEEP->m_end_feet;
                if (traversal.m_cursor.state().m_finished ||
                    result.m_state.m_feet == traversal.m_start) {
                    transition.reset();
                }
                continue;
            }
            const bool K_RAMP { m_shape->m_cook->parameters().m_format_version == 3U };
            const auto K_GRADIENT = m_shape->m_cook->floor_gradient();
            const float K_VERTICAL {
                K_RAMP ? static_cast<float>(dot(K_GRADIENT, horizontal_velocity)) : 0.F };
            // V3 is grounded on one admitted plane. Its downward support sweep
            // replaces free-fall integration, avoiding idle downslope creep.
            const float K_GRAVITY_STEP { K_RAMP ? 0.F : 9.81F * K_STEP_SECONDS };
            const auto K_UPDATE = m_impl->update(
                character, *K_VELOCITY + JPH::Vec3(0, K_VERTICAL - K_GRAVITY_STEP, 0), K_RAMP);
            if (!K_UPDATE) {
                return std::unexpected(K_UPDATE.error());
            }
            const auto K_POSITION = character.GetPosition();
            Vec3d candidate { K_POSITION.GetX(), result.m_state.m_feet.m_y, K_POSITION.GetZ() };
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
        m_transition = std::move(transition);
        return result;
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Synthetic capsule update allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::BACKEND_FAILURE,
                    std::string("Jolt capsule advance: ") + error.what());
    }
}

}  // namespace world_explorer
