#pragma once

#include "world_explorer/collision/static_shape.hpp"

#include <chrono>

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

// Single-threaded flat-floor diagnostic. Dimensions are explicit synthetic
// inputs, never a scale declaration or permission for measured walking.
class SyntheticCapsuleController {
    struct AdmissionToken { };
    struct Impl;

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
    std::unique_ptr<Impl> m_impl;
};

}  // namespace world_explorer
