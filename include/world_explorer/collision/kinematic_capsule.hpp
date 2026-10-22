#pragma once

#include "world_explorer/collision/static_shape.hpp"

#include <chrono>
#include <vector>

namespace world_explorer {

struct SyntheticCapsuleSettings {
    double m_radius {};
    double m_height {};
};

struct SyntheticCapsuleState {
    Vec3d m_feet;
    std::uint64_t m_simulated_ticks {};
};

struct SyntheticCapsuleAdvance {
    SyntheticCapsuleState m_state;
    std::uint32_t m_steps {};
    std::uint64_t m_dropped_ticks {};
    ClearanceState m_clearance { ClearanceState::CLEAR_DIAGNOSTIC };
};

struct SyntheticStepProposal {
    Vec3d m_raw_feet;
    double m_horizontal_distance {};
    bool m_jolt_supported {};
    ClearanceProbe m_straight_probe, m_transition_probe;
};

struct SyntheticStepTransitionState {
    Vec3d m_feet;
    std::size_t m_segment {};
    bool m_finished {};
};

// Development path cursor, not a Jolt controller or walking capability. Each
// tick traverses at most one certified leg; the controller will own its clock.
class SyntheticStepTransition {
    struct AdmissionToken { };

  public:
    explicit SyntheticStepTransition(AdmissionToken);

    static Result<SyntheticStepTransition>
    create(std::shared_ptr<const CanonicalCollisionMesh> cook,
           const SceneState& current,
           const CapsuleSweep& route,
           const std::stop_token& stop = {});

    SyntheticStepTransitionState state() const;

    // Signed speed along the certified path, bounded to 10 units/s. Zero holds
    // the current phase. Negative speed retraces the same certified legs.
    Result<CapsuleSweep>
    advance_tick(const SceneState& current, double path_speed, const std::stop_token& stop = {});

  private:
    std::shared_ptr<const CanonicalCollisionMesh> m_cook;
    std::array<Vec3d, 6uz> m_path {};
    std::array<double, 5uz> m_lengths {};
    CapsuleSweep m_route;
    SyntheticStepTransitionState m_state;
    double m_offset {};
};

// Single-threaded flat/inclined controller and speculative step diagnostic.
// Dimensions are explicit synthetic inputs, never a scale declaration or
// permission for measured walking.
class SyntheticCapsuleController {
    struct AdmissionToken { };
    struct Impl;

    struct StepTraversal {
        SyntheticStepTransition m_cursor;
        Vec3d m_start;
        double m_direction {};
    };

  public:
    explicit SyntheticCapsuleController(AdmissionToken);
    ~SyntheticCapsuleController();
    SyntheticCapsuleController(const SyntheticCapsuleController&) = delete;
    SyntheticCapsuleController& operator=(const SyntheticCapsuleController&) = delete;

    static Result<std::unique_ptr<SyntheticCapsuleController>>
    create(std::shared_ptr<const JoltStaticCollision> shape,
           const SceneState& current,
           SyntheticCapsuleSettings settings,
           Vec3d feet,
           const std::stop_token& stop = {});

    SyntheticCapsuleState state() const;

    // Bounded speculative Jolt replay, independent of accepted movement. Every
    // raw proposal is retained even when the project clearance probes block it.
    static Result<std::vector<SyntheticStepProposal>>
    inspect_step_motion(std::shared_ptr<const JoltStaticCollision> shape,
                        const SceneState& current,
                        SyntheticCapsuleSettings settings,
                        Vec3d feet,
                        Vec3d horizontal_velocity,
                        std::size_t ticks,
                        const std::stop_token& stop = {});

    // Fixed 120 Hz, at most eight ticks per call; excess whole ticks are dropped.
    // Horizontal speed is bounded to 10 units/s, elapsed time to [0, 60] seconds.
    // A failed call preserves position and clock; blocked space consumes time.
    Result<SyntheticCapsuleAdvance> advance(const SceneState& current,
                                            Vec3d horizontal_velocity,
                                            std::chrono::nanoseconds elapsed,
                                            const std::stop_token& stop = {});

  private:
    std::shared_ptr<const JoltStaticCollision> m_shape;
    SyntheticCapsuleSettings m_settings;
    SyntheticCapsuleState m_state;
    std::uint64_t m_clock_units {};
    std::optional<StepTraversal> m_transition;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace world_explorer
