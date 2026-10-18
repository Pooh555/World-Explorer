#ifndef WORLD_EXPLORER_APPLICATION_COMMANDS_HPP
#define WORLD_EXPLORER_APPLICATION_COMMANDS_HPP
#include "world_explorer/core/result.hpp"
#include "world_explorer/reconstruction_adapters/worker_protocol.hpp"

#include <filesystem>
#include <functional>
#include <stop_token>
#include <string>
#include <vector>

namespace world_explorer {

using LogCallback = std::function<void(std::string)>;
// Runs a process with argv, never through a shell. Output is drained in bounded
// blocks. Cancellation terminates its process tree and waits for reaping.
Result<int> run_process(const std::vector<std::string>& arguments,
                        const std::stop_token& stop,
                        const LogCallback& log);

struct WorkerConfiguration {
    std::filesystem::path m_script;
    std::string m_python { "python3" };
    bool m_framed_protocol { true };
};

// Observers run synchronously on the calling thread after wire verification.
// Unframed diagnostic processes provide logs only.
Result<int> run_worker(const WorkerConfiguration& configuration,
                       const std::vector<std::string>& arguments,
                       const std::stop_token& stop,
                       const LogCallback& log,
                       const WorkerEventCallback& observed = {});

}  // namespace world_explorer

#endif  // WORLD_EXPLORER_APPLICATION_COMMANDS_HPP
