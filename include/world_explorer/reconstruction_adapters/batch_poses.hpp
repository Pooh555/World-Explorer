#pragma once

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

enum class PoseRecipe : std::uint8_t { SEQUENTIAL, EXHAUSTIVE_GUIDED };

struct BatchPoseConfig {
    std::filesystem::path m_decode_stage, m_job_directory, m_worker_script;
    std::string m_source_sha256 {};
    std::string m_worker_python { "python3" }, m_pose_python { "python3" };
    PoseRecipe m_recipe { PoseRecipe::SEQUENTIAL };
    ByteBudget m_journal_bytes { 2uz * 1024 * 1024 * 1024 };
    std::size_t m_cpu_threads { 4uz };
    std::function<void(std::string)> m_log {};
    // Caller must own the job and its exact source/configuration intent. Rebuilds
    // ordered receipts; existing immutable RGB assets must match byte-for-byte.
    bool m_resume_input {};
    // Verified events are observed synchronously during finish(), not retained.
    WorkerEventCallback m_observed {};
    // Required for frozen inference stages; independently selected original role manifest.
    std::string m_frozen_selection_sha256 {};
};

// Offline single-camera selected-frame profile. start requires capacity for the
// inspected selection; successful submit persists RGB before consuming its lease.
// finish closes admission and synchronously runs the isolated worker. Invoke it
// on the application's job thread; request_stop may interrupt it from any thread.
// Fresh output by default; an owning coordinator can explicitly replay identical
// input with resume_input. Failed runs retain their journal for diagnosis.
class BatchPoseEstimator : public PoseEstimator {
  public:
    virtual Result<std::shared_ptr<const CompletedPoseDataset>> acquire_dataset() const = 0;
};

Result<std::unique_ptr<BatchPoseEstimator>>
make_batch_pose_estimator(BatchPoseConfig config, const std::stop_token& stop = {});

}  // namespace world_explorer
