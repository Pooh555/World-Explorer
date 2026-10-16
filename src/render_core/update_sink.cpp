#include "world_explorer/render_core/update_sink.hpp"
#include "world_explorer/core/hash.hpp"
#include <algorithm>
#include <array>
#include <utility>

namespace world_explorer {
namespace {
void add_u64(sha256_builder& hash, std::uint64_t value) {
    std::array<std::byte, 8> bytes{};
    for (unsigned index = 0; index < 8; ++index)
        bytes[index] = static_cast<std::byte>((value >> (8U * index)) & 255U);
    hash.update(bytes);
}
void add_string(sha256_builder& hash, const std::string& value) {
    add_u64(hash, value.size());
    hash.update(std::as_bytes(std::span(value.data(), value.size())));
}
} // namespace
renderer_update_sink::renderer_update_sink(residency_budget budget) : budget_(budget) {}
renderer_update_sink::record* renderer_update_sink::find(update_ticket ticket) {
    const auto found = std::find_if(history_.begin(), history_.end(),
                                    [&](const auto& item) { return item.ticket == ticket; });
    return found == history_.end() ? nullptr : &*found;
}
std::string
renderer_update_sink::chunk_fingerprint(const std::shared_ptr<const scene_chunk>& chunk) const {
    {
        std::lock_guard lock(fingerprint_mutex_);
        for (const auto& entry : fingerprint_cache_)
            if (entry.owner.lock() == chunk)
                return entry.hash;
    }
    auto hash = scene_chunk_content_hash(*chunk);
    std::lock_guard lock(fingerprint_mutex_);
    for (const auto& entry : fingerprint_cache_)
        if (entry.owner.lock() == chunk)
            return entry.hash;
    if (fingerprint_cache_.size() >= 4096) {
        std::erase_if(fingerprint_cache_, [](const auto& entry) { return entry.owner.expired(); });
        if (fingerprint_cache_.size() >= 4096)
            fingerprint_cache_.pop_front();
    }
    fingerprint_cache_.push_back({chunk, hash});
    return hash;
}
std::string renderer_update_sink::fingerprint(const scene_state& scene) const {
    sha256_builder hash;
    add_u64(hash, scene.id.value);
    add_u64(hash, scene.epoch.value);
    add_u64(hash, scene.revision.value);
    add_u64(hash, static_cast<std::uint64_t>(scene.scale));
    add_u64(hash, scene.provenance.size());
    for (const auto& [key, value] : scene.provenance) {
        add_string(hash, key);
        add_string(hash, value);
    }
    add_u64(hash, scene.chunks.size());
    for (const auto& [id, chunk] : scene.chunks) {
        add_u64(hash, id.value);
        const auto chunk_hash = chunk_fingerprint(chunk);
        hash.update(std::as_bytes(std::span(chunk_hash.data(), chunk_hash.size())));
    }
    return hash.finish();
}
result<prepared_scene_update> renderer_update_sink::prepare(const scene_delta_batch& batch) const {
    if (!batch.snapshot)
        return fail(error_code::INVALID_DATA, "Missing renderer snapshot");
    residency_budget validated_budget;
    {
        std::lock_guard lock(mutex_);
        validated_budget = budget_;
    }
    auto bytes = validate_scene(
        *batch.snapshot, {4096, validated_budget.max_splats, validated_budget.bytes, 1000000});
    if (!bytes)
        return std::unexpected(bytes.error());
    return prepared_scene_update(batch, *bytes, fingerprint(*batch.snapshot), validated_budget);
}
result<update_ticket> renderer_update_sink::enqueue(scene_delta_batch& batch) {
    if (!batch.snapshot)
        return fail(error_code::INVALID_DATA, "Missing renderer snapshot");
    {
        std::lock_guard lock(mutex_);
        const auto duplicate =
            std::find_if(history_.begin(), history_.end(),
                         [&](const auto& item) { return item.operation == batch.operation; });
        if (duplicate == history_.end()) {
            // A new operation cannot enter a full queue. Keep this check ahead of
            // full-snapshot validation and hashing so a retry cannot starve the
            // render consumer while it waits for capacity.
            if (pending_.size() >= budget_.max_pending)
                return fail(error_code::WOULD_BLOCK, "renderer pending update queue is full");
            std::size_t estimated_bytes = 0;
            bool can_estimate = true;
            for (const auto& [id, chunk] : batch.snapshot->chunks) {
                static_cast<void>(id);
                if (!chunk) {
                    can_estimate = false;
                    break;
                }
                if (chunk->splats.size() > (budget_.bytes - estimated_bytes) / sizeof(gaussian)) {
                    can_estimate = false;
                    break;
                }
                estimated_bytes += chunk->splats.size() * sizeof(gaussian);
            }
            if (can_estimate) {
                const auto* visible = visible_ticket_ ? find(*visible_ticket_) : nullptr;
                const auto resident = (visible ? visible->bytes : 0) + retired_bytes_;
                if (queued_bytes_ > budget_.bytes ||
                    estimated_bytes > budget_.bytes - queued_bytes_ ||
                    resident > budget_.bytes - queued_bytes_ - estimated_bytes)
                    return fail(error_code::WOULD_BLOCK,
                                "renderer upload or replacement residency budget is full");
            }
        }
    }
    auto prepared = prepare(batch);
    if (!prepared)
        return std::unexpected(prepared.error());
    auto ticket = enqueue(*prepared);
    if (ticket && !prepared->batch_.snapshot)
        batch = {};
    return ticket;
}
result<update_ticket> renderer_update_sink::enqueue(prepared_scene_update& prepared) {
    auto& batch = prepared.batch_;
    if (!batch.snapshot)
        return fail(error_code::INVALID_DATA, "Missing renderer snapshot");
    std::lock_guard lock(mutex_);
    const auto duplicate = std::find_if(history_.begin(), history_.end(), [&](const auto& item) {
        return item.operation == batch.operation;
    });
    if (duplicate != history_.end()) {
        if (duplicate->scene != batch.id || duplicate->epoch != batch.epoch ||
            duplicate->base != batch.base_revision ||
            duplicate->progress.revision != batch.new_revision ||
            duplicate->content_hash != prepared.hash_)
            return fail(error_code::DUPLICATE_OPERATION,
                        "renderer operation ID reused for a different revision");
        return duplicate->ticket;
    }
    if (budget_.max_splats != prepared.budget_.max_splats ||
        budget_.bytes != prepared.budget_.bytes)
        return fail(error_code::WOULD_BLOCK,
                    "renderer residency budget changed during update validation; retry");
    if (!batch.snapshot || batch.operation.value == 0 || batch.snapshot->id != batch.id ||
        batch.snapshot->epoch != batch.epoch || batch.snapshot->revision != batch.new_revision)
        return fail(error_code::INVALID_DATA,
                    "renderer batch metadata does not match its immutable snapshot");
    // Full snapshots may establish a new epoch only when earlier uploads have drained.
    if (batch.full_snapshot) {
        if (queued_bytes_ != 0 || !pending_.empty())
            return fail(error_code::WOULD_BLOCK, "resync waits for pending renderer updates");
    } else {
        if (batch.id != accepted_scene_ || batch.epoch != accepted_epoch_)
            return fail(error_code::WRONG_EPOCH,
                        "renderer requires a full snapshot for this scene epoch");
        if (batch.base_revision != accepted_revision_)
            return fail(error_code::RESYNC_REQUIRED,
                        "renderer missed the batch base revision; send a full snapshot");
        if (batch.new_revision.value <= batch.base_revision.value)
            return fail(error_code::INVALID_DATA, "renderer revisions must increase");
    }
    const auto* visible = visible_ticket_ ? find(*visible_ticket_) : nullptr;
    const std::size_t resident = (visible ? visible->bytes : 0) + retired_bytes_;
    if (pending_.size() >= budget_.max_pending || prepared.bytes_ > budget_.bytes ||
        queued_bytes_ > budget_.bytes - prepared.bytes_ ||
        resident > budget_.bytes - prepared.bytes_ - queued_bytes_)
        return fail(error_code::WOULD_BLOCK,
                    "renderer upload or replacement residency budget is full");
    while (history_.size() >= 256) {
        const auto state = history_.front().progress.state;
        if (state != update_state::RETIRED && state != update_state::CANCELLED &&
            state != update_state::FAILED)
            return fail(error_code::WOULD_BLOCK, "renderer acknowledgement replay window is full");
        history_.pop_front();
    }
    const update_ticket ticket{next_ticket_++};
    history_.push_back({ticket,
                        batch.operation,
                        batch.id,
                        batch.epoch,
                        batch.base_revision,
                        {update_state::ACCEPTED, batch.new_revision, 0, {}},
                        prepared.bytes_,
                        prepared.hash_});
    accepted_scene_ = batch.id;
    accepted_epoch_ = batch.epoch;
    accepted_revision_ = batch.new_revision;
    queued_bytes_ += prepared.bytes_;
    batch.upload_bytes = prepared.bytes_;
    pending_.push_back({ticket, std::move(batch)});
    batch = {};
    return ticket;
}
result<update_progress> renderer_update_sink::query_progress(update_ticket ticket) const {
    std::lock_guard lock(mutex_);
    for (const auto& item : history_)
        if (item.ticket == ticket)
            return item.progress;
    return fail(error_code::NOT_FOUND,
                "renderer ticket absent or outside the 256-operation replay window");
}
result<void> renderer_update_sink::set_residency_budget(residency_budget budget) {
    std::lock_guard lock(mutex_);
    if (!budget.bytes || !budget.max_splats || !budget.max_pending)
        return fail(error_code::INVALID_ARGUMENT, "renderer budgets must be positive");
    if (queued_bytes_ > budget.bytes || retired_bytes_ > budget.bytes - queued_bytes_)
        return fail(error_code::WOULD_BLOCK, "new renderer budget is smaller than pending uploads");
    if (visible_) {
        auto size = validate_scene(*visible_, {4096, budget.max_splats, budget.bytes, 1000000});
        if (!size || *size > budget.bytes - queued_bytes_ - retired_bytes_)
            return fail(error_code::WOULD_BLOCK,
                        "new budget cannot hold visible and pending snapshots");
    }
    budget_ = budget;
    return {};
}
void renderer_update_sink::cancel_pending_locked() {
    for (const auto& item : pending_)
        if (auto* entry = find(item.ticket)) {
            entry->progress.state = update_state::CANCELLED;
            queued_bytes_ -= entry->bytes;
        }
    pending_.clear();
    accepted_scene_ = visible_ ? visible_->id : scene_id{};
    accepted_epoch_ = visible_ ? visible_->epoch : world_epoch{};
    accepted_revision_ = visible_ ? visible_->revision : scene_revision_id{};
    if (active_ticket_) {
        const auto* active = find(*active_ticket_);
        accepted_scene_ = active->scene;
        accepted_epoch_ = active->epoch;
        accepted_revision_ = active->progress.revision;
    }
}
void renderer_update_sink::cancel_pending() {
    std::lock_guard lock(mutex_);
    cancel_pending_locked();
}
scene_snapshot renderer_update_sink::visible_snapshot() const {
    std::lock_guard lock(mutex_);
    return visible_;
}
std::size_t renderer_update_sink::queued_bytes() const {
    std::lock_guard lock(mutex_);
    return queued_bytes_;
}
std::optional<admitted_update> renderer_update_sink::take_next() {
    std::lock_guard lock(mutex_);
    if (pending_.empty() || active_ticket_)
        return {};
    auto update = std::move(pending_.front());
    pending_.pop_front();
    find(update.ticket)->progress.state = update_state::ASSETS_AVAILABLE;
    active_ticket_ = update.ticket;
    return update;
}
void renderer_update_sink::acknowledge_uploaded(update_ticket ticket, std::uint64_t timeline) {
    std::lock_guard lock(mutex_);
    if (auto* item = find(ticket); item && active_ticket_ == ticket &&
                                   item->progress.state == update_state::ASSETS_AVAILABLE) {
        item->progress.state = update_state::UPLOADED;
        item->progress.timeline_value = timeline;
    }
}
void renderer_update_sink::acknowledge_visible(update_ticket ticket, scene_snapshot snapshot,
                                               std::uint64_t timeline) {
    std::lock_guard lock(mutex_);
    auto* incoming = find(ticket);
    if (!incoming || active_ticket_ != ticket ||
        incoming->progress.state != update_state::UPLOADED || !snapshot ||
        snapshot->id != incoming->scene || snapshot->epoch != incoming->epoch ||
        snapshot->revision != incoming->progress.revision)
        return;
    if (visible_ticket_)
        if (auto* previous = find(*visible_ticket_)) {
            previous->progress.timeline_value = timeline;
            retired_bytes_ += previous->bytes;
        }
    if (auto* item = find(ticket)) {
        item->progress.state = update_state::RENDER_VISIBLE;
        item->progress.timeline_value = timeline;
        queued_bytes_ -= item->bytes;
    }
    visible_ = std::move(snapshot);
    visible_ticket_ = ticket;
    active_ticket_.reset();
}
void renderer_update_sink::retire_completed(std::uint64_t completed_timeline) {
    std::lock_guard lock(mutex_);
    for (auto& item : history_)
        if (item.progress.state == update_state::RENDER_VISIBLE && item.ticket != visible_ticket_ &&
            item.progress.timeline_value <= completed_timeline) {
            item.progress.state = update_state::RETIRED;
            retired_bytes_ -= item.bytes;
        }
}
void renderer_update_sink::fail_update(update_ticket ticket, error failure) {
    std::lock_guard lock(mutex_);
    if (auto* item = find(ticket); item && active_ticket_ == ticket) {
        item->progress.state = update_state::FAILED;
        item->progress.failure = std::move(failure);
        queued_bytes_ -= item->bytes;
        active_ticket_.reset();
    }
    cancel_pending_locked();
}
} // namespace world_explorer
