#include "world_explorer/scene_store/scene_store.hpp"

#include "world_explorer/scene_store/world_frame.hpp"

#include <bit>
#include <limits>
#include <set>

namespace world_explorer {
namespace {

// Canonical scalar hashing intentionally excludes object padding and ownership
// addresses; a replay of independently decoded identical assets is the same op.
std::uint64_t fingerprint(const SceneTransaction& transaction) {
    std::uint64_t hash = 14695981039346656037ULL;
    const auto K_ADD = [&](std::uint64_t value) {
        for (int i { 0 }; i < 8; ++i) {
            hash ^= value & 255U;
            hash *= 1099511628211ULL;
            value >>= 8U;
        }
    };
    const auto K_TRANSFORM = [&](const SimilarityTransform& value) {
        for (double scalar : { value.m_rotation.m_w,
                              value.m_rotation.m_x,
                              value.m_rotation.m_y,
                              value.m_rotation.m_z,
                              value.m_translation.m_x,
                              value.m_translation.m_y,
                              value.m_translation.m_z,
                              value.m_scale }) {
            K_ADD(std::bit_cast<std::uint64_t>(scalar));
        }
    };
    K_ADD(transaction.m_id.m_value);
    K_ADD(transaction.m_epoch.m_value);
    K_ADD(transaction.m_base_revision.m_value);
    K_ADD(transaction.m_new_revision.m_value);
    for (const auto& change : transaction.m_changes) {
        K_ADD(static_cast<std::uint64_t>(change.m_kind));
        K_ADD(change.m_id.m_value);
        K_ADD(change.m_pose_revision.m_value);
        K_TRANSFORM(change.m_world_from_chunk);
        if (change.m_chunk) {
            const auto& chunk = *change.m_chunk;
            K_ADD(chunk.m_id.m_value);
            K_ADD(chunk.m_generation);
            K_ADD(chunk.m_pose_revision.m_value);
            K_ADD(chunk.m_geometry_valid);
            K_TRANSFORM(chunk.m_world_from_chunk);
            for (double value : { chunk.m_bounds.m_minimum.m_x,
                                 chunk.m_bounds.m_minimum.m_y,
                                 chunk.m_bounds.m_minimum.m_z,
                                 chunk.m_bounds.m_maximum.m_x,
                                 chunk.m_bounds.m_maximum.m_y,
                                 chunk.m_bounds.m_maximum.m_z }) {
                K_ADD(std::bit_cast<std::uint64_t>(value));
            }
            K_ADD(chunk.m_splats.size());
            for (const auto& splat : chunk.m_splats) {
                for (float value : splat.m_mean) {
                    K_ADD(std::bit_cast<std::uint32_t>(value));
                }
                for (float value : splat.m_scale) {
                    K_ADD(std::bit_cast<std::uint32_t>(value));
                }
                for (float value : splat.m_rotation) {
                    K_ADD(std::bit_cast<std::uint32_t>(value));
                }
                K_ADD(std::bit_cast<std::uint32_t>(splat.m_opacity));
                K_ADD(splat.m_sh_degree);
                for (float value : splat.m_sh) {
                    K_ADD(std::bit_cast<std::uint32_t>(value));
                }
            }
            for (auto frame : chunk.m_source_frames) {
                K_ADD(frame.m_value);
            }
            if (chunk.m_mesh) {
                const auto K_MESH_HASH = scene_chunk_content_hash(chunk);
                for (char value : K_MESH_HASH) {
                    K_ADD(static_cast<unsigned char>(value));
                }
            }
        }
    }
    if (transaction.m_world_frame) {
        K_ADD(0x5745584652414d45ULL);
        const auto& frame = *transaction.m_world_frame;
        K_ADD(frame.m_source_epoch.m_value);
        K_ADD(frame.m_source_revision.m_value);
        K_ADD(frame.m_synthetic);
        K_TRANSFORM(frame.m_world_from_source);
        for (char value : frame.m_document_json + frame.m_document_sha256) {
            K_ADD(static_cast<unsigned char>(value));
        }
    }
    return hash;
}

}  // namespace

SceneStore::SceneStore(SceneId id, SceneStoreLimits limits) : m_limits(limits) {
    auto initial = std::make_shared<SceneState>();
    initial->m_id = id;
    m_current = std::move(initial);
}

SceneSnapshot SceneStore::acquire_snapshot() const {
    std::lock_guard lock(m_mutex);
    return m_current;
}

Result<SceneRevisionId> SceneStore::commit(const SceneTransaction& transaction) {
    std::lock_guard lock(m_mutex);
    if (transaction.m_id != m_current->m_id || transaction.m_operation.m_value == 0) {
        return fail(ErrorCode::INVALID_ARGUMENT, "wrong scene or missing operation ID");
    }
    if (transaction.m_changes.size() > m_limits.m_scene.m_max_chunks) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "transaction exceeds operation budget");
    }
    for (const auto& change : transaction.m_changes) {
        if (change.m_kind > SceneOperationKind::TRANSFORM) {
            return fail(ErrorCode::INVALID_DATA, "unsupported scene operation kind");
        }
        if (change.m_chunk) {
            auto checked = validate_chunk(*change.m_chunk, m_limits.m_scene);
            if (!checked) {
                return std::unexpected(checked.error());
            }
        }
    }
    if (transaction.m_world_frame &&
        (transaction.m_world_frame->m_document_json.size() > K_WORLD_FRAME_DOCUMENT_BYTES ||
         transaction.m_world_frame->m_document_sha256.size() != 64uz ||
         !transaction.m_changes.empty())) {
        return fail(ErrorCode::INVALID_DATA, "Alignment must be a bounded exclusive operation");
    }
    const auto K_DIGEST = fingerprint(transaction);
    for (const auto& prior : m_operations) {
        if (prior.m_operation == transaction.m_operation) {
            if (prior.m_epoch == transaction.m_epoch &&
                prior.m_base == transaction.m_base_revision &&
                prior.m_new_revision == transaction.m_new_revision &&
                prior.m_fingerprint == K_DIGEST) {
                return prior.m_new_revision;
            }
            return fail(ErrorCode::DUPLICATE_OPERATION,
                        "operation ID was already used with different content");
        }
    }
    if (transaction.m_epoch != m_current->m_epoch) {
        return fail(ErrorCode::WRONG_EPOCH, "epoch mismatch; request full snapshot");
    }
    if (transaction.m_base_revision != m_current->m_revision) {
        return fail(ErrorCode::WRONG_BASE, "revision mismatch; request replay or snapshot");
    }
    if (transaction.m_base_revision.m_value == std::numeric_limits<std::uint64_t>::max() ||
        transaction.m_new_revision.m_value != transaction.m_base_revision.m_value + 1) {
        return fail(ErrorCode::INVALID_ARGUMENT, "transaction must advance exactly one revision");
    }
    if (m_current->m_world_frame) {
        return fail(ErrorCode::RESYNC_REQUIRED,
                    "Aligned source is immutable; corrections require a newer full snapshot");
    }
    auto next = std::make_shared<SceneState>(*m_current);
    if (transaction.m_world_frame) {
        if (next->m_epoch.m_value == std::numeric_limits<std::uint64_t>::max()) {
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "World epoch exhausted");
        }
        next->m_world_frame = transaction.m_world_frame;
        next->m_scale = transaction.m_world_frame->m_synthetic ? ScaleStatus::ARBITRARY
                                                               : ScaleStatus::METRIC_MEASURED;
        ++next->m_epoch.m_value;
    }
    std::set<ChunkId> changed;
    for (const auto& change : transaction.m_changes) {
        if (!changed.insert(change.m_id).second) {
            return fail(ErrorCode::INVALID_DATA, "multiple changes for one chunk in transaction");
        }
        auto existing = next->m_chunks.find(change.m_id);
        const bool K_EXISTS = existing != next->m_chunks.end();
        if (change.m_kind == SceneOperationKind::CREATE ||
            change.m_kind == SceneOperationKind::REPLACE) {
            if (!change.m_chunk || change.m_chunk->m_id != change.m_id) {
                return fail(ErrorCode::INVALID_DATA, "replacement chunk identity mismatch");
            }
            if ((change.m_kind == SceneOperationKind::CREATE && K_EXISTS) ||
                (change.m_kind == SceneOperationKind::REPLACE && !K_EXISTS)) {
                return fail(ErrorCode::INVALID_DATA,
                            "create/replace existence precondition failed");
            }
            if (K_EXISTS && change.m_chunk->m_generation <= existing->second->m_generation) {
                return fail(ErrorCode::INVALID_DATA, "replacement must advance chunk generation");
            }
            // Copy the caller's object: a const shared_ptr alone does not prevent
            // mutation through another non-const alias retained by its producer.
            next->m_chunks[change.m_id] = std::make_shared<const SceneChunk>(*change.m_chunk);
        } else {
            if (!K_EXISTS) {
                return fail(ErrorCode::NOT_FOUND, "changed chunk does not exist");
            }
            if (change.m_kind == SceneOperationKind::ERASE) {
                next->m_chunks.erase(existing);
            } else {
                auto replacement = std::make_shared<SceneChunk>(*existing->second);
                if (replacement->m_generation == std::numeric_limits<std::uint64_t>::max()) {
                    return fail(ErrorCode::RESOURCE_EXHAUSTED, "generation exhausted");
                }
                ++replacement->m_generation;
                replacement->m_geometry_valid = false;
                replacement->m_pose_revision = change.m_pose_revision;
                if (change.m_kind == SceneOperationKind::TRANSFORM) {
                    replacement->m_world_from_chunk = change.m_world_from_chunk;
                }
                next->m_chunks[change.m_id] = std::move(replacement);
            }
        }
    }
    next->m_revision = transaction.m_new_revision;
    auto checked = validate_scene(*next, m_limits.m_scene);
    if (!checked) {
        return std::unexpected(checked.error());
    }
    // Prepare bounded records before publication, so allocation failure cannot
    // expose a revision without its replay/idempotency record.
    m_operations.push_back({ transaction.m_operation,
                            transaction.m_base_revision,
                            transaction.m_new_revision,
                            transaction.m_epoch,
                            K_DIGEST });
    try {
        m_journal.push_back(transaction.m_base_revision);
    } catch (...) {
        m_operations.pop_back();
        throw;
    }
    if (transaction.m_world_frame) {
        m_journal.clear();
    }
    m_current = std::move(next);
    while (m_operations.size() > m_limits.m_remembered_operations) {
        m_operations.pop_front();
    }
    while (m_journal.size() > m_limits.m_journal_entries) {
        m_journal.pop_front();
    }
    return transaction.m_new_revision;
}

Result<SceneDeltaBatch> SceneStore::changes_since(SceneRevisionId revision,
                                                  ByteBudget budget) const {
    std::lock_guard lock(m_mutex);
    if (revision.m_value > m_current->m_revision.m_value ||
        (revision != m_current->m_revision &&
         (m_journal.empty() || revision.m_value < m_journal.front().m_value))) {
        return fail(ErrorCode::RESYNC_REQUIRED, "revision outside retained replay window");
    }
    auto bytes = validate_scene(*m_current, m_limits.m_scene);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    if (*bytes > budget.m_bytes) {
        return fail(ErrorCode::WOULD_BLOCK, "snapshot exceeds replay byte budget");
    }
    const auto K_OPERATION = m_operations.empty() ? OperationId { m_current->m_revision.m_value + 1 }
                                                  : m_operations.back().m_operation;
    return SceneDeltaBatch { m_current->m_id,
                            m_current->m_epoch,
                            revision,
                            m_current->m_revision,
                            K_OPERATION,
                            m_current,
                            false,
                            *bytes };
}

Result<void> SceneStore::reset(const SceneSnapshot& snapshot) {
    if (!snapshot) {
        return fail(ErrorCode::INVALID_ARGUMENT, "missing reset snapshot");
    }
    auto checked = validate_scene(*snapshot, m_limits.m_scene);
    if (!checked) {
        return std::unexpected(checked.error());
    }
    auto owned = std::make_shared<SceneState>(*snapshot);
    for (auto& [K_ID, chunk] : owned->m_chunks) {
        (void)K_ID;
        chunk = std::make_shared<const SceneChunk>(*chunk);
    }
    std::lock_guard lock(m_mutex);
    if (owned->m_id != m_current->m_id || owned->m_epoch.m_value <= m_current->m_epoch.m_value) {
        return fail(ErrorCode::WRONG_EPOCH, "reset requires same scene and a newer world epoch");
    }
    m_current = std::move(owned);
    m_operations.clear();
    m_journal.clear();
    return {};
}

}  // namespace world_explorer
