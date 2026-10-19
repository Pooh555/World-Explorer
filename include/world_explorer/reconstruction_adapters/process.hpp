#pragma once

#include "world_explorer/core/result.hpp"

#include <chrono>
#include <cstddef>
#include <functional>
#include <stop_token>
#include <string>
#include <vector>

namespace world_explorer {

using ProcessOutputCallback = std::function<void(std::string)>;
inline constexpr std::size_t K_PROCESS_INPUT_LIMIT { 64uz * 1024uz };

struct ProcessControl {
    // Called only on the process owner, when the previous bounded frame is sent.
    // Empty means no command ready. Callbacks must not block.
    std::function<Result<std::vector<std::byte>>(bool cancelling)> m_next_input;
    std::chrono::milliseconds m_cancellation_grace {};
};

// Separate byte-preserving streams. Callbacks are serialized and must not block.
// Cancellation terminates/reaps the child tree. Arguments never enter a shell.
Result<int> execute_process(const std::vector<std::string>& arguments,
                            const std::stop_token& stop,
                            const ProcessOutputCallback& output,
                            const ProcessOutputCallback& diagnostic,
                            const ProcessControl& control = {});

}  // namespace world_explorer
