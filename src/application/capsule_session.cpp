#include "world_explorer/application/capsule_session.hpp"

#include "collision_inputs.hpp"
#include "world_explorer/collision/kinematic_capsule.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace world_explorer {

struct SyntheticCapsuleSession::Impl {
    std::unique_ptr<SyntheticCapsuleController> m_controller;
};

std::string_view capsule_session_label(CapsuleSessionStatus status) {
    switch (status) {
    case CapsuleSessionStatus::ACTIVE:
        return "ACTIVE_SYNTHETIC";
    case CapsuleSessionStatus::STOPPED:
        return "STOPPED";
    case CapsuleSessionStatus::INVALIDATED:
        return "INVALIDATED";
    }
    return "INVALIDATED";
}

SyntheticCapsuleSession::SyntheticCapsuleSession(AdmissionToken) { }

SyntheticCapsuleSession::~SyntheticCapsuleSession() = default;

SceneSnapshot SyntheticCapsuleSession::source() const {
    return m_source;
}

CapsuleSessionFrame SyntheticCapsuleSession::frame() const {
    return m_frame;
}

void SyntheticCapsuleSession::stop() {
    if (m_frame.m_status == CapsuleSessionStatus::ACTIVE) {
        m_frame.m_status = CapsuleSessionStatus::STOPPED;
    }
}

Result<std::unique_ptr<SyntheticCapsuleSession>>
SyntheticCapsuleSession::open(const std::filesystem::path& request,
                              std::string_view expected_sha256,
                              const std::stop_token& stop) {
    try {
        const auto K_DOCUMENT = collision_inputs::read_request(request, expected_sha256, stop);
        if (!K_DOCUMENT) {
            return std::unexpected(K_DOCUMENT.error());
        }
        collision_inputs::fields(*K_DOCUMENT,
                                 { "kind",
                                  "source_bundle",
                                  "source_manifest_sha256",
                                  "support",
                                  "support_sha256",
                                  "capsule" });
        if (K_DOCUMENT->at("kind") != "SYNTHETIC_CAPSULE_SESSION_V1") {
            return fail(ErrorCode::INVALID_DATA, "Unknown synthetic capsule session request");
        }
        const auto& capsule = K_DOCUMENT->at("capsule");
        collision_inputs::fields(
            capsule, { "feet", "radius", "height", "eye_height", "speed", "yaw_radians" });
        const auto K_FEET = collision_inputs::vector(capsule.at("feet"));
        const SyntheticCapsuleSettings K_SETTINGS { collision_inputs::real(capsule.at("radius")),
                                                   collision_inputs::real(capsule.at("height")) };
        const double K_EYE { collision_inputs::real(capsule.at("eye_height")) };
        const double K_SPEED { collision_inputs::real(capsule.at("speed")) };
        const double K_YAW { collision_inputs::real(capsule.at("yaw_radians")) };
        if (K_EYE <= 0 || K_EYE >= K_SETTINGS.m_height || K_SPEED < .01 || K_SPEED > 10 ||
            std::abs(K_YAW) > std::numbers::pi) {
            return fail(ErrorCode::INVALID_ARGUMENT, "Invalid synthetic eye height, speed or yaw");
        }
        const auto K_INPUTS = collision_inputs::load(request, *K_DOCUMENT, stop);
        if (!K_INPUTS) {
            return std::unexpected(K_INPUTS.error());
        }
        const auto K_COOK = cook_synthetic_collision(K_INPUTS->m_source, K_INPUTS->m_support, stop);
        if (!K_COOK) {
            return std::unexpected(K_COOK.error());
        }
        const auto K_SHAPE = build_jolt_static_collision(*K_COOK, *K_INPUTS->m_source, stop);
        if (!K_SHAPE) {
            return std::unexpected(K_SHAPE.error());
        }
        auto controller = SyntheticCapsuleController::create(
            *K_SHAPE, *K_INPUTS->m_source, K_SETTINGS, K_FEET, stop);
        if (!controller) {
            return std::unexpected(controller.error());
        }
        auto session = std::make_unique<SyntheticCapsuleSession>(AdmissionToken {});
        session->m_source = K_INPUTS->m_source;
        session->m_eye_height = K_EYE;
        session->m_speed = K_SPEED;
        session->m_frame.m_feet = K_FEET;
        session->m_frame.m_camera.m_position = K_FEET + Vec3d { 0, K_EYE, 0 };
        session->m_frame.m_camera.m_yaw = K_YAW;
        session->m_frame.m_camera.m_speed = K_SPEED;
        session->m_impl = std::make_unique<Impl>();
        session->m_impl->m_controller = std::move(*controller);
        return session;
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Capsule session allocation failed");
    } catch (const std::exception& error) {
        return fail(ErrorCode::INVALID_DATA, error.what());
    }
}

Result<CapsuleSessionFrame> SyntheticCapsuleSession::advance(const SceneState& current,
                                                             const FlyInput& input,
                                                             std::chrono::nanoseconds elapsed,
                                                             const std::stop_token& stop) {
    if (m_frame.m_status != CapsuleSessionStatus::ACTIVE) {
        return fail(m_frame.m_status == CapsuleSessionStatus::INVALIDATED
                        ? ErrorCode::RESYNC_REQUIRED
                        : ErrorCode::CANCELLED,
                    "Synthetic capsule stopped; open a fresh session to continue");
    }
    if (!std::isfinite(input.m_forward) || !std::isfinite(input.m_right) ||
        !std::isfinite(input.m_up) || !std::isfinite(input.m_mouse_x) ||
        !std::isfinite(input.m_mouse_y) || !std::isfinite(input.m_wheel) ||
        std::abs(input.m_forward) > 1 || std::abs(input.m_right) > 1 ||
        std::abs(input.m_mouse_x) > 1e6 || std::abs(input.m_mouse_y) > 1e6 ||
        elapsed < std::chrono::nanoseconds::zero() || elapsed > std::chrono::seconds(60)) {
        return fail(ErrorCode::INVALID_ARGUMENT, "Invalid synthetic capsule frame input");
    }
    auto camera = m_frame.m_camera;
    const bool K_MOVING { input.m_captured && input.m_focused && !input.m_text_input };
    FlyInput look = input;
    look.m_forward = look.m_right = look.m_up = look.m_wheel = 0;
    camera.update(look, std::chrono::duration<double>(elapsed).count());
    Vec3d velocity;
    if (K_MOVING) {
        velocity = Vec3d { std::sin(camera.m_yaw), 0, -std::cos(camera.m_yaw) } * input.m_forward +
                   camera.right() * input.m_right;
        const double K_LENGTH { std::sqrt(dot(velocity, velocity)) };
        if (K_LENGTH > 1) {
            velocity = velocity * (1 / K_LENGTH);
        }
        velocity = velocity * std::min(10.0, m_speed * (input.m_accelerate ? 4 : 1));
    }
    const auto K_ADVANCE = m_impl->m_controller->advance(
        current, velocity, K_MOVING ? elapsed : std::chrono::nanoseconds::zero(), stop);
    if (!K_ADVANCE) {
        if (K_ADVANCE.error().m_code == ErrorCode::CANCELLED) {
            this->stop();
        } else if (K_ADVANCE.error().m_code != ErrorCode::INVALID_ARGUMENT) {
            m_frame.m_status = CapsuleSessionStatus::INVALIDATED;
        }
        return std::unexpected(K_ADVANCE.error());
    }
    camera.m_position = K_ADVANCE->m_state.m_feet + Vec3d { 0, m_eye_height, 0 };
    m_frame.m_camera = camera;
    m_frame.m_feet = K_ADVANCE->m_state.m_feet;
    m_frame.m_simulated_ticks = K_ADVANCE->m_state.m_simulated_ticks;
    m_frame.m_dropped_ticks = K_ADVANCE->m_dropped_ticks;
    m_frame.m_clearance = K_ADVANCE->m_clearance;
    return m_frame;
}

Result<std::string> replay_capsule_session(const std::filesystem::path& request,
                                           std::string_view expected_sha256,
                                           const FlyInput& input,
                                           std::chrono::nanoseconds frame_time,
                                           std::size_t frames,
                                           const std::stop_token& stop) {
    if (frames == 0uz || frames > 600uz) {
        return fail(ErrorCode::INVALID_ARGUMENT, "Capsule replay requires 1..600 frames");
    }
    auto session = SyntheticCapsuleSession::open(request, expected_sha256, stop);
    if (!session) {
        return std::unexpected(session.error());
    }
    try {
        collision_inputs::Json samples = collision_inputs::Json::array();
        for (std::size_t frame { 0uz }; frame < frames; ++frame) {
            const auto K_FRAME =
                (*session)->advance(*(*session)->source(), input, frame_time, stop);
            if (!K_FRAME) {
                return std::unexpected(K_FRAME.error());
            }
            const auto K_FEET = K_FRAME->m_feet;
            samples.push_back({ { "feet", { K_FEET.m_x, K_FEET.m_y, K_FEET.m_z } },
                               { "tick", K_FRAME->m_simulated_ticks },
                               { "dropped_ticks", K_FRAME->m_dropped_ticks },
                               { "clearance", clearance_label(K_FRAME->m_clearance) } });
        }
        return collision_inputs::Json { { "kind", "SYNTHETIC_CAPSULE_REPLAY_V1" },
                                       { "request_sha256", expected_sha256 },
                                       { "source_sha256", scene_content_hash(*(*session)->source()) },
                                       { "walking_enabled", false },
                                       { "measured_collision", false },
                                       { "samples", samples } }
            .dump(2);
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Capsule replay allocation failed");
    }
}

}  // namespace world_explorer
