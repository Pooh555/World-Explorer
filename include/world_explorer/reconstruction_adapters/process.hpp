#ifndef WORLD_EXPLORER_RECONSTRUCTION_ADAPTERS_PROCESS_HPP
#define WORLD_EXPLORER_RECONSTRUCTION_ADAPTERS_PROCESS_HPP
#include "world_explorer/core/result.hpp"
#include <chrono>
#include <cstddef>
#include <functional>
#include <stop_token>
#include <string>
#include <vector>
namespace world_explorer {
using process_output_callback = std::function<void(std::string)>;
inline constexpr std::size_t PROCESS_INPUT_LIMIT = std::size_t{64} * 1024;
struct process_control {
    // Called only on the process owner, when the previous bounded frame is sent.
    // Empty means no command ready. Callbacks must not block.
    std::function<result<std::vector<std::byte>>(bool cancelling)> next_input;
    std::chrono::milliseconds cancellation_grace{};
};
// Separate byte-preserving streams. Callbacks are serialized and must not block.
// Cancellation terminates/reaps the child tree. Arguments never enter a shell.
result<int> execute_process(const std::vector<std::string>& arguments, const std::stop_token& stop,
                            const process_output_callback& output,
                            const process_output_callback& diagnostic,
                            const process_control& control = {});
} // namespace world_explorer
#endif
