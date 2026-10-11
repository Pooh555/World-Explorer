#include "world_explorer/scene_store/scene_store.hpp"
#include <bit>
#include <limits>
#include <set>

namespace world_explorer {
namespace {
// Canonical scalar hashing intentionally excludes object padding and ownership
// addresses; a replay of independently decoded identical assets is the same op.
std::uint64_t fingerprint(const scene_transaction& transaction) {
    std::uint64_t hash = 14695981039346656037ULL;
    const auto add = [&](std::uint64_t value) {
        for (int i = 0; i < 8; ++i) {
            hash ^= value & 255;
            hash *= 1099511628211ULL;
            value >>= 8;
        }
    };
    const auto transform = [&](const similarity_transform& value) {
        for (double scalar :
             {value.rotation.w, value.rotation.x, value.rotation.y, value.rotation.z,
              value.translation.x, value.translation.y, value.translation.z, value.scale})
            add(std::bit_cast<std::uint64_t>(scalar));
    };
    add(transaction.id.value);
    add(transaction.epoch.value);
    add(transaction.base_revision.value);
    add(transaction.new_revision.value);
    for (const auto& change : transaction.changes) {
        add(static_cast<std::uint64_t>(change.kind));
        add(change.id.value);
        add(change.pose_revision.value);
        transform(change.world_from_chunk);
        if (change.chunk) {
            const auto& chunk = *change.chunk;
            add(chunk.id.value);
            add(chunk.generation);
            add(chunk.pose_revision.value);
            add(chunk.geometry_valid);
            transform(chunk.world_from_chunk);
            for (double value :
                 {chunk.bounds.minimum.x, chunk.bounds.minimum.y, chunk.bounds.minimum.z,
                  chunk.bounds.maximum.x, chunk.bounds.maximum.y, chunk.bounds.maximum.z})
                add(std::bit_cast<std::uint64_t>(value));
            add(chunk.splats.size());
            for (const auto& splat : chunk.splats) {
                for (float value : splat.mean)
                    add(std::bit_cast<std::uint32_t>(value));
                for (float value : splat.scale)
                    add(std::bit_cast<std::uint32_t>(value));
                for (float value : splat.rotation)
                    add(std::bit_cast<std::uint32_t>(value));
                add(std::bit_cast<std::uint32_t>(splat.opacity));
                add(splat.sh_degree);
                for (float value : splat.sh)
                    add(std::bit_cast<std::uint32_t>(value));
            }
            for (auto frame : chunk.source_frames)
                add(frame.value);
        }
    }
    return hash;
}
} // namespace
scene_store::scene_store(scene_id id, scene_store_limits limits) : limits_(limits) {
    auto initial = std::make_shared<scene_state>();
    initial->id = id;
    current_ = std::move(initial);
}
scene_snapshot scene_store::acquire_snapshot() const {
    std::lock_guard lock(mutex_);
    return current_;
}
result<scene_revision_id> scene_store::commit(const scene_transaction& transaction) {
    std::lock_guard lock(mutex_);
    if (transaction.id != current_->id || transaction.operation.value == 0)
        return fail(error_code::INVALID_ARGUMENT, "wrong scene or missing operation ID");
    if (transaction.changes.size() > limits_.scene.max_chunks)
        return fail(error_code::RESOURCE_EXHAUSTED, "transaction exceeds operation budget");
    for (const auto& change : transaction.changes)
        if (change.chunk) {
            auto checked = validate_chunk(*change.chunk, limits_.scene);
            if (!checked)
                return std::unexpected(checked.error());
        }
    const auto digest = fingerprint(transaction);
    for (const auto& prior : operations_)
        if (prior.operation == transaction.operation) {
            if (prior.epoch == transaction.epoch && prior.base == transaction.base_revision &&
                prior.new_revision == transaction.new_revision && prior.fingerprint == digest)
                return prior.new_revision;
            return fail(error_code::DUPLICATE_OPERATION,
                        "operation ID was already used with different content");
        }
    if (transaction.epoch != current_->epoch)
        return fail(error_code::WRONG_EPOCH, "epoch mismatch; request full snapshot");
    if (transaction.base_revision != current_->revision)
        return fail(error_code::WRONG_BASE, "revision mismatch; request replay or snapshot");
    if (transaction.base_revision.value == std::numeric_limits<std::uint64_t>::max() ||
        transaction.new_revision.value != transaction.base_revision.value + 1)
        return fail(error_code::INVALID_ARGUMENT, "transaction must advance exactly one revision");
    auto next = std::make_shared<scene_state>(*current_);
    std::set<chunk_id> changed;
    for (const auto& change : transaction.changes) {
        if (!changed.insert(change.id).second)
            return fail(error_code::INVALID_DATA, "multiple changes for one chunk in transaction");
        auto existing = next->chunks.find(change.id);
        const bool exists = existing != next->chunks.end();
        if (change.kind == scene_operation_kind::CREATE ||
            change.kind == scene_operation_kind::REPLACE) {
            if (!change.chunk || change.chunk->id != change.id)
                return fail(error_code::INVALID_DATA, "replacement chunk identity mismatch");
            if ((change.kind == scene_operation_kind::CREATE && exists) ||
                (change.kind == scene_operation_kind::REPLACE && !exists))
                return fail(error_code::INVALID_DATA,
                            "create/replace existence precondition failed");
            if (exists && change.chunk->generation <= existing->second->generation)
                return fail(error_code::INVALID_DATA, "replacement must advance chunk generation");
            // Copy the caller's object: a const shared_ptr alone does not prevent
            // mutation through another non-const alias retained by its producer.
            next->chunks[change.id] = std::make_shared<const scene_chunk>(*change.chunk);
        } else {
            if (!exists)
                return fail(error_code::NOT_FOUND, "changed chunk does not exist");
            if (change.kind == scene_operation_kind::ERASE)
                next->chunks.erase(existing);
            else {
                auto replacement = std::make_shared<scene_chunk>(*existing->second);
                if (replacement->generation == std::numeric_limits<std::uint64_t>::max())
                    return fail(error_code::RESOURCE_EXHAUSTED, "generation exhausted");
                ++replacement->generation;
                replacement->geometry_valid = false;
                replacement->pose_revision = change.pose_revision;
                if (change.kind == scene_operation_kind::TRANSFORM)
                    replacement->world_from_chunk = change.world_from_chunk;
                next->chunks[change.id] = std::move(replacement);
            }
        }
    }
    next->revision = transaction.new_revision;
    auto checked = validate_scene(*next, limits_.scene);
    if (!checked)
        return std::unexpected(checked.error());
    // Prepare bounded records before publication, so allocation failure cannot
    // expose a revision without its replay/idempotency record.
    operations_.push_back({transaction.operation, transaction.base_revision,
                           transaction.new_revision, transaction.epoch, digest});
    try {
        journal_.push_back(transaction.base_revision);
    } catch (...) {
        operations_.pop_back();
        throw;
    }
    current_ = std::move(next);
    while (operations_.size() > limits_.remembered_operations)
        operations_.pop_front();
    while (journal_.size() > limits_.journal_entries)
        journal_.pop_front();
    return transaction.new_revision;
}
result<scene_delta_batch> scene_store::changes_since(scene_revision_id revision,
                                                     byte_budget budget) const {
    std::lock_guard lock(mutex_);
    if (revision.value > current_->revision.value ||
        (revision != current_->revision &&
         (journal_.empty() || revision.value < journal_.front().value)))
        return fail(error_code::RESYNC_REQUIRED, "revision outside retained replay window");
    auto bytes = validate_scene(*current_, limits_.scene);
    if (!bytes)
        return std::unexpected(bytes.error());
    if (*bytes > budget.bytes)
        return fail(error_code::WOULD_BLOCK, "snapshot exceeds replay byte budget");
    const auto operation = operations_.empty() ? operation_id{current_->revision.value + 1}
                                               : operations_.back().operation;
    return scene_delta_batch{current_->id, current_->epoch, revision, current_->revision,
                             operation,    current_,        false,    *bytes};
}
result<void> scene_store::reset(scene_snapshot snapshot) {
    if (!snapshot)
        return fail(error_code::INVALID_ARGUMENT, "missing reset snapshot");
    auto checked = validate_scene(*snapshot, limits_.scene);
    if (!checked)
        return std::unexpected(checked.error());
    auto owned = std::make_shared<scene_state>(*snapshot);
    for (auto& [id, chunk] : owned->chunks) {
        (void)id;
        chunk = std::make_shared<const scene_chunk>(*chunk);
    }
    std::lock_guard lock(mutex_);
    if (owned->id != current_->id || owned->epoch.value <= current_->epoch.value)
        return fail(error_code::WRONG_EPOCH, "reset requires same scene and a newer world epoch");
    current_ = std::move(owned);
    operations_.clear();
    journal_.clear();
    return {};
}
} // namespace world_explorer
