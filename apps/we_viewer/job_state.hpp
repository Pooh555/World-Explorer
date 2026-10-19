#pragma once

#include "request_summary.hpp"
#include "world_explorer/application/commands.hpp"
#include "world_explorer/application/reconstruction_progress.hpp"
#include "world_explorer/scene_store/bundle.hpp"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

namespace world_explorer::viewer {

// Classification follows explicit scene provenance, never the startup flag/path.
bool is_synthetic_scene(const SceneSnapshot& scene);

// Observed on the owned job thread after state update, outside the state mutex.
// The stop token permits bounded diagnostic synchronization; events are borrowed.
using reconstruction_observer =
    std::function<void(const ReconstructionProgress&, const std::stop_token&)>;

// The UI reads results under mutex; one owned job publishes immutable snapshots.
// job is last so its destructor cancels/joins before any captured state is destroyed.
struct application_state {
    BundleLimits load_limits;
    std::mutex mutex;
    std::string log;
    std::string status { "Open a scene, or import a completed camera video." };
    std::string reconstruction_error;
    SceneSnapshot loaded;
    std::shared_ptr<const request_summary> reviewed_request;
    std::string review_error;
    bool review_invalidated {};
    std::optional<ReconstructionProgress> progress;
    bool reconstructing {};
    std::atomic<bool> busy {};
    std::atomic<bool> processing {};
    std::chrono::steady_clock::time_point started;
    std::jthread job;

    // A completed loader may leave a snapshot awaiting UI admission after busy clears.
    bool has_pending_work();

    void append(std::string text);
    void load(std::filesystem::path path);
    void load_fusion(std::filesystem::path request, std::string sha256 = {});
    void worker(WorkerConfiguration configuration, std::vector<std::string> arguments);
    void review_request(std::filesystem::path request);
    void clear_request_review();
    void run_reviewed_request();
    void reconstruction(std::filesystem::path request,
                        reconstruction_observer observed = {},
                        std::string expected_request_sha256 = {});
    // Called by the UI owner; stop callbacks run without the state mutex held.
    void request_cancel();
    void update_reconstruction_progress(const ReconstructionProgress& value);
    void complete_reconstruction(Result<SceneSnapshot> scene,
                                 const std::optional<std::filesystem::path>& published);

  private:
    bool begin_job();
};

}  // namespace world_explorer::viewer
