#ifndef WORLD_EXPLORER_VIEWER_JOB_STATE_HPP
#define WORLD_EXPLORER_VIEWER_JOB_STATE_HPP

#include "world_explorer/application/commands.hpp"
#include "world_explorer/application/reconstruction_progress.hpp"
#include "world_explorer/scene_store/bundle.hpp"
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

namespace world_explorer::viewer {

// Classification follows explicit scene provenance, never the startup flag/path.
bool is_synthetic_scene(const scene_snapshot& scene);

// Observed on the owned job thread after state update, outside the state mutex.
// The stop token permits bounded diagnostic synchronization; events are borrowed.
using reconstruction_observer =
    std::function<void(const reconstruction_progress&, const std::stop_token&)>;

// The UI reads results under mutex; one owned job publishes immutable snapshots.
// job is last so its destructor cancels/joins before any captured state is destroyed.
struct application_state {
    bundle_limits load_limits;
    std::mutex mutex;
    std::string log;
    std::string status{"Open a scene, or import a completed camera video."};
    std::string reconstruction_error;
    scene_snapshot loaded;
    std::optional<reconstruction_progress> progress;
    bool reconstructing{};
    std::atomic<bool> busy{};
    std::atomic<bool> processing{};
    std::chrono::steady_clock::time_point started;
    std::jthread job;

    // A completed loader may leave a snapshot awaiting UI admission after busy clears.
    bool has_pending_work();

    void append(std::string text);
    void load(std::filesystem::path path);
    void worker(worker_configuration configuration, std::vector<std::string> arguments);
    void reconstruction(std::filesystem::path request, reconstruction_observer observed = {});
    // Called by the UI owner; stop callbacks run without the state mutex held.
    void request_cancel();
    void update_reconstruction_progress(const reconstruction_progress& value);
    void complete_reconstruction(result<scene_snapshot> scene,
                                 const std::optional<std::filesystem::path>& published);

  private:
    bool begin_job();
};

} // namespace world_explorer::viewer
#endif // WORLD_EXPLORER_VIEWER_JOB_STATE_HPP
