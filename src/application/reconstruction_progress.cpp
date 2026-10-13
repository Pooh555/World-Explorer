#include "world_explorer/application/reconstruction_progress.hpp"
#include <cmath>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace world_explorer {
std::string_view reconstruction_stage_name(reconstruction_stage stage) {
    switch (stage) {
    case reconstruction_stage::VALIDATING:
        return "Validating reconstruction request";
    case reconstruction_stage::INSPECTING:
        return "Inspecting video";
    case reconstruction_stage::DECODING:
        return "Preparing selected frames";
    case reconstruction_stage::POSES:
        return "Preparing camera poses";
    case reconstruction_stage::MAP:
        return "Preparing pose map";
    case reconstruction_stage::FITTING:
        return "Fitting appearance";
    case reconstruction_stage::VERIFYING_FIT:
        return "Verifying completed appearance";
    case reconstruction_stage::COMPILING:
        return "Compiling and publishing scene";
    case reconstruction_stage::LOADING_SCENE:
        return "Loading published scene";
    }
    return "Reconstruction";
}

std::optional<reconstruction_progress>
checked_reconstruction_progress(const worker_event& event, reconstruction_stage expected_stage,
                                std::uint32_t fit_steps) {
    const auto* value = std::get_if<worker_progress>(&event);
    if (!value || !std::isfinite(value->elapsed_seconds) || value->elapsed_seconds < 0)
        return {};
    const auto expected = expected_stage == reconstruction_stage::INSPECTING ? "inspect"
                          : expected_stage == reconstruction_stage::DECODING ? "decode"
                          : expected_stage == reconstruction_stage::POSES    ? "poses"
                          : expected_stage == reconstruction_stage::FITTING  ? "appearance"
                                                                             : "";
    if (expected[0] == '\0' || value->stage != expected)
        return {};
    reconstruction_progress progress{expected_stage, value->elapsed_seconds, {}, 0};
    if (expected_stage == reconstruction_stage::FITTING && fit_steps > 0 &&
        value->message.size() <= 65536) {
        try {
            const auto message = nlohmann::json::parse(
                value->message, [](int depth, nlohmann::json::parse_event_t, nlohmann::json&) {
                    if (depth > 8)
                        throw std::runtime_error("Progress metadata exceeds nesting limit");
                    return true;
                });
            if (message.is_object() && message.contains("step") &&
                message.at("step").is_number_unsigned()) {
                const auto step = message.at("step").get<std::uint64_t>();
                if (step <= fit_steps) {
                    progress.fit_step = static_cast<std::uint32_t>(step);
                    progress.fit_steps = fit_steps;
                }
            }
        } catch (const nlohmann::json::exception&) {
            // Optional telemetry cannot invalidate a checked worker session.
            return progress;
        } catch (const std::runtime_error&) {
            return progress;
        }
    }
    return progress;
}
} // namespace world_explorer
