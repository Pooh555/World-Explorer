#ifndef WORLD_EXPLORER_SCENE_STORE_SCENE_STORE_HPP
#define WORLD_EXPLORER_SCENE_STORE_SCENE_STORE_HPP

#include "world_explorer/scene_store/scene.hpp"
#include <deque>
#include <mutex>

namespace world_explorer {
struct scene_store_limits {
    scene_limits scene;
    std::size_t journal_entries{128};
    std::size_t remembered_operations{1024};
};
// All methods thread-safe. A successful transaction atomically publishes an
// immutable snapshot. Journal eviction requires snapshot resync for old clients.
class scene_store {
  public:
    explicit scene_store(scene_id id, scene_store_limits limits = {});
    result<scene_revision_id> commit(const scene_transaction& transaction);
    scene_snapshot acquire_snapshot() const;
    result<scene_delta_batch> changes_since(scene_revision_id revision, byte_budget budget) const;
    result<void> reset(const scene_snapshot& snapshot);

  private:
    struct committed_operation {
        operation_id operation;
        scene_revision_id base, new_revision;
        world_epoch epoch;
        std::uint64_t fingerprint{};
    };
    mutable std::mutex mutex_;
    scene_store_limits limits_;
    scene_snapshot current_;
    std::deque<committed_operation> operations_;
    std::deque<scene_revision_id> journal_;
};
} // namespace world_explorer

#endif // WORLD_EXPLORER_SCENE_STORE_SCENE_STORE_HPP
