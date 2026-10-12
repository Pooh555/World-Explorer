#include "job_state.hpp"
#include "world_explorer/application/reconstruction_job.hpp"

namespace world_explorer::viewer {
bool is_synthetic_scene(const scene_snapshot& scene) {
    if (!scene)
        return false;
    if (const auto kind = scene->provenance.find("source_kind"); kind != scene->provenance.end())
        return kind->second == "synthetic_diagnostic";
    // Compatibility with the first fixed-camera reference bundle's explicit label.
    const auto diagnostic = scene->provenance.find("diagnostic");
    return diagnostic != scene->provenance.end() && diagnostic->second.starts_with("synthetic ");
}

bool application_state::has_pending_work() {
    std::lock_guard lock(mutex);
    return busy || static_cast<bool>(loaded);
}

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
    processing = true;
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
        processing = false;
        busy = false;
    });
}

void application_state::reconstruction(std::filesystem::path request) {
    if (busy.exchange(true))
        return;
    started = std::chrono::steady_clock::now();
    processing = true;
    {
        std::lock_guard lock(mutex);
        status = "Continuing reconstruction locally. Scene rendering paused; ETA unknown.";
        reconstruction_error.clear();
        log.clear();
    }
    job = std::jthread([this, request = std::move(request)](const std::stop_token& stop) {
        try {
            const auto selected = read_reconstruction_job(request);
            const auto published =
                selected ? reconstruct(*selected, stop,
                                       [this](std::string text) { append(std::move(text)); })
                         : result<std::filesystem::path>{std::unexpected(selected.error())};
            // Publication is a commit boundary. A subsequent stop still leaves
            // the bundle available; the UI may open it in a separate command.
            const auto scene = published
                                   ? load_bundle(*published, load_limits)
                                   : result<scene_snapshot>{std::unexpected(published.error())};
            std::lock_guard lock(mutex);
            if (scene) {
                loaded = *scene;
                status = "Scene published and validated; waiting for renderer admission.";
            } else if (published) {
                status = "Scene published at " + published->string() +
                         "; viewer load failed: " + scene.error().message;
                reconstruction_error = status;
            } else {
                status = published.error().message + ". Previous scene retained.";
                reconstruction_error = status;
            }
        } catch (const std::exception& exception) {
            std::lock_guard lock(mutex);
            status = "Reconstruction failed: " + std::string(exception.what());
            reconstruction_error = status;
        }
        processing = false;
        busy = false;
    });
}

} // namespace world_explorer::viewer
