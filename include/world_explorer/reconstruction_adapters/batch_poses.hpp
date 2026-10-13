#ifndef WORLD_EXPLORER_RECONSTRUCTION_ADAPTERS_BATCH_POSES_HPP
#define WORLD_EXPLORER_RECONSTRUCTION_ADAPTERS_BATCH_POSES_HPP
#include "world_explorer/reconstruction_adapters/completed_poses.hpp"
#include "world_explorer/reconstruction_adapters/worker_protocol.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <stop_token>
#include <string>

namespace world_explorer {
enum class pose_recipe : std::uint8_t { SEQUENTIAL, EXHAUSTIVE_GUIDED };
struct batch_pose_config {
    std::filesystem::path decode_stage, job_directory, worker_script;
    std::string source_sha256{};
    std::string worker_python{"python3"}, pose_python{"python3"};
    pose_recipe recipe{pose_recipe::SEQUENTIAL};
    byte_budget journal_bytes{std::size_t{2} * 1024 * 1024 * 1024};
    std::size_t cpu_threads{4};
    std::function<void(std::string)> log{};
    // Caller must own the job and its exact source/configuration intent. Rebuilds
    // ordered receipts; existing immutable RGB assets must match byte-for-byte.
    bool resume_input{};
    // Verified events are observed synchronously during finish(), not retained.
    worker_event_callback observed{};
};
// Offline single-camera selected-frame profile. start requires capacity for the
// inspected selection; successful submit persists RGB before consuming its lease.
// finish closes admission and synchronously runs the isolated worker. Invoke it
// on the application's job thread; request_stop may interrupt it from any thread.
// Fresh output by default; an owning coordinator can explicitly replay identical
// input with resume_input. Failed runs retain their journal for diagnosis.
class batch_pose_estimator : public pose_estimator {
  public:
    virtual result<std::shared_ptr<const completed_pose_dataset>> acquire_dataset() const = 0;
};
result<std::unique_ptr<batch_pose_estimator>>
make_batch_pose_estimator(batch_pose_config config, const std::stop_token& stop = {});
} // namespace world_explorer
#endif
