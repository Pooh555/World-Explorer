#include "world_explorer/render_core/update_sink.hpp"

#include "world_explorer/core/hash.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace world_explorer {
namespace {

void add_u64(Sha256Builder& hash, std::uint64_t value) {
    std::array<std::byte, 8> bytes {};
    for (unsigned index { 0 }; index < 8; ++index)
        bytes[index] = static_cast<std::byte>((value >> (8U * index)) & 255U);
    hash.update(bytes);
}

void add_string(Sha256Builder& hash, const std::string& value) {
    add_u64(hash, value.size());
    hash.update(std::as_bytes(std::span(value.data(), value.size())));
}

}  // namespace

RendererUpdateSink::RendererUpdateSink(ResidencyBudget budget) : m_budget(budget) {
}

RendererUpdateSink::Record* RendererUpdateSink::find(UpdateTicket ticket) {
    const auto K_FOUND = std::find_if(m_history.begin(), m_history.end(), [&](const auto& item) {
        return item.m_ticket == ticket;
    });
    return K_FOUND == m_history.end() ? nullptr : &*K_FOUND;
}

std::string
RendererUpdateSink::chunk_fingerprint(const std::shared_ptr<const SceneChunk>& chunk) const {
    {
        std::lock_guard lock(m_fingerprint_mutex);
        for (const auto& entry : m_fingerprint_cache)
            if (entry.m_owner.lock() == chunk) return entry.m_hash;
    }
    auto hash = scene_chunk_content_hash(*chunk);
    std::lock_guard lock(m_fingerprint_mutex);
    for (const auto& entry : m_fingerprint_cache)
        if (entry.m_owner.lock() == chunk) return entry.m_hash;
    if (m_fingerprint_cache.size() >= 4096) {
        std::erase_if(m_fingerprint_cache,
                      [](const auto& entry) { return entry.m_owner.expired(); });
        if (m_fingerprint_cache.size() >= 4096) m_fingerprint_cache.pop_front();
    }
    m_fingerprint_cache.push_back({chunk, hash});
    return hash;
}

std::string RendererUpdateSink::fingerprint(const SceneState& scene) const {
    Sha256Builder hash;
    add_u64(hash, scene.m_id.m_value);
    add_u64(hash, scene.m_epoch.m_value);
    add_u64(hash, scene.m_revision.m_value);
    add_u64(hash, static_cast<std::uint64_t>(scene.m_scale));
    add_u64(hash, scene.m_provenance.size());
    for (const auto& [K_KEY, K_VALUE] : scene.m_provenance) {
        add_string(hash, K_KEY);
        add_string(hash, K_VALUE);
    }
    add_u64(hash, scene.m_chunks.size());
    for (const auto& [K_ID, K_CHUNK] : scene.m_chunks) {
        add_u64(hash, K_ID.m_value);
        const auto K_CHUNK_HASH = chunk_fingerprint(K_CHUNK);
        hash.update(std::as_bytes(std::span(K_CHUNK_HASH.data(), K_CHUNK_HASH.size())));
    }
    return hash.finish();
}

Result<PreparedSceneUpdate> RendererUpdateSink::prepare(const SceneDeltaBatch& batch) const {
    if (!batch.m_snapshot) return fail(ErrorCode::INVALID_DATA, "Missing renderer snapshot");
    ResidencyBudget validated_budget;
    {
        std::lock_guard lock(m_mutex);
        validated_budget = m_budget;
    }
    auto bytes =
        validate_scene(*batch.m_snapshot,
                       {4096, validated_budget.m_max_splats, validated_budget.m_bytes, 1000000});
    if (!bytes) return std::unexpected(bytes.error());
    return PreparedSceneUpdate(batch, *bytes, fingerprint(*batch.m_snapshot), validated_budget);
}

Result<UpdateTicket> RendererUpdateSink::enqueue(SceneDeltaBatch& batch) {
    if (!batch.m_snapshot) return fail(ErrorCode::INVALID_DATA, "Missing renderer snapshot");
    {
        std::lock_guard lock(m_mutex);
        const auto K_DUPLICATE =
            std::find_if(m_history.begin(), m_history.end(), [&](const auto& item) {
                return item.m_operation == batch.m_operation;
            });
        if (K_DUPLICATE == m_history.end()) {
            // A new operation cannot enter a full queue. Keep this check ahead of
            // full-snapshot validation and hashing so a retry cannot starve the
            // render consumer while it waits for capacity.
            if (m_pending.size() >= m_budget.m_max_pending)
                return fail(ErrorCode::WOULD_BLOCK, "renderer pending update queue is full");
            std::size_t estimated_bytes = 0;
            bool can_estimate = true;
            for (const auto& [K_ID, K_CHUNK] : batch.m_snapshot->m_chunks) {
                static_cast<void>(K_ID);
                if (!K_CHUNK) {
                    can_estimate = false;
                    break;
                }
                if (K_CHUNK->m_splats.size() >
                    (m_budget.m_bytes - estimated_bytes) / sizeof(Gaussian)) {
                    can_estimate = false;
                    break;
                }
                estimated_bytes += K_CHUNK->m_splats.size() * sizeof(Gaussian);
            }
            if (can_estimate) {
                const auto* visible = m_visible_ticket ? find(*m_visible_ticket) : nullptr;
                const auto K_RESIDENT = (visible ? visible->m_bytes : 0) + m_retired_bytes;
                if (m_queued_bytes > m_budget.m_bytes ||
                    estimated_bytes > m_budget.m_bytes - m_queued_bytes ||
                    K_RESIDENT > m_budget.m_bytes - m_queued_bytes - estimated_bytes)
                    return fail(ErrorCode::WOULD_BLOCK,
                                "renderer upload or replacement residency budget is full");
            }
        }
    }
    auto prepared = prepare(batch);
    if (!prepared) return std::unexpected(prepared.error());
    auto ticket = enqueue(*prepared);
    if (ticket && !prepared->m_batch.m_snapshot) batch = {};
    return ticket;
}

Result<UpdateTicket> RendererUpdateSink::enqueue(PreparedSceneUpdate& prepared) {
    auto& batch = prepared.m_batch;
    if (!batch.m_snapshot) return fail(ErrorCode::INVALID_DATA, "Missing renderer snapshot");
    std::lock_guard lock(m_mutex);
    const auto K_DUPLICATE =
        std::find_if(m_history.begin(), m_history.end(), [&](const auto& item) {
            return item.m_operation == batch.m_operation;
        });
    if (K_DUPLICATE != m_history.end()) {
        if (K_DUPLICATE->m_scene != batch.m_id || K_DUPLICATE->m_epoch != batch.m_epoch ||
            K_DUPLICATE->m_base != batch.m_base_revision ||
            K_DUPLICATE->m_progress.m_revision != batch.m_new_revision ||
            K_DUPLICATE->m_content_hash != prepared.m_hash)
            return fail(ErrorCode::DUPLICATE_OPERATION,
                        "renderer operation ID reused for a different revision");
        return K_DUPLICATE->m_ticket;
    }
    if (m_budget.m_max_splats != prepared.m_budget.m_max_splats ||
        m_budget.m_bytes != prepared.m_budget.m_bytes)
        return fail(ErrorCode::WOULD_BLOCK,
                    "renderer residency budget changed during update validation; retry");
    if (!batch.m_snapshot || batch.m_operation.m_value == 0 ||
        batch.m_snapshot->m_id != batch.m_id || batch.m_snapshot->m_epoch != batch.m_epoch ||
        batch.m_snapshot->m_revision != batch.m_new_revision)
        return fail(ErrorCode::INVALID_DATA,
                    "renderer batch metadata does not match its immutable snapshot");
    // Full snapshots may establish a new epoch only when earlier uploads have drained.
    if (batch.m_full_snapshot) {
        if (m_queued_bytes != 0 || !m_pending.empty())
            return fail(ErrorCode::WOULD_BLOCK, "resync waits for pending renderer updates");
    } else {
        if (batch.m_id != m_accepted_scene || batch.m_epoch != m_accepted_epoch)
            return fail(ErrorCode::WRONG_EPOCH,
                        "renderer requires a full snapshot for this scene epoch");
        if (batch.m_base_revision != m_accepted_revision)
            return fail(ErrorCode::RESYNC_REQUIRED,
                        "renderer missed the batch base revision; send a full snapshot");
        if (batch.m_new_revision.m_value <= batch.m_base_revision.m_value)
            return fail(ErrorCode::INVALID_DATA, "renderer revisions must increase");
    }
    const auto* visible = m_visible_ticket ? find(*m_visible_ticket) : nullptr;
    const std::size_t K_RESIDENT = (visible ? visible->m_bytes : 0) + m_retired_bytes;
    if (m_pending.size() >= m_budget.m_max_pending || prepared.m_bytes > m_budget.m_bytes ||
        m_queued_bytes > m_budget.m_bytes - prepared.m_bytes ||
        K_RESIDENT > m_budget.m_bytes - prepared.m_bytes - m_queued_bytes)
        return fail(ErrorCode::WOULD_BLOCK,
                    "renderer upload or replacement residency budget is full");
    while (m_history.size() >= 256) {
        const auto K_STATE = m_history.front().m_progress.m_state;
        if (K_STATE != UpdateState::RETIRED && K_STATE != UpdateState::CANCELLED &&
            K_STATE != UpdateState::FAILED)
            return fail(ErrorCode::WOULD_BLOCK, "renderer acknowledgement replay window is full");
        m_history.pop_front();
    }
    const UpdateTicket K_TICKET { m_next_ticket++ };
    m_history.push_back({K_TICKET,
                         batch.m_operation,
                         batch.m_id,
                         batch.m_epoch,
                         batch.m_base_revision,
                         {UpdateState::ACCEPTED, batch.m_new_revision, 0, {}},
                         prepared.m_bytes,
                         prepared.m_hash});
    m_accepted_scene = batch.m_id;
    m_accepted_epoch = batch.m_epoch;
    m_accepted_revision = batch.m_new_revision;
    m_queued_bytes += prepared.m_bytes;
    batch.m_upload_bytes = prepared.m_bytes;
    m_pending.push_back({K_TICKET, std::move(batch)});
    batch = {};
    return K_TICKET;
}

Result<UpdateProgress> RendererUpdateSink::query_progress(UpdateTicket ticket) const {
    std::lock_guard lock(m_mutex);
    for (const auto& item : m_history)
        if (item.m_ticket == ticket) return item.m_progress;
    return fail(ErrorCode::NOT_FOUND,
                "renderer ticket absent or outside the 256-operation replay window");
}

Result<void> RendererUpdateSink::set_residency_budget(ResidencyBudget budget) {
    std::lock_guard lock(m_mutex);
    if (!budget.m_bytes || !budget.m_max_splats || !budget.m_max_pending)
        return fail(ErrorCode::INVALID_ARGUMENT, "renderer budgets must be positive");
    if (m_queued_bytes > budget.m_bytes || m_retired_bytes > budget.m_bytes - m_queued_bytes)
        return fail(ErrorCode::WOULD_BLOCK, "new renderer budget is smaller than pending uploads");
    if (m_visible) {
        auto size =
            validate_scene(*m_visible, {4096, budget.m_max_splats, budget.m_bytes, 1000000});
        if (!size || *size > budget.m_bytes - m_queued_bytes - m_retired_bytes)
            return fail(ErrorCode::WOULD_BLOCK,
                        "new budget cannot hold visible and pending snapshots");
    }
    m_budget = budget;
    return {};
}

void RendererUpdateSink::cancel_pending_locked() {
    for (const auto& item : m_pending)
        if (auto* entry = find(item.m_ticket)) {
            entry->m_progress.m_state = UpdateState::CANCELLED;
            m_queued_bytes -= entry->m_bytes;
        }
    m_pending.clear();
    m_accepted_scene = m_visible ? m_visible->m_id : SceneId {};
    m_accepted_epoch = m_visible ? m_visible->m_epoch : WorldEpoch {};
    m_accepted_revision = m_visible ? m_visible->m_revision : SceneRevisionId {};
    if (m_active_ticket) {
        const auto* active = find(*m_active_ticket);
        m_accepted_scene = active->m_scene;
        m_accepted_epoch = active->m_epoch;
        m_accepted_revision = active->m_progress.m_revision;
    }
}

void RendererUpdateSink::cancel_pending() {
    std::lock_guard lock(m_mutex);
    cancel_pending_locked();
}

SceneSnapshot RendererUpdateSink::visible_snapshot() const {
    std::lock_guard lock(m_mutex);
    return m_visible;
}

std::size_t RendererUpdateSink::queued_bytes() const {
    std::lock_guard lock(m_mutex);
    return m_queued_bytes;
}

std::optional<AdmittedUpdate> RendererUpdateSink::take_next() {
    std::lock_guard lock(m_mutex);
    if (m_pending.empty() || m_active_ticket) return {};
    auto update = std::move(m_pending.front());
    m_pending.pop_front();
    find(update.m_ticket)->m_progress.m_state = UpdateState::ASSETS_AVAILABLE;
    m_active_ticket = update.m_ticket;
    return update;
}

void RendererUpdateSink::acknowledge_uploaded(UpdateTicket ticket, std::uint64_t timeline) {
    std::lock_guard lock(m_mutex);
    if (auto* item = find(ticket); item && m_active_ticket == ticket &&
                                   item->m_progress.m_state == UpdateState::ASSETS_AVAILABLE) {
        item->m_progress.m_state = UpdateState::UPLOADED;
        item->m_progress.m_timeline_value = timeline;
    }
}

void RendererUpdateSink::acknowledge_visible(UpdateTicket ticket,
                                             SceneSnapshot snapshot,
                                             std::uint64_t timeline) {
    std::lock_guard lock(m_mutex);
    auto* incoming = find(ticket);
    if (!incoming || m_active_ticket != ticket ||
        incoming->m_progress.m_state != UpdateState::UPLOADED || !snapshot ||
        snapshot->m_id != incoming->m_scene || snapshot->m_epoch != incoming->m_epoch ||
        snapshot->m_revision != incoming->m_progress.m_revision)
        return;
    if (m_visible_ticket)
        if (auto* previous = find(*m_visible_ticket)) {
            previous->m_progress.m_timeline_value = timeline;
            m_retired_bytes += previous->m_bytes;
        }
    if (auto* item = find(ticket)) {
        item->m_progress.m_state = UpdateState::RENDER_VISIBLE;
        item->m_progress.m_timeline_value = timeline;
        m_queued_bytes -= item->m_bytes;
    }
    m_visible = std::move(snapshot);
    m_visible_ticket = ticket;
    m_active_ticket.reset();
}

void RendererUpdateSink::retire_completed(std::uint64_t completed_timeline) {
    std::lock_guard lock(m_mutex);
    for (auto& item : m_history)
        if (item.m_progress.m_state == UpdateState::RENDER_VISIBLE &&
            item.m_ticket != m_visible_ticket &&
            item.m_progress.m_timeline_value <= completed_timeline) {
            item.m_progress.m_state = UpdateState::RETIRED;
            m_retired_bytes -= item.m_bytes;
        }
}

void RendererUpdateSink::fail_update(UpdateTicket ticket, Error failure) {
    std::lock_guard lock(m_mutex);
    if (auto* item = find(ticket); item && m_active_ticket == ticket) {
        item->m_progress.m_state = UpdateState::FAILED;
        item->m_progress.m_failure = std::move(failure);
        m_queued_bytes -= item->m_bytes;
        m_active_ticket.reset();
    }
    cancel_pending_locked();
}

}  // namespace world_explorer
