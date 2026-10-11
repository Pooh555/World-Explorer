#include "job_state.hpp"

namespace world_explorer::viewer {

void application_state::append(std::string text) {
    std::lock_guard lock(mutex);
    constexpr std::size_t MAX_LOG = std::size_t{128} * 1024;
    if (text.size() > MAX_LOG)
        text.erase(0, text.size() - MAX_LOG);
    if (log.size() + text.size() > MAX_LOG)
        log.erase(0, log.size() + text.size() - MAX_LOG);
    log += text;
}

void application_state::load(std::filesystem::path path) {
    if (busy.exchange(true))
        return;
    started = std::chrono::steady_clock::now();
    {
        std::lock_guard lock(mutex);
        status = "Loading and validating scene...";
    }
    job = std::jthread([this, path = std::move(path)](const std::stop_token& stop) {
        try {
            auto scene = load_bundle(path, load_limits);
            std::lock_guard lock(mutex);
            if (stop.stop_requested())
                status = "Cancelled. Current scene retained.";
            else if (scene) {
                loaded = *scene;
                status = "Scene validated; waiting for renderer admission.";
            } else
                status = "Open failed: " + scene.error().message + ". Current scene retained.";
        } catch (const std::exception& error) {
            std::lock_guard lock(mutex);
            status = "Open failed: " + std::string(error.what());
        }
        busy = false;
    });
}

void application_state::worker(worker_configuration configuration,
                               std::vector<std::string> arguments) {
    if (busy.exchange(true))
        return;
    started = std::chrono::steady_clock::now();
    {
        std::lock_guard lock(mutex);
        status = "Processing locally. ETA unknown.";
        log.clear();
    }
    job = std::jthread([this, configuration = std::move(configuration),
                        arguments = std::move(arguments)](const std::stop_token& stop) {
        try {
            auto result = run_worker(configuration, arguments, stop,
                                     [this](std::string text) { append(std::move(text)); });
            std::lock_guard lock(mutex);
            status =
                !result ? result.error().message
                : *result == 0
                    ? "Worker completed. See the output location and logs."
                    : "Worker failed. Committed output retained; inspect the log before retrying.";
        } catch (const std::exception& error) {
            std::lock_guard lock(mutex);
            status = "Worker failed: " + std::string(error.what());
        }
        busy = false;
    });
}

} // namespace world_explorer::viewer
