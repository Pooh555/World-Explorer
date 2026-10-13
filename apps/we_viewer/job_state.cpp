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

bool application_state::begin_job() {
    std::lock_guard lock(mutex);
    if (busy || loaded)
        return false;
    busy = true;
    return true;
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
    if (!begin_job())
        return;
    started = std::chrono::steady_clock::now();
    {
        std::lock_guard lock(mutex);
        progress.reset();
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
    if (!begin_job())
        return;
    processing = true;
    started = std::chrono::steady_clock::now();
    {
        std::lock_guard lock(mutex);
        progress.reset();
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

void application_state::reconstruction(std::filesystem::path request,
                                       reconstruction_observer observed) {
    if (!begin_job())
        return;
    started = std::chrono::steady_clock::now();
    processing = true;
    {
        std::lock_guard lock(mutex);
        reconstructing = true;
        progress = reconstruction_progress{};
        status = "Validating reconstruction request. Scene rendering paused; ETA unknown.";
        reconstruction_error.clear();
        log.clear();
    }
    job = std::jthread([this, request = std::move(request),
                        observed = std::move(observed)](const std::stop_token& stop) {
        const auto notify = [&](const reconstruction_progress& value) {
            update_reconstruction_progress(value);
            if (observed)
                observed(value, stop);
        };
        try {
            const auto selected = read_reconstruction_job(request);
            const auto published =
                selected ? reconstruct(
                               *selected, stop,
                               [this](std::string text) { append(std::move(text)); }, notify)
                         : result<std::filesystem::path>{std::unexpected(selected.error())};
            // Publication is a commit boundary. A subsequent stop still leaves
            // the bundle available; the UI may open it in a separate command.
            if (published)
                notify({reconstruction_stage::LOADING_SCENE, {}, {}, 0});
            const auto scene = published
                                   ? load_bundle(*published, load_limits)
                                   : result<scene_snapshot>{std::unexpected(published.error())};
            complete_reconstruction(std::move(scene),
                                    published ? std::optional{*published} : std::nullopt);
        } catch (const std::exception& exception) {
            complete_reconstruction(fail(error_code::INVALID_DATA, exception.what()), {});
        }
    });
}

void application_state::request_cancel() { job.request_stop(); }

void application_state::update_reconstruction_progress(const reconstruction_progress& value) {
    std::string message;
    {
        std::lock_guard lock(mutex);
        if (!reconstructing)
            return;
        progress = value;
        status = reconstruction_stage_name(value.stage);
        if (value.fit_step)
            status +=
                ": step " + std::to_string(*value.fit_step) + "/" + std::to_string(value.fit_steps);
        status += ". Scene rendering paused; ETA unknown.";
        message = "Reconstruction status: " + status;
        if (value.backend_elapsed_seconds)
            message += " Backend elapsed " + std::to_string(*value.backend_elapsed_seconds) + "s.";
        message += '\n';
    }
    append(std::move(message));
}

void application_state::complete_reconstruction(
    result<scene_snapshot> scene, const std::optional<std::filesystem::path>& published) {
    std::lock_guard lock(mutex);
    if (scene) {
        loaded = *scene;
        status = "Scene published and validated; waiting for renderer admission.";
    } else if (published) {
        status = "Scene published at " + published->string() +
                 "; viewer load failed: " + scene.error().message;
        reconstruction_error = status;
    } else {
        status = progress ? std::string(reconstruction_stage_name(progress->stage)) + ": " : "";
        status += scene.error().code == error_code::CANCELLED ? "Cancelled. " : "Failed. ";
        status += scene.error().message + ". Previous scene and committed output retained.";
        reconstruction_error = status;
    }
    progress.reset();
    reconstructing = false;
    processing = false;
    busy = false;
}

} // namespace world_explorer::viewer
