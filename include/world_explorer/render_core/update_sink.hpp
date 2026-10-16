#ifndef WORLD_EXPLORER_RENDER_CORE_UPDATE_SINK_HPP
#define WORLD_EXPLORER_RENDER_CORE_UPDATE_SINK_HPP
#include "world_explorer/scene_store/scene.hpp"
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace world_explorer {
enum class update_state : std::uint8_t {
    ACCEPTED,
    ASSETS_AVAILABLE,
    UPLOADED,
    RENDER_VISIBLE,
    RETIRED,
    CANCELLED,
    FAILED
};
struct update_progress {
    update_state state{update_state::FAILED};
    scene_revision_id revision{};
    std::uint64_t timeline_value{};
    std::optional<error> failure;
};
struct residency_budget {
    std::size_t bytes{512ULL * 1024 * 1024};
    std::size_t max_splats{250000};
    std::size_t max_pending{4};
};
struct admitted_update {
    update_ticket ticket;
    scene_delta_batch batch;
};
// Prepared off the render admission path. The snapshot remains immutable for the
// lifetime of this value; a failed enqueue leaves the value available for retry.
class prepared_scene_update {
  public:
    prepared_scene_update(const prepared_scene_update&) = delete;
    prepared_scene_update& operator=(const prepared_scene_update&) = delete;
    prepared_scene_update(prepared_scene_update&&) noexcept = default;
    prepared_scene_update& operator=(prepared_scene_update&&) noexcept = default;

  private:
    friend class renderer_update_sink;
    prepared_scene_update(scene_delta_batch batch, std::size_t bytes, std::string hash,
                          residency_budget budget)
        : batch_(std::move(batch)), bytes_(bytes), hash_(std::move(hash)), budget_(budget) {}
    scene_delta_batch batch_;
    std::size_t bytes_{};
    std::string hash_;
    residency_budget budget_;
};
// Multi-producer, single render-consumer. Snapshots are immutable. Failed enqueue
// preserves its argument. Tickets and duplicate operations are retained for a
// bounded 256-operation replay window; consumers must resync after losing it.
class renderer_update_sink {
  public:
    explicit renderer_update_sink(residency_budget budget = {});
    result<prepared_scene_update> prepare(const scene_delta_batch& batch) const;
    result<update_ticket> enqueue(prepared_scene_update& prepared);
    result<update_ticket> enqueue(scene_delta_batch& batch);
    result<update_progress> query_progress(update_ticket ticket) const;
    result<void> set_residency_budget(residency_budget budget);
    void cancel_pending();
    scene_snapshot visible_snapshot() const;
    std::size_t queued_bytes() const;
    // Render-owner lifecycle operations. GPU timeline completion governs release.
    std::optional<admitted_update> take_next();
    void acknowledge_uploaded(update_ticket ticket, std::uint64_t timeline);
    void acknowledge_visible(update_ticket ticket, scene_snapshot snapshot, std::uint64_t timeline);
    void retire_completed(std::uint64_t completed_timeline);
    void fail_update(update_ticket ticket, error failure);

  private:
    struct record {
        update_ticket ticket;
        operation_id operation;
        scene_id scene;
        world_epoch epoch;
        scene_revision_id base;
        update_progress progress;
        std::size_t bytes{};
        std::string content_hash;
    };
    void cancel_pending_locked();
    record* find(update_ticket ticket);
    std::string fingerprint(const scene_state& scene) const;
    std::string chunk_fingerprint(const std::shared_ptr<const scene_chunk>& chunk) const;
    struct cached_chunk_fingerprint {
        std::weak_ptr<const scene_chunk> owner;
        std::string hash;
    };
    mutable std::mutex fingerprint_mutex_;
    mutable std::deque<cached_chunk_fingerprint> fingerprint_cache_;
    mutable std::mutex mutex_;
    residency_budget budget_;
    std::deque<admitted_update> pending_;
    std::deque<record> history_;
    scene_snapshot visible_;
    std::optional<update_ticket> visible_ticket_;
    std::optional<update_ticket> active_ticket_;
    scene_id accepted_scene_;
    world_epoch accepted_epoch_;
    scene_revision_id accepted_revision_;
    std::size_t queued_bytes_{};
    std::size_t retired_bytes_{};
    std::uint64_t next_ticket_{1};
};
} // namespace world_explorer

#endif // WORLD_EXPLORER_RENDER_CORE_UPDATE_SINK_HPP
