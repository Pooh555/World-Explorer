#include "world_explorer/application/reconstruction_progress.hpp"

#include <cmath>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace world_explorer {

std::string_view reconstruction_stage_name(ReconstructionStage stage) {
    switch (stage) {
    case ReconstructionStage::VALIDATING:
        return "Validating reconstruction request";
    case ReconstructionStage::INSPECTING:
        return "Inspecting video";
    case ReconstructionStage::DECODING:
        return "Preparing selected frames";
    case ReconstructionStage::POSES:
        return "Preparing camera poses";
    case ReconstructionStage::MAP:
        return "Preparing pose map";
    case ReconstructionStage::FITTING:
        return "Fitting appearance";
    case ReconstructionStage::VERIFYING_FIT:
        return "Verifying completed appearance";
    case ReconstructionStage::COMPILING:
        return "Compiling and publishing scene";
    case ReconstructionStage::LOADING_SCENE:
        return "Loading published scene";
    }
    return "Reconstruction";
}

std::optional<ReconstructionProgress> checked_reconstruction_progress(
    const WorkerEvent& event, ReconstructionStage expected_stage, std::uint32_t fit_steps) {
    const auto* value = std::get_if<WorkerProgress>(&event);
    if (!value || !std::isfinite(value->m_elapsed_seconds) || value->m_elapsed_seconds < 0)
        return {};
    const auto K_EXPECTED = expected_stage == ReconstructionStage::INSPECTING ? "inspect"
                            : expected_stage == ReconstructionStage::DECODING ? "decode"
                            : expected_stage == ReconstructionStage::POSES    ? "poses"
                            : expected_stage == ReconstructionStage::FITTING  ? "appearance"
                                                                              : "";
    if (K_EXPECTED[0] == '\0' || value->m_stage != K_EXPECTED) return {};
    ReconstructionProgress progress {expected_stage, value->m_elapsed_seconds, {}, 0};
    if (expected_stage == ReconstructionStage::FITTING && fit_steps > 0 &&
        value->m_message.size() <= 65536) {
        try {
            const auto K_MESSAGE = nlohmann::json::parse(
                value->m_message, [](int depth, nlohmann::json::parse_event_t, nlohmann::json&) {
                    if (depth > 8)
                        throw std::runtime_error("Progress metadata exceeds nesting limit");
                    return true;
                });
            if (K_MESSAGE.is_object() && K_MESSAGE.contains("step") &&
                K_MESSAGE.at("step").is_number_unsigned()) {
                const auto K_STEP = K_MESSAGE.at("step").get<std::uint64_t>();
                if (K_STEP <= fit_steps) {
                    progress.m_fit_step = static_cast<std::uint32_t>(K_STEP);
                    progress.m_fit_steps = fit_steps;
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

}  // namespace world_explorer
