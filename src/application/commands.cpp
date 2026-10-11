#include "world_explorer/application/commands.hpp"
#include "world_explorer/reconstruction_adapters/process.hpp"
#include "world_explorer/reconstruction_adapters/worker_protocol.hpp"
namespace world_explorer {
result<int> run_process(const std::vector<std::string>& arguments, const std::stop_token& stop,
                        const log_callback& log) {
    return execute_process(arguments, stop, log, log);
}
result<int> run_worker(const worker_configuration& configuration,
                       const std::vector<std::string>& arguments, const std::stop_token& stop,
                       const log_callback& log) {
    if (configuration.script.empty() || !std::filesystem::is_regular_file(configuration.script))
        return fail(error_code::NOT_FOUND, "Worker script not found; configure --worker-script");
    if (configuration.framed_protocol)
        return execute_worker_protocol(configuration.python, configuration.script, arguments, stop,
                                       log);
    std::vector<std::string> command{configuration.python, "-u", configuration.script.string()};
    command.insert(command.end(), arguments.begin(), arguments.end());
    return run_process(command, stop, log);
}
} // namespace world_explorer
