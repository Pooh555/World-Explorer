#ifndef WORLD_EXPLORER_RECONSTRUCTION_ADAPTERS_PROCESS_HPP
#define WORLD_EXPLORER_RECONSTRUCTION_ADAPTERS_PROCESS_HPP
#include "world_explorer/core/result.hpp"
#include <functional>
#include <stop_token>
#include <string>
#include <vector>
namespace world_explorer {
using process_output_callback = std::function<void(std::string)>;
// Separate byte-preserving streams. Callbacks are serialized and must not block.
// Cancellation terminates/reaps the child tree. Arguments never enter a shell.
result<int> execute_process(const std::vector<std::string>& arguments, const std::stop_token& stop,
                            const process_output_callback& output,
                            const process_output_callback& diagnostic);
} // namespace world_explorer
#endif
