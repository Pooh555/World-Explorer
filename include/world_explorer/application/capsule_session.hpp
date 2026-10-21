#pragma once

#include "world_explorer/navigation/fly_camera.hpp"
#include "world_explorer/scene_store/collision_support.hpp"

#include <chrono>
#include <filesystem>

namespace world_explorer {

enum class CapsuleSessionStatus : std::uint8_t { ACTIVE, STOPPED, INVALIDATED };
std::string_view capsule_session_label(CapsuleSessionStatus status);

struct CapsuleSessionFrame {
    FlyCamera m_camera;
    Vec3d m_feet;
    std::uint64_t m_simulated_ticks {}, m_dropped_ticks {};
    ClearanceState m_clearance { ClearanceState::CLEAR_DIAGNOSTIC };
    CapsuleSessionStatus m_status { CapsuleSessionStatus::ACTIVE };
};

// Single UI/CLI owner. Synthetic diagnostics never grant measured walk mode.
class SyntheticCapsuleSession {
    struct AdmissionToken { };
    struct Impl;

  public:
    explicit SyntheticCapsuleSession(AdmissionToken);
    ~SyntheticCapsuleSession();
    SyntheticCapsuleSession(const SyntheticCapsuleSession&) = delete;
    SyntheticCapsuleSession& operator=(const SyntheticCapsuleSession&) = delete;

    static Result<std::unique_ptr<SyntheticCapsuleSession>>
    open(const std::filesystem::path& request,
         std::string_view expected_sha256,
         const std::stop_token& stop = {});

    SceneSnapshot source() const;
    CapsuleSessionFrame frame() const;
    void stop();
    Result<CapsuleSessionFrame> advance(const SceneState& current,
                                        const FlyInput& input,
                                        std::chrono::nanoseconds elapsed,
                                        const std::stop_token& stop = {});

  private:
    SceneSnapshot m_source;
    CapsuleSessionFrame m_frame;
    double m_eye_height {}, m_speed {};
    std::unique_ptr<Impl> m_impl;
};

// Bounded deterministic CLI exercise of exactly the same session/input path.
Result<std::string> replay_capsule_session(const std::filesystem::path& request,
                                           std::string_view expected_sha256,
                                           const FlyInput& input,
                                           std::chrono::nanoseconds frame_time,
                                           std::size_t frames,
                                           const std::stop_token& stop = {});

}  // namespace world_explorer
