#pragma once

#include "world_explorer/reconstruction_adapters/worker_protocol.hpp"

#include <functional>
#include <optional>
#include <string_view>

namespace world_explorer {

enum class ReconstructionStage : std::uint8_t {
    VALIDATING,
    INSPECTING,
    DECODING,
    POSES,
    MAP,
    FITTING,
    VERIFYING_FIT,
    COMPILING,
    LOADING_SCENE
};

// One bounded value, not a history. Backend elapsed time is scoped to its event,
// not the whole job or an ETA. Fit totals come from the checked request.
struct ReconstructionProgress {
    ReconstructionStage m_stage { ReconstructionStage::VALIDATING };
    std::optional<double> m_backend_elapsed_seconds;
    std::optional<std::uint32_t> m_fit_step;
    std::uint32_t m_fit_steps {};
};

using ReconstructionProgressCallback = std::function<void(const ReconstructionProgress&)>;
std::string_view reconstruction_stage_name(ReconstructionStage stage);
// Only verified worker events should enter here. Unknown stages/invalid optional
// metadata are ignored; arbitrary backend text never becomes product status.
std::optional<ReconstructionProgress> checked_reconstruction_progress(
    const WorkerEvent& event, ReconstructionStage expected_stage, std::uint32_t fit_steps);

}  // namespace world_explorer
