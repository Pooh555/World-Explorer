#include "world_explorer/application/scenes.hpp"
#include "world_explorer/render_core/update_sink.hpp"

#include <array>
#include <barrier>
#include <optional>
#include <thread>
#include <utility>

#include <catch2/catch_test_macros.hpp>

using namespace world_explorer;

namespace {

constexpr std::size_t K_PRODUCERS { 4uz };

SceneDeltaBatch batch(std::uint64_t revision, std::uint64_t operation = 0) {
    auto scene = std::make_shared<SceneState>(*make_diagnostic_scene());
    scene->m_revision = { revision };
    auto result = make_snapshot_batch(scene, {operation ? operation : revision}).value();
    result.m_base_revision = { revision - 1 };
    result.m_full_snapshot = revision == 1;
    return result;
}

void publish(RendererUpdateSink& sink, const AdmittedUpdate& active) {
    const auto K_TIMELINE = active.m_batch.m_new_revision.m_value;
    sink.acknowledge_uploaded(active.m_ticket, K_TIMELINE);
    sink.acknowledge_visible(active.m_ticket, active.m_batch.m_snapshot, K_TIMELINE);
}

AdmittedUpdate require_next(RendererUpdateSink& sink) {
    if (auto active = sink.take_next()) return std::move(*active);
    FAIL("expected a queued renderer update");
    return {};
}

void initialize(RendererUpdateSink& sink) {
    auto first = batch(1);
    REQUIRE(sink.enqueue(first));
    publish(sink, require_next(sink));
}

// Assertions remain on the test owner. Each producer writes only its own slot;
// the barrier releases all participants together and joins publish their results.
template <class Consumer>
auto race_admission(RendererUpdateSink& sink,
                    std::array<SceneDeltaBatch, K_PRODUCERS>& candidates,
                    Consumer consume) {
    std::array<Result<UpdateTicket>, K_PRODUCERS> receipts;
    std::barrier start(static_cast<std::ptrdiff_t>(K_PRODUCERS + 1));
    std::array<std::jthread, K_PRODUCERS> producers;
    for (std::size_t index { 0uz }; index < K_PRODUCERS; ++index)
        producers[index] = std::jthread([&, index] {
            start.arrive_and_wait();
            receipts[index] = sink.enqueue(candidates[index]);
        });
    start.arrive_and_wait();
    consume();
    for (auto& producer : producers) producer.join();
    return receipts;
}

auto copies(const SceneDeltaBatch& original) {
    std::array<SceneDeltaBatch, K_PRODUCERS> candidates;
    candidates.fill(original);
    return candidates;
}

void check_preserved(const SceneDeltaBatch& candidate, const SceneDeltaBatch& original) {
    CHECK(candidate.m_snapshot == original.m_snapshot);
    CHECK(candidate.m_id == original.m_id);
    CHECK(candidate.m_epoch == original.m_epoch);
    CHECK(candidate.m_base_revision == original.m_base_revision);
    CHECK(candidate.m_new_revision == original.m_new_revision);
    CHECK(candidate.m_operation == original.m_operation);
    CHECK(candidate.m_full_snapshot == original.m_full_snapshot);
    CHECK(candidate.m_upload_bytes == original.m_upload_bytes);
}

}  // namespace

TEST_CASE("concurrent renderer producers share one receipt while the render owner consumes",
          "[render_threads]") {
    const auto K_BYTES = validate_scene(*make_diagnostic_scene()).value();
    RendererUpdateSink sink({K_BYTES * 3, 250000, 1});
    initialize(sink);
    const auto K_ORIGINAL_VISIBLE = sink.visible_snapshot();
    const auto K_ORIGINAL_HASH = scene_content_hash(*K_ORIGINAL_VISIBLE);
    for (std::uint64_t revision { 2 }; revision <= 33; ++revision) {
        CAPTURE(revision);
        const auto K_ORIGINAL = batch(revision);
        auto candidates = copies(K_ORIGINAL);
        std::optional<AdmittedUpdate> active;
        const auto K_RECEIPTS = race_admission(sink, candidates, [&] {
            active = sink.take_next();
            sink.retire_completed(revision - 1);
            const auto K_VISIBLE = sink.visible_snapshot();
            CHECK(K_VISIBLE);
            if (K_VISIBLE) CHECK(K_VISIBLE->m_revision.m_value == revision - 1);
        });
        const auto K_ADMITTED = active ? std::move(*active) : require_next(sink);
        std::size_t moved {};
        for (std::size_t index { 0uz }; index < K_PRODUCERS; ++index) {
            REQUIRE(K_RECEIPTS[index]);
            CHECK(*K_RECEIPTS[index] == K_ADMITTED.m_ticket);
            if (!candidates[index].m_snapshot) ++moved;
            else check_preserved(candidates[index], K_ORIGINAL);
        }
        CHECK(moved == 1);
        CHECK_FALSE(sink.take_next());
        CHECK(sink.queued_bytes() == K_BYTES);
        CHECK(K_ADMITTED.m_batch.m_snapshot == K_ORIGINAL.m_snapshot);
        auto retry_candidates = copies(K_ORIGINAL);
        const auto K_RETRIES =
            race_admission(sink, retry_candidates, [&] { publish(sink, K_ADMITTED); });
        for (std::size_t index { 0uz }; index < K_PRODUCERS; ++index) {
            REQUIRE(K_RETRIES[index]);
            CHECK(*K_RETRIES[index] == K_ADMITTED.m_ticket);
            check_preserved(retry_candidates[index], K_ORIGINAL);
        }
        CHECK(sink.query_progress(K_ADMITTED.m_ticket)->m_state == UpdateState::RENDER_VISIBLE);
        CHECK(sink.visible_snapshot() == K_ORIGINAL.m_snapshot);
        CHECK(sink.queued_bytes() == 0);
        CHECK(scene_content_hash(*K_ORIGINAL_VISIBLE) == K_ORIGINAL_HASH);
    }
    sink.retire_completed(33);
    CHECK(sink.set_residency_budget({K_BYTES, 250000, 1}));
}

TEST_CASE("concurrent renderer cancellation preserves admitted or cancelled retry identity",
          "[render_threads]") {
    for (unsigned iteration { 0 }; iteration < 32; ++iteration) {
        CAPTURE(iteration);
        RendererUpdateSink sink;
        initialize(sink);
        const auto K_VISIBLE = sink.visible_snapshot();
        const auto K_ORIGINAL = batch(2);
        auto candidates = copies(K_ORIGINAL);
        std::optional<AdmittedUpdate> active;
        const auto K_RECEIPTS = race_admission(sink, candidates, [&] {
            active = sink.take_next();
            sink.cancel_pending();
            CHECK(sink.visible_snapshot() == K_VISIBLE);
        });
        REQUIRE(K_RECEIPTS[0]);
        std::size_t moved {};
        for (std::size_t index { 0uz }; index < K_PRODUCERS; ++index) {
            REQUIRE(K_RECEIPTS[index]);
            CHECK(*K_RECEIPTS[index] == *K_RECEIPTS[0]);
            if (!candidates[index].m_snapshot) ++moved;
            else check_preserved(candidates[index], K_ORIGINAL);
        }
        CHECK(moved == 1);
        if (!active) active = sink.take_next();
        const auto K_PROGRESS = sink.query_progress(*K_RECEIPTS[0]);
        REQUIRE(K_PROGRESS);
        if (active) {
            CHECK(active.value().m_ticket == *K_RECEIPTS[0]);
            CHECK(K_PROGRESS->m_state == UpdateState::ASSETS_AVAILABLE);
            sink.cancel_pending();
            publish(sink, active.value());
            CHECK(sink.visible_snapshot() == K_ORIGINAL.m_snapshot);
            CHECK(sink.query_progress(active.value().m_ticket)->m_state ==
                  UpdateState::RENDER_VISIBLE);
        } else {
            CHECK(K_PROGRESS->m_state == UpdateState::CANCELLED);
            CHECK(sink.visible_snapshot() == K_VISIBLE);
        }
        CHECK(sink.queued_bytes() == 0);
        auto retry = K_ORIGINAL;
        const auto K_TICKET = sink.enqueue(retry);
        REQUIRE(K_TICKET);
        CHECK(*K_TICKET == *K_RECEIPTS[0]);
        check_preserved(retry, K_ORIGINAL);
        CHECK_FALSE(sink.take_next());
    }
}

TEST_CASE("concurrent renderer backpressure preserves callers through pending cancellation",
          "[render_threads]") {
    const auto K_BYTES = validate_scene(*make_diagnostic_scene()).value();
    for (unsigned iteration { 0 }; iteration < 16; ++iteration) {
        CAPTURE(iteration);
        RendererUpdateSink sink({K_BYTES * 3, 250000, 1});
        initialize(sink);
        const auto K_VISIBLE = sink.visible_snapshot();
        auto second = batch(2);
        REQUIRE(sink.enqueue(second));
        const auto K_ACTIVE = require_next(sink);
        auto third = batch(3);
        const auto K_PENDING = sink.enqueue(third);
        REQUIRE(K_PENDING);
        const auto K_FOURTH = batch(4);
        auto candidates = copies(K_FOURTH);
        const auto K_RECEIPTS = race_admission(sink, candidates, [&] {
            CHECK_FALSE(sink.take_next());
            sink.retire_completed(1);
            CHECK(sink.query_progress(*K_PENDING)->m_state == UpdateState::ACCEPTED);
            CHECK(sink.visible_snapshot() == K_VISIBLE);
        });
        for (std::size_t index { 0uz }; index < K_PRODUCERS; ++index) {
            REQUIRE_FALSE(K_RECEIPTS[index]);
            CHECK(K_RECEIPTS[index].error().m_code == ErrorCode::WOULD_BLOCK);
            check_preserved(candidates[index], K_FOURTH);
        }
        sink.cancel_pending();
        CHECK(sink.query_progress(*K_PENDING)->m_state == UpdateState::CANCELLED);
        CHECK(sink.query_progress(K_ACTIVE.m_ticket)->m_state == UpdateState::ASSETS_AVAILABLE);
        CHECK(sink.queued_bytes() == K_BYTES);
        publish(sink, K_ACTIVE);
        auto stale = K_FOURTH;
        const auto K_REJECTED = sink.enqueue(stale);
        REQUIRE_FALSE(K_REJECTED);
        CHECK(K_REJECTED.error().m_code == ErrorCode::RESYNC_REQUIRED);
        check_preserved(stale, K_FOURTH);
        auto corrected = batch(3, K_FOURTH.m_operation.m_value);
        REQUIRE(sink.enqueue(corrected));
        CHECK_FALSE(corrected.m_snapshot);
        sink.cancel_pending();
        CHECK(sink.visible_snapshot() == K_ACTIVE.m_batch.m_snapshot);
        CHECK(sink.queued_bytes() == 0);
        sink.retire_completed(2);
        CHECK(sink.set_residency_budget({K_BYTES, 250000, 1}));
    }
}
