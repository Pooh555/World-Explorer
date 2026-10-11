#ifndef WORLD_EXPLORER_APPLICATION_COMMANDS_HPP
#define WORLD_EXPLORER_APPLICATION_COMMANDS_HPP
#include "world_explorer/core/result.hpp"
#include <filesystem>
#include <functional>
#include <stop_token>
#include <string>
#include <vector>

namespace world_explorer {
using log_callback = std::function<void(std::string)>;
// Runs a process with argv, never through a shell. Output is drained in bounded
// blocks. Cancellation terminates its process tree and waits for reaping.
result<int> run_process(const std::vector<std::string>& arguments, const std::stop_token& stop,
                        const log_callback& log);
struct worker_configuration {
    std::filesystem::path script;
    std::string python{"python3"};
};
result<int> run_worker(const worker_configuration& configuration,
                       const std::vector<std::string>& arguments, const std::stop_token& stop,
                       const log_callback& log);
} // namespace world_explorer

#endif // WORLD_EXPLORER_APPLICATION_COMMANDS_HPP
