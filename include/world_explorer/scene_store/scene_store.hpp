#ifndef WORLD_EXPLORER_SCENE_STORE_SCENE_STORE_HPP
#define WORLD_EXPLORER_SCENE_STORE_SCENE_STORE_HPP

#include "world_explorer/scene_store/scene.hpp"

#include <deque>
#include <mutex>

namespace world_explorer {

struct SceneStoreLimits {
    SceneLimits m_scene;
    std::size_t m_journal_entries { 128uz };
    std::size_t m_remembered_operations { 1024uz };
};

// All methods thread-safe. A successful transaction atomically publishes an
// immutable snapshot. Journal eviction requires snapshot resync for old clients.
class SceneStore {
  public:
    explicit SceneStore(SceneId id, SceneStoreLimits limits = {});
    Result<SceneRevisionId> commit(const SceneTransaction& transaction);
    SceneSnapshot acquire_snapshot() const;
    Result<SceneDeltaBatch> changes_since(SceneRevisionId revision, ByteBudget budget) const;
    Result<void> reset(const SceneSnapshot& snapshot);

  private:
    struct CommittedOperation {
        OperationId m_operation;
        SceneRevisionId m_base, m_new_revision;
        WorldEpoch m_epoch;
        std::uint64_t m_fingerprint {};
    };

    mutable std::mutex m_mutex;
    SceneStoreLimits m_limits;
    SceneSnapshot m_current;
    std::deque<CommittedOperation> m_operations;
    std::deque<SceneRevisionId> m_journal;
};

}  // namespace world_explorer

#endif  // WORLD_EXPLORER_SCENE_STORE_SCENE_STORE_HPP
