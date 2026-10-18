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

enum class UpdateState : std::uint8_t {
    ACCEPTED,
    ASSETS_AVAILABLE,
    UPLOADED,
    RENDER_VISIBLE,
    RETIRED,
    CANCELLED,
    FAILED
};

struct UpdateProgress {
    UpdateState m_state { UpdateState::FAILED };
    SceneRevisionId m_revision {};
    std::uint64_t m_timeline_value {};
    std::optional<Error> m_failure;
};

struct ResidencyBudget {
    std::size_t m_bytes { 512uz * 1024uz * 1024uz };
    std::size_t m_max_splats { 250000uz };
    std::size_t m_max_pending { 4uz };
};

struct AdmittedUpdate {
    UpdateTicket m_ticket;
    SceneDeltaBatch m_batch;
};

// Prepared off the render admission path. The snapshot remains immutable for the
// lifetime of this value; a failed enqueue leaves the value available for retry.
class PreparedSceneUpdate {
  public:
    PreparedSceneUpdate(const PreparedSceneUpdate&) = delete;
    PreparedSceneUpdate& operator=(const PreparedSceneUpdate&) = delete;
    PreparedSceneUpdate(PreparedSceneUpdate&&) noexcept = default;
    PreparedSceneUpdate& operator=(PreparedSceneUpdate&&) noexcept = default;

  private:
    friend class RendererUpdateSink;

    PreparedSceneUpdate(SceneDeltaBatch batch,
                        std::size_t bytes,
                        std::string hash,
                        ResidencyBudget budget)
        : m_batch(std::move(batch)), m_bytes(bytes), m_hash(std::move(hash)), m_budget(budget) { }

    SceneDeltaBatch m_batch;
    std::size_t m_bytes {};
    std::string m_hash;
    ResidencyBudget m_budget;
};

// Multi-producer, single render-consumer. Snapshots are immutable. Failed enqueue
// preserves its argument. Tickets and duplicate operations are retained for a
// bounded 256-operation replay window; consumers must resync after losing it.
class RendererUpdateSink {
  public:
    explicit RendererUpdateSink(ResidencyBudget budget = {});
    Result<PreparedSceneUpdate> prepare(const SceneDeltaBatch& batch) const;
    Result<UpdateTicket> enqueue(PreparedSceneUpdate& prepared);
    Result<UpdateTicket> enqueue(SceneDeltaBatch& batch);
    Result<UpdateProgress> query_progress(UpdateTicket ticket) const;
    Result<void> set_residency_budget(ResidencyBudget budget);
    void cancel_pending();
    SceneSnapshot visible_snapshot() const;
    std::size_t queued_bytes() const;
    // Render-owner lifecycle operations. GPU timeline completion governs release.
    std::optional<AdmittedUpdate> take_next();
    void acknowledge_uploaded(UpdateTicket ticket, std::uint64_t timeline);
    void acknowledge_visible(UpdateTicket ticket, SceneSnapshot snapshot, std::uint64_t timeline);
    void retire_completed(std::uint64_t completed_timeline);
    void fail_update(UpdateTicket ticket, Error failure);

  private:
    struct Record {
        UpdateTicket m_ticket;
        OperationId m_operation;
        SceneId m_scene;
        WorldEpoch m_epoch;
        SceneRevisionId m_base;
        UpdateProgress m_progress;
        std::size_t m_bytes {};
        std::string m_content_hash;
    };

    void cancel_pending_locked();
    Record* find(UpdateTicket ticket);
    std::string fingerprint(const SceneState& scene) const;
    std::string chunk_fingerprint(const std::shared_ptr<const SceneChunk>& chunk) const;

    struct CachedChunkFingerprint {
        std::weak_ptr<const SceneChunk> m_owner;
        std::string m_hash;
    };

    mutable std::mutex m_fingerprint_mutex;
    mutable std::deque<CachedChunkFingerprint> m_fingerprint_cache;
    mutable std::mutex m_mutex;
    ResidencyBudget m_budget;
    std::deque<AdmittedUpdate> m_pending;
    std::deque<Record> m_history;
    SceneSnapshot m_visible;
    std::optional<UpdateTicket> m_visible_ticket;
    std::optional<UpdateTicket> m_active_ticket;
    SceneId m_accepted_scene;
    WorldEpoch m_accepted_epoch;
    SceneRevisionId m_accepted_revision;
    std::size_t m_queued_bytes {};
    std::size_t m_retired_bytes {};
    std::uint64_t m_next_ticket { 1 };
};

}  // namespace world_explorer

#endif  // WORLD_EXPLORER_RENDER_CORE_UPDATE_SINK_HPP
