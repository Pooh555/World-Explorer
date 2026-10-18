#include "world_explorer/application/commands.hpp"

#include "world_explorer/reconstruction_adapters/process.hpp"
#include "world_explorer/reconstruction_adapters/worker_protocol.hpp"

namespace world_explorer {

Result<int> run_process(const std::vector<std::string>& arguments,
                        const std::stop_token& stop,
                        const LogCallback& log) {
    return execute_process(arguments, stop, log, log);
}

Result<int> run_worker(const WorkerConfiguration& configuration,
                       const std::vector<std::string>& arguments,
                       const std::stop_token& stop,
                       const LogCallback& log,
                       const WorkerEventCallback& observed) {
    if (configuration.m_script.empty() || !std::filesystem::is_regular_file(configuration.m_script))
        return fail(ErrorCode::NOT_FOUND, "Worker script not found; configure --worker-script");
    if (configuration.m_framed_protocol)
        return execute_worker_protocol(
            configuration.m_python, configuration.m_script, arguments, stop, log, observed);
    std::vector<std::string> command {
        configuration.m_python, "-u", configuration.m_script.string()};
    command.insert(command.end(), arguments.begin(), arguments.end());
    return run_process(command, stop, log);
}

}  // namespace world_explorer
