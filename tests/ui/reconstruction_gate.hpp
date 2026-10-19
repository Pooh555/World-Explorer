#pragma once

#include "job_state.hpp"

#include <condition_variable>

namespace world_explorer::test {

// The actual job stops at a reported boundary. Stop wakes it automatically;
// deadlines bound failures without depending on timing sleeps.
class reconstruction_gate {
  public:
    explicit reconstruction_gate(ReconstructionStage stage) : stage_(stage) { }

    void observe(const ReconstructionProgress& value, const std::stop_token& stop) {
        std::unique_lock lock(mutex_);
        if (value.m_stage != stage_ || reached_) return;
        reached_ = true;
        changed_.notify_all();
        const auto resumed = changed_.wait_until(
            lock, stop, std::chrono::steady_clock::now() + std::chrono::seconds(30), [this] {
                return released_;
            });
        timed_out_ = !resumed && !stop.stop_requested();
    }

    bool wait_until_reached(std::chrono::seconds timeout = std::chrono::seconds(5)) {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock, timeout, [this] { return reached_; });
    }

    void release() {
        std::lock_guard lock(mutex_);
        released_ = true;
        changed_.notify_all();
    }

    bool timed_out() {
        std::lock_guard lock(mutex_);
        return timed_out_;
    }

  private:
    ReconstructionStage stage_;
    std::mutex mutex_;
    std::condition_variable_any changed_;
    bool reached_ {}, released_ {}, timed_out_ {};
};

}  // namespace world_explorer::test
