#ifndef WORLD_EXPLORER_VIEWER_JOB_STATE_HPP
#define WORLD_EXPLORER_VIEWER_JOB_STATE_HPP

#include "world_explorer/application/commands.hpp"
#include "world_explorer/scene_store/bundle.hpp"
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

namespace world_explorer::viewer {

// The UI reads results under mutex; one owned job publishes immutable snapshots.
// job is last so its destructor cancels/joins before any captured state is destroyed.
struct application_state {
    bundle_limits load_limits;
    std::mutex mutex;
    std::string log;
    std::string status{"Open a scene, or import a completed camera video."};
    scene_snapshot loaded;
    std::atomic<bool> busy{};
    std::chrono::steady_clock::time_point started;
    std::jthread job;

    void append(std::string text);
    void load(std::filesystem::path path);
    void worker(worker_configuration configuration, std::vector<std::string> arguments);
};

} // namespace world_explorer::viewer
#endif // WORLD_EXPLORER_VIEWER_JOB_STATE_HPP
