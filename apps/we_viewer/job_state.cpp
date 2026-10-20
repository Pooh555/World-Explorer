#include "job_state.hpp"

#include "world_explorer/application/fusion_scene.hpp"
#include "world_explorer/application/geometry_job.hpp"
#include "world_explorer/application/reconstruction_job.hpp"
#include "world_explorer/core/hash.hpp"

namespace world_explorer::viewer {

bool is_synthetic_scene(const SceneSnapshot& scene) {
    if (!scene) {
        return false;
    }
    if (const auto K_KIND = scene->m_provenance.find("source_kind");
        K_KIND != scene->m_provenance.end()) {
        return K_KIND->second == "synthetic_diagnostic";
    }
    // Compatibility with the first fixed-camera reference bundle's explicit label.
    const auto K_DIAGNOSTIC = scene->m_provenance.find("diagnostic");
    return K_DIAGNOSTIC != scene->m_provenance.end() &&
           K_DIAGNOSTIC->second.starts_with("synthetic ");
}

bool application_state::has_pending_work() {
    std::lock_guard lock(mutex);
    return busy || static_cast<bool>(loaded);
}

bool application_state::begin_job() {
    std::lock_guard lock(mutex);
    if (busy || loaded) {
        return false;
    }
    busy = true;
    return true;
}

void application_state::append(std::string text) {
    std::lock_guard lock(mutex);
    constexpr std::size_t K_MAX_LOG { 128uz * 1024uz };
    if (text.size() > K_MAX_LOG) {
        text.erase(0, text.size() - K_MAX_LOG);
    }
    if (log.size() + text.size() > K_MAX_LOG) {
        log.erase(0, log.size() + text.size() - K_MAX_LOG);
    }
    log += text;
}

void application_state::load_fusion(std::filesystem::path request, std::string sha256) {
    if (!begin_job()) {
        return;
    }
    started = std::chrono::steady_clock::now();
    {
        std::lock_guard lock(mutex);
        progress.reset();
        status = "Checking observed geometry and pose dependencies...";
    }
    job = std::jthread([this, request = std::move(request), sha256 = std::move(sha256)](
                           const std::stop_token& stop) {
        const auto K_PIN = sha256.empty() ? sha256_file(request, 1024uz * 1024uz, stop)
                                          : Result<std::string> { sha256 };
        const auto K_SCENE = K_PIN ? load_fusion_scene_request(request, *K_PIN, stop)
                                   : Result<SceneSnapshot> { std::unexpected(K_PIN.error()) };
        std::lock_guard lock(mutex);
        if (stop.stop_requested()) {
            status = "Cancelled. Current scene retained.";
        } else if (K_SCENE) {
            loaded = *K_SCENE;
            status = "Partial observed geometry validated; waiting for renderer admission.";
        } else {
            status =
                "Geometry open failed: " + K_SCENE.error().m_message + ". Current scene retained.";
        }
        busy = false;
    });
}

void application_state::load_depth_space(DepthSpaceOpenRequest request) {
    if (!begin_job()) {
        return;
    }
    started = std::chrono::steady_clock::now();
    {
        std::lock_guard lock(mutex);
        progress.reset();
        status = "Checking pinned depth-space evidence...";
    }
    job = std::jthread([this, request = std::move(request)](const std::stop_token& stop) {
        const auto K_EVIDENCE = open_depth_space(request, stop);
        std::lock_guard lock(mutex);
        if (stop.stop_requested()) {
            status = "Cancelled. Current scene retained.";
        } else if (K_EVIDENCE) {
            loaded = (*K_EVIDENCE)->diagnostic();
            status = "Depth-space evidence admitted; walking unavailable.";
        } else {
            status = "Depth-space open failed: " + K_EVIDENCE.error().m_message +
                     ". Current scene retained.";
        }
        busy = false;
    });
}

void application_state::load(std::filesystem::path path) {
    if (!begin_job()) {
        return;
    }
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
            if (stop.stop_requested()) {
                status = "Cancelled. Current scene retained.";
            } else if (scene) {
                loaded = *scene;
                status = "Scene validated; waiting for renderer admission.";
            } else {
                status = "Open failed: " + scene.error().m_message + ". Current scene retained.";
            }
        } catch (const std::exception& error) {
            std::lock_guard lock(mutex);
            status = "Open failed: " + std::string(error.what());
        }
        busy = false;
    });
}

void application_state::worker(WorkerConfiguration configuration,
                               std::vector<std::string> arguments) {
    if (!begin_job()) {
        return;
    }
    processing = true;
    started = std::chrono::steady_clock::now();
    {
        std::lock_guard lock(mutex);
        progress.reset();
        status = "Processing locally. ETA unknown.";
        log.clear();
    }
    job = std::jthread([this,
                        configuration = std::move(configuration),
                        arguments = std::move(arguments)](const std::stop_token& stop) {
        try {
            auto result = run_worker(configuration, arguments, stop, [this](std::string text) {
                append(std::move(text));
            });
            std::lock_guard lock(mutex);
            status =
                !result ? result.error().m_message
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

void application_state::clear_request_review() {
    std::lock_guard lock(mutex);
    reviewed_request.reset();
    review_error.clear();
    review_invalidated = true;
}

void application_state::review_request(std::filesystem::path request) {
    if (!begin_job()) {
        return;
    }
    started = std::chrono::steady_clock::now();
    {
        std::lock_guard lock(mutex);
        reviewed_request.reset();
        review_error.clear();
        review_invalidated = false;
        status = "Checking reconstruction request. Scene viewing remains available.";
    }
    job = std::jthread([this, request = std::move(request)](const std::stop_token& stop) {
        try {
            auto summary = review_reconstruction_request(request, stop);
            std::lock_guard lock(mutex);
            if (review_invalidated) {
                status = "Request selection changed. Review the selected request before running.";
            } else if (summary) {
                reviewed_request = std::make_shared<const request_summary>(std::move(*summary));
                status = "Request checked. Review actions and limits in Import video.";
            } else {
                review_error = summary.error().m_message.substr(0, 2048);
                status = "Request review failed. Current scene retained.";
            }
        } catch (const std::exception& error) {
            std::lock_guard lock(mutex);
            review_error = std::string(error.what()).substr(0, 2048);
            status = "Request review failed. Current scene retained.";
        }
        busy = false;
    });
}

void application_state::run_reviewed_request() {
    std::shared_ptr<const request_summary> selected;
    {
        std::lock_guard lock(mutex);
        selected = reviewed_request;
    }
    if (selected) {
        reconstruction(selected->request, {}, selected->request_sha256);
    }
}

void application_state::reconstruction(std::filesystem::path request,
                                       reconstruction_observer observed,
                                       std::string expected_request_sha256) {
    if (!begin_job()) {
        return;
    }
    started = std::chrono::steady_clock::now();
    processing = true;
    {
        std::lock_guard lock(mutex);
        reconstructing = true;
        progress = ReconstructionProgress {};
        status = "Validating reconstruction request. Scene rendering paused; ETA unknown.";
        reconstruction_error.clear();
        log.clear();
    }
    job = std::jthread([this,
                        request = std::move(request),
                        observed = std::move(observed),
                        expected_request_sha256 =
                            std::move(expected_request_sha256)](const std::stop_token& stop) {
        const auto K_NOTIFY = [&](const ReconstructionProgress& value) {
            update_reconstruction_progress(value);
            if (observed) {
                observed(value, stop);
            }
        };
        try {
            const auto K_SELECTED = [&]() -> Result<ReconstructionJob> {
                if (!expected_request_sha256.empty()) {
                    std::error_code path_error;
                    const auto K_CURRENT_PATH = std::filesystem::canonical(request, path_error);
                    if (path_error || K_CURRENT_PATH != request) {
                        return fail(ErrorCode::INVALID_DATA,
                                    "Reviewed request path changed. Review again.");
                    }
                }
                return read_reconstruction_job(request, expected_request_sha256);
            }();
            if (!K_SELECTED && !expected_request_sha256.empty()) {
                std::lock_guard lock(mutex);
                reviewed_request.reset();
                review_error = "Request could not be rechecked. Review again before running. " +
                               K_SELECTED.error().m_message.substr(0, 2048);
            }
            const auto K_PUBLISHED =
                K_SELECTED ? reconstruct(
                                 *K_SELECTED,
                                 stop,
                                 [this](std::string text) {
                                     append(std::move(text));
                                 },
                                 K_NOTIFY)
                           : Result<std::filesystem::path> { std::unexpected(K_SELECTED.error()) };
            // Publication is a commit boundary. A subsequent stop still leaves
            // the bundle available; the UI may open it in a separate command.
            if (K_PUBLISHED) {
                K_NOTIFY({ ReconstructionStage::LOADING_SCENE, {}, {}, 0 });
            }
            const auto K_SCENE = K_PUBLISHED
                                     ? load_bundle(*K_PUBLISHED, load_limits)
                                     : Result<SceneSnapshot> { std::unexpected(K_PUBLISHED.error()) };
            complete_reconstruction(std::move(K_SCENE),
                                    K_PUBLISHED ? std::optional { *K_PUBLISHED } : std::nullopt);
        } catch (const std::exception& exception) {
            complete_reconstruction(fail(ErrorCode::INVALID_DATA, exception.what()), {});
        }
    });
}

void application_state::geometry(std::filesystem::path request,
                                 std::string expected_request_sha256,
                                 std::filesystem::path destination) {
    if (!begin_job()) {
        return;
    }
    started = std::chrono::steady_clock::now();
    processing = true;
    {
        std::lock_guard lock(mutex);
        reconstructing = true;
        progress.reset();
        status =
            "Processing configured geometry. Partial surfaces; walking unavailable. ETA unknown.";
        reconstruction_error.clear();
        log.clear();
    }
    job = std::jthread([this,
                        request = std::move(request),
                        pin = std::move(expected_request_sha256),
                        destination = std::move(destination)](const std::stop_token& stop) {
        try {
            const auto K_PUBLISHED =
                run_geometry_job(request, pin, destination, stop, [this](std::string text) {
                    append(std::move(text));
                });
            const auto K_SCENE = K_PUBLISHED
                                     ? load_bundle(*K_PUBLISHED, load_limits)
                                     : Result<SceneSnapshot> { std::unexpected(K_PUBLISHED.error()) };
            complete_reconstruction(K_SCENE,
                                    K_PUBLISHED ? std::optional { *K_PUBLISHED } : std::nullopt);
        } catch (const std::exception& exception) {
            complete_reconstruction(fail(ErrorCode::INVALID_DATA, exception.what()), {});
        }
    });
}

void application_state::request_cancel() {
    job.request_stop();
}

void application_state::update_reconstruction_progress(const ReconstructionProgress& value) {
    std::string message;
    {
        std::lock_guard lock(mutex);
        if (!reconstructing) {
            return;
        }
        progress = value;
        status = reconstruction_stage_name(value.m_stage);
        if (value.m_fit_step) {
            status += ": step " + std::to_string(*value.m_fit_step) + "/" +
                      std::to_string(value.m_fit_steps);
        }
        status += ". Scene rendering paused; ETA unknown.";
        message = "Reconstruction status: " + status;
        if (value.m_backend_elapsed_seconds) {
            message +=
                " Backend elapsed " + std::to_string(*value.m_backend_elapsed_seconds) + "s.";
        }
        message += '\n';
    }
    append(std::move(message));
}

void application_state::complete_reconstruction(
    Result<SceneSnapshot> scene, const std::optional<std::filesystem::path>& published) {
    std::lock_guard lock(mutex);
    if (scene) {
        loaded = *scene;
        status = "Scene published and validated; waiting for renderer admission.";
    } else if (published) {
        status = "Scene published at " + published->string() +
                 "; viewer load failed: " + scene.error().m_message;
        reconstruction_error = status;
    } else {
        status = progress ? std::string(reconstruction_stage_name(progress->m_stage)) + ": " : "";
        status += scene.error().m_code == ErrorCode::CANCELLED ? "Cancelled. " : "Failed. ";
        status += scene.error().m_message + ". Previous scene and committed output retained.";
        reconstruction_error = status;
    }
    progress.reset();
    reconstructing = false;
    processing = false;
    busy = false;
}

}  // namespace world_explorer::viewer
