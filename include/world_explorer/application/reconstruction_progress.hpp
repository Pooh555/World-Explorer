#ifndef WORLD_EXPLORER_APPLICATION_RECONSTRUCTION_PROGRESS_HPP
#define WORLD_EXPLORER_APPLICATION_RECONSTRUCTION_PROGRESS_HPP

#include "world_explorer/reconstruction_adapters/worker_protocol.hpp"
#include <functional>
#include <optional>
#include <string_view>

namespace world_explorer {
enum class reconstruction_stage : std::uint8_t {
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
struct reconstruction_progress {
    reconstruction_stage stage{reconstruction_stage::VALIDATING};
    std::optional<double> backend_elapsed_seconds;
    std::optional<std::uint32_t> fit_step;
    std::uint32_t fit_steps{};
};
using reconstruction_progress_callback = std::function<void(const reconstruction_progress&)>;
std::string_view reconstruction_stage_name(reconstruction_stage stage);
// Only verified worker events should enter here. Unknown stages/invalid optional
// metadata are ignored; arbitrary backend text never becomes product status.
std::optional<reconstruction_progress>
checked_reconstruction_progress(const worker_event& event, reconstruction_stage expected_stage,
                                std::uint32_t fit_steps);
} // namespace world_explorer
#endif
