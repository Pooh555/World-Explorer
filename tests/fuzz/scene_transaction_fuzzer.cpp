#include "world_explorer/scene_store/scene_store.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <span>

namespace {

using namespace world_explorer;
constexpr std::size_t K_MAX_STEPS { 32uz };
constexpr std::size_t K_RECORD_BYTES { 24uz };
constexpr std::size_t K_MAX_INPUT_BYTES { 4uz + K_MAX_STEPS * K_RECORD_BYTES };
constexpr scene_limits MAX_SCENE { 4, 8, 8 * sizeof(gaussian), 4 };

void require(bool condition) {
    if (!condition) std::abort();
}

struct counters {
    std::size_t accepted {}, rejected {}, corrected {}, retries {}, conflicts {}, replay_queries {};

    ~counters() {
        std::cerr << "synthetic_transactions accepted=" << accepted << " rejected=" << rejected
                  << " corrected=" << corrected << " retries=" << retries
                  << " conflicts=" << conflicts << " replay_queries=" << replay_queries << '\n';
    }
};

class input_bytes {
  public:
    explicit input_bytes(std::span<const std::uint8_t> bytes) : bytes_(bytes) { }

    std::uint8_t at(std::size_t index) const { return index < bytes_.size() ? bytes_[index] : 0; }

    double scalar() const {
        std::uint64_t bits {};
        for (unsigned index { 0 }; index < 8; ++index)
            bits |= static_cast<std::uint64_t>(at(16 + index)) << (index * 8U);
        return std::bit_cast<double>(bits);
    }

    float coefficient() const {
        const auto bits = static_cast<std::uint32_t>(std::bit_cast<std::uint64_t>(scalar()));
        return std::bit_cast<float>(bits);
    }

  private:
    std::span<const std::uint8_t> bytes_;
};

std::shared_ptr<scene_chunk> fresh_chunk(chunk_id id, std::uint64_t generation = 1) {
    auto chunk = std::make_shared<scene_chunk>();
    chunk->id = id;
    chunk->generation = generation;
    chunk->bounds = { { -1, -1, -1 }, { 1, 1, 1 } };
    chunk->splats.emplace_back();
    chunk->pose_revision = { 1 };
    chunk->geometry_valid = true;
    chunk->source_frames = { { 1 } };
    return chunk;
}

std::string chunk_hash(const std::shared_ptr<const scene_chunk>& chunk) {
    scene_state scene;
    scene.id = { 42 };
    scene.chunks[chunk->id] = chunk;
    return scene_content_hash(scene);
}

scene_transaction clone(const scene_transaction& transaction) {
    auto copy = transaction;
    for (auto& change : copy.changes)
        if (change.chunk) change.chunk = std::make_shared<scene_chunk>(*change.chunk);
    return copy;
}

struct receipt {
    scene_transaction transaction;
    scene_revision_id revision;
};

struct retained_snapshot {
    scene_snapshot snapshot;
    std::string hash;
};

scene_store_limits make_limits(input_bytes header) {
    scene_store_limits limits { MAX_SCENE, header.at(0) % 5U, header.at(1) % 9U };
    limits.scene.max_splats = header.at(2) % 9U;
    limits.scene.max_payload_bytes = limits.scene.max_splats * sizeof(gaussian);
    return limits;
}

class campaign_case {
  public:
    campaign_case(input_bytes header, counters& counts)
        : limits_(make_limits(header)), store_({42}, limits_), counts_(counts) {
        // Construct with the selected aggregate budget, then reset into a bounded
        // synthetic state. Reset is setup only; no fuzz input names external data.
        auto initial = std::make_shared<scene_state>();
        initial->id = { 42 };
        initial->epoch = { 1 };
        initial->revision = {
            (header.at(3) & 1U) != 0 ? std::numeric_limits<std::uint64_t>::max() - 1 : 0 };
        initial->provenance = { { "source_kind", "synthetic_diagnostic" },
                               { "fixture", "scene-transaction-fuzz-v1" } };
        if (limits_.scene.max_splats != 0) {
            auto chunk = fresh_chunk(
                {1}, (header.at(3) & 2U) != 0 ? std::numeric_limits<std::uint64_t>::max() : 1);
            initial->chunks[{1}] = chunk;
        }
        require(store_.reset(initial).has_value());
        remember_snapshot();
    }

    void step(input_bytes input, std::size_t index) {
        const auto before = store_.acquire_snapshot();
        auto transaction = make_transaction(input, index, before);
        exercise(transaction);
        const bool remembered = std::ranges::any_of(receipts_, [&](const auto& prior) {
            return prior.transaction.operation == transaction.operation;
        });
        // A rejected, unused operation must be reusable with corrected content.
        // Empty changes isolate receipt preservation from exhausted chunk budgets.
        if (last_rejected_ && !remembered && transaction.operation.value != 0 &&
            before->revision.value != std::numeric_limits<std::uint64_t>::max()) {
            scene_transaction corrected {before->id,
                                         before->epoch,
                                         before->revision,
                                         {before->revision.value + 1},
                                         transaction.operation,
                                         {}};
            exercise(corrected);
            require(!last_rejected_);
            ++counts_.corrected;
        }
        check_receipts();
        check_replay(input);
        check_snapshots();
    }

  private:
    scene_transaction
    make_transaction(input_bytes input, std::size_t index, const scene_snapshot& before) const {
        const auto action = input.at(0) % 8U;
        if ((action == 5 || action == 6) && !receipts_.empty()) {
            auto transaction = clone(receipts_[input.at(1) % receipts_.size()].transaction);
            if (action == 6) transaction.new_revision.value ^= 1U;
            return transaction;
        }
        const chunk_id id { static_cast<std::uint64_t>(input.at(1) % 6U) };
        scene_transaction transaction {before->id,
                                       before->epoch,
                                       before->revision,
                                       {before->revision.value + 1},
                                       {1000 + index},
                                       {}};
        switch (input.at(7) % 4U) {
        case 1:
            transaction.operation = {};
            break;
        case 2:
            if (!receipts_.empty()) transaction.operation = receipts_.front().transaction.operation;
            break;
        case 3:
            transaction.operation = { 1 };
            break;
        default:
            break;
        }
        auto chunk = fresh_chunk(id);
        const auto existing = before->chunks.find(id);
        if (existing != before->chunks.end()) chunk->generation = existing->second->generation + 1;
        switch (input.at(4) % 4U) {
        case 1:
            chunk->generation = existing == before->chunks.end() ? 1 : existing->second->generation;
            break;
        case 2:
            chunk->generation = 0;
            break;
        case 3:
            chunk->generation = std::numeric_limits<std::uint64_t>::max();
            break;
        default:
            break;
        }
        chunk->splats.resize(input.at(5) % 10U);
        chunk->source_frames.resize(input.at(6) % 6U, frame_id { 1 });
        scene_operation change {static_cast<scene_operation_kind>(action < 5 ? action : 255),
                                id,
                                nullptr,
                                {},
                                {input.at(8)}};
        if (action <= 1 || (input.at(3) & 4U) != 0) change.chunk = chunk;
        // Fixed selectors expose validation boundaries while scalar bits remain
        // arbitrary, including NaNs, infinities, subnormals and signed zero.
        switch (input.at(2) % 32U) {
        case 1:
            transaction.id = { 0 };
            break;
        case 2:
            transaction.epoch.value ^= 1U;
            break;
        case 3:
            transaction.base_revision.value ^= 1U;
            break;
        case 4:
            transaction.new_revision.value ^= 1U;
            break;
        case 5:
            change.kind = static_cast<scene_operation_kind>(5 + input.at(9) % 251U);
            break;
        case 6:
            change.id = { 0 };
            break;
        case 7:
            change.chunk.reset();
            break;
        case 8:
            chunk->id.value ^= 1U;
            break;
        case 9:
            chunk->generation = 0;
            break;
        case 10:
            chunk->pose_revision = {};
            break;
        case 11:
            chunk->bounds.minimum.x = input.scalar();
            break;
        case 12:
            chunk->bounds.maximum.z = input.scalar();
            break;
        case 13:
            chunk->world_from_chunk.scale = input.scalar();
            break;
        case 14:
            chunk->world_from_chunk.rotation.w = input.scalar();
            break;
        case 15:
            chunk->world_from_chunk.translation.y = input.scalar();
            break;
        case 16:
            change.world_from_chunk.scale = input.scalar();
            break;
        case 17:
            change.world_from_chunk.rotation.w = input.scalar();
            break;
        case 18:
            change.world_from_chunk.translation.z = input.scalar();
            break;
        default:
            if (!chunk->splats.empty()) {
                auto& splat = chunk->splats.front();
                switch (input.at(2) % 32U) {
                case 19:
                    splat.mean[input.at(10) % 3U] = input.coefficient();
                    break;
                case 20:
                    splat.scale[input.at(10) % 3U] = input.coefficient();
                    break;
                case 21:
                    splat.rotation[input.at(10) % 4U] = input.coefficient();
                    break;
                case 22:
                    splat.opacity = input.coefficient();
                    break;
                case 23:
                    splat.sh[input.at(10) % 48U] = input.coefficient();
                    break;
                case 24:
                    splat.sh_degree = input.at(9);
                    break;
                default:
                    break;
                }
            }
            break;
        }
        transaction.changes.push_back(change);
        const auto extra = (input.at(3) >> 3U) % 6U;
        for (unsigned item { 0 }; item < extra; ++item) {
            auto additional = change;
            if ((input.at(3) & 1U) != 0) {
                additional.id = { 2 + item };
                if (additional.chunk) {
                    auto owned = std::make_shared<scene_chunk>(*additional.chunk);
                    owned->id = additional.id;
                    additional.chunk = std::move(owned);
                }
            }
            transaction.changes.push_back(std::move(additional));
        }
        return transaction;
    }

    void exercise(const scene_transaction& transaction) {
        const auto before = store_.acquire_snapshot();
        const auto hash = scene_content_hash(*before);
        const auto caller = clone(transaction);
        const auto committed = store_.commit(transaction);
        for (std::size_t index { 0uz }; index < transaction.changes.size(); ++index)
            if (transaction.changes[index].chunk)
                require(chunk_hash(transaction.changes[index].chunk) ==
                        chunk_hash(caller.changes[index].chunk));
        const auto after = store_.acquire_snapshot();
        require(scene_content_hash(*before) == hash);
        last_rejected_ = !committed;
        if (!committed) {
            ++counts_.rejected;
            require(after == before);
        } else {
            ++counts_.accepted;
            const auto prior = std::ranges::find_if(receipts_, [&](const auto& value) {
                return value.transaction.operation == transaction.operation;
            });
            if (prior != receipts_.end()) {
                require(after == before && *committed == prior->revision);
            } else {
                require(*committed == transaction.new_revision && after->revision == *committed);
                require(after->id == before->id && after->epoch == before->epoch);
                require(validate_scene(*after, limits_.scene).has_value());
                check_effects(transaction, *before, after);
                receipts_.push_back({caller, *committed});
                while (receipts_.size() > limits_.remembered_operations) receipts_.pop_front();
                ++commits_;
                remember_snapshot();
            }
        }
        // The retained producer aliases are deliberately mutated and restored.
        // A store retaining caller objects will alter a saved snapshot here.
        for (std::size_t index { 0uz }; index < transaction.changes.size(); ++index) {
            if (!transaction.changes[index].chunk) continue;
            // These chunks were all allocated mutable by this harness; exact
            // replay transactions are cloned into mutable owners as well.
            auto alias = std::const_pointer_cast<scene_chunk>(transaction.changes[index].chunk);
            alias->splats.clear();
            alias->source_frames.clear();
            alias->generation = 0;
            alias->world_from_chunk.scale = -1;
            check_snapshots();
            require(scene_content_hash(*before) == hash);
            *alias = *caller.changes[index].chunk;
        }
    }

    static void check_effects(const scene_transaction& transaction,
                              const scene_state& before,
                              const scene_snapshot& after) {
        std::size_t expected_count = before.chunks.size();
        for (const auto& change : transaction.changes) {
            const auto found = after->chunks.find(change.id);
            if (change.kind == scene_operation_kind::ERASE) {
                require(found == after->chunks.end());
                --expected_count;
            } else {
                require(found != after->chunks.end());
                if (change.kind == scene_operation_kind::CREATE ||
                    change.kind == scene_operation_kind::REPLACE) {
                    require(found->second != change.chunk &&
                            chunk_hash(found->second) == chunk_hash(change.chunk));
                    if (change.kind == scene_operation_kind::CREATE) ++expected_count;
                } else {
                    auto expected = std::make_shared<scene_chunk>(*before.chunks.at(change.id));
                    ++expected->generation;
                    expected->geometry_valid = false;
                    expected->pose_revision = change.pose_revision;
                    if (change.kind == scene_operation_kind::TRANSFORM)
                        expected->world_from_chunk = change.world_from_chunk;
                    require(chunk_hash(found->second) == chunk_hash(expected));
                }
            }
        }
        require(after->chunks.size() == expected_count);
        for (const auto& [id, chunk] : before.chunks)
            if (std::ranges::none_of(transaction.changes,
                                     [&](const auto& change) { return change.id == id; }))
                require(after->chunks.at(id) == chunk);
    }

    void remember_snapshot() {
        const auto snapshot = store_.acquire_snapshot();
        snapshots_.push_back({snapshot, scene_content_hash(*snapshot)});
    }

    void check_snapshots() const {
        for (const auto& retained : snapshots_)
            require(scene_content_hash(*retained.snapshot) == retained.hash);
    }

    void check_receipts() {
        const auto snapshot = store_.acquire_snapshot();
        for (const auto& prior : receipts_) {
            const auto retried = store_.commit(prior.transaction);
            require(retried && *retried == prior.revision && store_.acquire_snapshot() == snapshot);
            ++counts_.retries;
            auto conflict = clone(prior.transaction);
            conflict.new_revision.value ^= 1U;
            const auto rejected = store_.commit(conflict);
            require(!rejected && rejected.error().m_code == ErrorCode::DUPLICATE_OPERATION &&
                    store_.acquire_snapshot() == snapshot);
            ++counts_.conflicts;
        }
    }

    void check_replay(input_bytes input) {
        const auto snapshot = store_.acquire_snapshot();
        const auto bytes = validate_scene(*snapshot, limits_.scene);
        require(bytes.has_value());
        scene_revision_id requested = snapshot->revision;
        switch (input.at(11) % 4U) {
        case 1:
            requested = {};
            break;
        case 2:
            requested.value -= requested.value != 0 ? 1U : 0U;
            break;
        case 3:
            requested.value ^= 1U;
            break;
        default:
            break;
        }
        const byte_budget budget { (input.at(12) & 1U) != 0 && *bytes != 0 ? *bytes - 1 : *bytes };
        const auto replay = store_.changes_since(requested, budget);
        const auto retained = std::min(commits_, limits_.journal_entries);
        const bool outside = requested.value > snapshot->revision.value ||
                             snapshot->revision.value - requested.value > retained;
        if (outside) require(!replay && replay.error().m_code == ErrorCode::RESYNC_REQUIRED);
        else if (budget.bytes < *bytes)
            require(!replay && replay.error().m_code == ErrorCode::WOULD_BLOCK);
        else
            require(replay && replay->snapshot == snapshot && replay->id == snapshot->id &&
                    replay->epoch == snapshot->epoch && replay->base_revision == requested &&
                    replay->new_revision == snapshot->revision && replay->upload_bytes == *bytes &&
                    !replay->full_snapshot);
        require(store_.acquire_snapshot() == snapshot);
        ++counts_.replay_queries;
    }

    scene_store_limits limits_;
    scene_store store_;
    counters& counts_;
    std::deque<receipt> receipts_;
    std::vector<retained_snapshot> snapshots_;
    std::size_t commits_ {};
    bool last_rejected_ {};
};

}  // namespace

// NOLINTNEXTLINE(readability-identifier-naming): libFuzzer's required external C entry point.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size > K_MAX_INPUT_BYTES) return 0;
    static counters counts;
    const std::span<const std::uint8_t> input(data, size);
    campaign_case test(input_bytes(input.first(std::min(size, 4uz))), counts);
    for (std::size_t offset { 4uz }, step { 0 }; offset < size && step < K_MAX_STEPS;
         offset += K_RECORD_BYTES, ++step)
        test.step(input_bytes(input.subspan(offset, std::min(size - offset, K_RECORD_BYTES))),
                  step);
    return 0;
}
