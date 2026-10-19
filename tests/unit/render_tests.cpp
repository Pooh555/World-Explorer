#include "world_explorer/application/scenes.hpp"
#include "world_explorer/render_core/update_sink.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
using namespace world_explorer;

namespace {

SceneDeltaBatch batch_at(SceneRevisionId revision, OperationId operation, bool full = false) {
    auto scene = std::make_shared<SceneState>(*make_diagnostic_scene());
    scene->m_revision = revision;
    auto batch = make_snapshot_batch(scene, operation).value();
    batch.m_full_snapshot = full;
    batch.m_base_revision = { revision.m_value - 1 };
    return batch;
}

AdmittedUpdate require_next(RendererUpdateSink& sink) {
    if (auto admitted = sink.take_next()) {
        return std::move(*admitted);
    }
    FAIL("expected a queued renderer update");
    return {};
}

void make_visible(RendererUpdateSink& sink, UpdateTicket ticket, std::uint64_t timeline) {
    auto admitted = require_next(sink);
    REQUIRE(admitted.m_ticket == ticket);
    sink.acknowledge_uploaded(ticket, timeline);
    sink.acknowledge_visible(ticket, admitted.m_batch.m_snapshot, timeline);
}

}  // namespace

TEST_CASE("renderer admission is idempotent and rejects stale or modified retries") {
    RendererUpdateSink sink;
    auto initial = batch_at({ 1 }, { 1 }, true);
    const auto K_RETRY = initial;
    auto ticket = sink.enqueue(initial);
    REQUIRE(ticket);
    REQUIRE_FALSE(initial.m_snapshot);
    auto duplicate = K_RETRY;
    const auto K_RETRIED = sink.enqueue(duplicate);
    REQUIRE(K_RETRIED);
    REQUIRE(*K_RETRIED == *ticket);
    REQUIRE(duplicate.m_snapshot);
    auto modified = std::make_shared<SceneState>(*K_RETRY.m_snapshot);
    modified->m_chunks.clear();
    duplicate.m_snapshot = modified;
    REQUIRE_FALSE(sink.enqueue(duplicate));
    make_visible(sink, *ticket, 1);
    auto stale = batch_at({ 3 }, { 3 });
    REQUIRE_FALSE(sink.enqueue(stale));
    REQUIRE(stale.m_snapshot);
    auto wrong_epoch = batch_at({ 2 }, { 2 });
    wrong_epoch.m_epoch = { 1 };
    REQUIRE_FALSE(sink.enqueue(wrong_epoch));
}

TEST_CASE("prepared renderer admission preserves retries and checks current budget") {
    const auto K_BYTES = validate_scene(*make_diagnostic_scene()).value();
    RendererUpdateSink sink({ K_BYTES * 4, 250000, 4 });
    const auto K_ORIGINAL = batch_at({ 1 }, { 11 }, true);
    auto prepared = sink.prepare(K_ORIGINAL);
    REQUIRE(prepared);
    REQUIRE(K_ORIGINAL.m_snapshot);
    REQUIRE(sink.set_residency_budget({ K_BYTES * 5, 250000, 4 }));
    const auto K_CHANGED = sink.enqueue(*prepared);
    REQUIRE_FALSE(K_CHANGED);
    CHECK(K_CHANGED.error().m_code == ErrorCode::WOULD_BLOCK);
    auto fresh = sink.prepare(K_ORIGINAL);
    REQUIRE(fresh);
    const auto K_TICKET = sink.enqueue(*fresh);
    REQUIRE(K_TICKET);
    REQUIRE_FALSE(sink.enqueue(*fresh));
    auto duplicate = sink.prepare(K_ORIGINAL);
    REQUIRE(duplicate);
    const auto K_REPEATED = sink.enqueue(*duplicate);
    REQUIRE(K_REPEATED);
    CHECK(*K_REPEATED == *K_TICKET);
    CHECK(sink.enqueue(*duplicate));
    auto altered = batch_at({ 2 }, { 11 });
    auto collision = sink.prepare(altered);
    REQUIRE(collision);
    const auto K_REJECTED = sink.enqueue(*collision);
    REQUIRE_FALSE(K_REJECTED);
    CHECK(K_REJECTED.error().m_code == ErrorCode::DUPLICATE_OPERATION);
    auto changed_scene = std::make_shared<SceneState>(*K_ORIGINAL.m_snapshot);
    auto changed_chunk = std::make_shared<SceneChunk>(*changed_scene->m_chunks.begin()->second);
    changed_chunk->m_splats.front().m_opacity *= 0.5F;
    changed_scene->m_chunks.begin()->second = changed_chunk;
    auto changed_content = K_ORIGINAL;
    changed_content.m_snapshot = changed_scene;
    auto changed_prepared = sink.prepare(changed_content);
    REQUIRE(changed_prepared);
    const auto K_CHANGED_RETRY = sink.enqueue(*changed_prepared);
    REQUIRE_FALSE(K_CHANGED_RETRY);
    CHECK(K_CHANGED_RETRY.error().m_code == ErrorCode::DUPLICATE_OPERATION);
    make_visible(sink, *K_TICKET, 1);
    auto stale = batch_at({ 3 }, { 12 });
    auto stale_prepared = sink.prepare(stale);
    REQUIRE(stale_prepared);
    const auto K_WRONG_BASE = sink.enqueue(*stale_prepared);
    REQUIRE_FALSE(K_WRONG_BASE);
    CHECK(K_WRONG_BASE.error().m_code == ErrorCode::RESYNC_REQUIRED);
    CHECK(stale.m_snapshot);
}

TEST_CASE("renderer reports an oversized new snapshot as invalid before residency admission") {
    RendererUpdateSink sink({ sizeof(Gaussian) - 1, 250000, 4 });
    auto first = batch_at({ 1 }, { 1 }, true);
    const auto K_RETAINED = first.m_snapshot;
    const auto K_REJECTED = sink.enqueue(first);
    REQUIRE_FALSE(K_REJECTED);
    CHECK(K_REJECTED.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
    CHECK(first.m_snapshot == K_RETAINED);
    CHECK_FALSE(sink.take_next());
}

TEST_CASE("old residency remains charged until GPU retirement and duplicate ack is harmless") {
    const auto K_BYTES = validate_scene(*make_diagnostic_scene()).value();
    RendererUpdateSink sink({ K_BYTES * 2, 250000, 4 });
    auto first = batch_at({ 1 }, { 1 }, true);
    auto first_ticket = sink.enqueue(first);
    REQUIRE(first_ticket);
    make_visible(sink, *first_ticket, 1);
    auto second = batch_at({ 2 }, { 2 });
    auto second_ticket = sink.enqueue(second);
    REQUIRE(second_ticket);
    make_visible(sink, *second_ticket, 2);
    sink.acknowledge_visible(*second_ticket, sink.visible_snapshot(), 2);
    REQUIRE(sink.queued_bytes() == 0);
    auto third = batch_at({ 3 }, { 3 });
    REQUIRE_FALSE(sink.enqueue(third));
    sink.retire_completed(1);
    REQUIRE_FALSE(sink.enqueue(third));
    sink.retire_completed(2);
    REQUIRE(sink.enqueue(third));
    REQUIRE(sink.query_progress(*first_ticket)->m_state == UpdateState::RETIRED);
}

TEST_CASE("cancelling queued updates preserves the in-flight revision chain") {
    RendererUpdateSink sink;
    auto first = batch_at({ 1 }, { 1 }, true);
    auto ticket = sink.enqueue(first);
    REQUIRE(ticket);
    const auto K_ACTIVE = require_next(sink);
    auto second = batch_at({ 2 }, { 2 });
    REQUIRE(sink.enqueue(second));
    sink.cancel_pending();
    REQUIRE_FALSE(sink.take_next());
    sink.acknowledge_uploaded(*ticket, 1);
    sink.acknowledge_visible(*ticket, K_ACTIVE.m_batch.m_snapshot, 1);
    auto replacement = batch_at({ 2 }, { 3 });
    REQUIRE(sink.enqueue(replacement));
    auto uploading = require_next(sink);
    sink.fail_update(uploading.m_ticket, { ErrorCode::BACKEND_FAILURE, "injected upload failure" });
    REQUIRE(sink.visible_snapshot()->m_revision.m_value == 1);
    REQUIRE(sink.queued_bytes() == 0);
    auto resync = batch_at({ 5 }, { 4 }, true);
    REQUIRE(sink.enqueue(resync));
}

TEST_CASE("renderer rejects malformed admission without consuming caller data", "[validation]") {
    RendererUpdateSink sink;
    auto first = batch_at({ 1 }, { 1 }, true);
    const auto K_TICKET = sink.enqueue(first);
    REQUIRE(K_TICKET);
    make_visible(sink, *K_TICKET, 1);
    const auto K_VISIBLE = sink.visible_snapshot();
    auto next = batch_at({ 2 }, { 2 });
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8);
    CAPTURE(K_FIELD);
    auto expected = ErrorCode::INVALID_DATA;
    switch (K_FIELD) {
    case 0:
        next.m_snapshot.reset();
        break;
    case 1:
        next.m_operation = {};
        break;
    case 2:
        next.m_id = { 123 };
        break;
    case 3:
        next.m_epoch = { 123 };
        break;
    case 4:
        next.m_new_revision = { 123 };
        break;
    case 5: {
        auto invalid = std::make_shared<SceneState>(*next.m_snapshot);
        invalid->m_id = {};
        next.m_snapshot = invalid;
        break;
    }
    case 6: {
        auto wrong_epoch = std::make_shared<SceneState>(*next.m_snapshot);
        wrong_epoch->m_epoch = { 123 };
        next.m_snapshot = wrong_epoch;
        next.m_epoch = { 123 };
        expected = ErrorCode::WRONG_EPOCH;
        break;
    }
    case 7:
        next.m_base_revision = { 0 };
        expected = ErrorCode::RESYNC_REQUIRED;
        break;
    case 8: {
        auto non_increasing = std::make_shared<SceneState>(*next.m_snapshot);
        non_increasing->m_revision = { 1 };
        next.m_snapshot = non_increasing;
        next.m_new_revision = { 1 };
        break;
    }
    default:
        FAIL("unknown validation mutation");
    }
    const auto K_RETAINED = next.m_snapshot;
    const auto K_REJECTED = sink.enqueue(next);
    REQUIRE_FALSE(K_REJECTED);
    CHECK(K_REJECTED.error().m_code == expected);
    CHECK(next.m_snapshot == K_RETAINED);
    CHECK(sink.visible_snapshot() == K_VISIBLE);
    CHECK(sink.queued_bytes() == 0);
    CHECK_FALSE(sink.take_next());
    auto repair = batch_at({ 2 }, { 2 });
    REQUIRE(sink.enqueue(repair));
}

TEST_CASE("renderer rejects modified retry metadata within the receipt window",
          "[validation][replay]") {
    RendererUpdateSink sink;
    auto first = batch_at({ 1 }, { 1 }, true);
    const auto K_ORIGINAL = first;
    REQUIRE(sink.enqueue(first));
    auto retry = K_ORIGINAL;
    const auto K_FIELD = GENERATE(0, 1, 2, 3);
    switch (K_FIELD) {
    case 0:
        retry.m_id = { 123 };
        break;
    case 1:
        retry.m_epoch = { 123 };
        break;
    case 2:
        retry.m_base_revision = { 123 };
        break;
    case 3:
        retry.m_new_revision = { 123 };
        break;
    default:
        FAIL("unknown validation mutation");
    }
    const auto K_REJECTED = sink.enqueue(retry);
    REQUIRE_FALSE(K_REJECTED);
    CHECK(K_REJECTED.error().m_code == ErrorCode::DUPLICATE_OPERATION);
    CHECK(retry.m_snapshot == K_ORIGINAL.m_snapshot);
}

TEST_CASE("renderer budget changes account for pending visible and retired assets",
          "[validation]") {
    const auto K_BYTES = validate_scene(*make_diagnostic_scene()).value();
    RendererUpdateSink sink({ K_BYTES * 4, 250000, 1 });
    for (const auto K_BUDGET :
         { ResidencyBudget { 0, 1, 1 }, ResidencyBudget { 1, 0, 1 }, ResidencyBudget { 1, 1, 0 } }) {
        CHECK_FALSE(sink.set_residency_budget(K_BUDGET));
    }
    auto first = batch_at({ 1 }, { 1 }, true);
    const auto K_FIRST_TICKET = sink.enqueue(first);
    REQUIRE(K_FIRST_TICKET);
    CHECK_FALSE(sink.set_residency_budget({ K_BYTES - 1, 250000, 1 }));
    auto second = batch_at({ 2 }, { 2 });
    CHECK_FALSE(sink.enqueue(second));
    CHECK(second.m_snapshot);
    const auto K_ACTIVE = require_next(sink);
    auto resync = batch_at({ 9 }, { 9 }, true);
    CHECK_FALSE(sink.enqueue(resync));
    const auto K_SECOND_TICKET = sink.enqueue(second);
    REQUIRE(K_SECOND_TICKET);
    CHECK_FALSE(sink.enqueue(resync));
    sink.acknowledge_uploaded(*K_FIRST_TICKET, 1);
    sink.acknowledge_visible(*K_FIRST_TICKET, K_ACTIVE.m_batch.m_snapshot, 1);
    make_visible(sink, *K_SECOND_TICKET, 2);
    CHECK_FALSE(sink.set_residency_budget({ K_BYTES, 250000, 1 }));
    sink.retire_completed(2);
    CHECK_FALSE(sink.set_residency_budget({ K_BYTES, 1, 1 }));
    REQUIRE(sink.set_residency_budget({ K_BYTES * 2, 250000, 1 }));
    REQUIRE(sink.enqueue(resync));
    sink.cancel_pending();
    CHECK(sink.query_progress(*K_SECOND_TICKET)->m_state == UpdateState::RENDER_VISIBLE);
}

TEST_CASE("renderer ignores out-of-order acknowledgements and preserves the active update",
          "[validation]") {
    RendererUpdateSink sink;
    CHECK_FALSE(sink.query_progress({ 999 }));
    sink.acknowledge_uploaded({ 999 }, 1);
    sink.acknowledge_visible({ 999 }, nullptr, 1);
    auto first = batch_at({ 1 }, { 1 }, true);
    const auto K_TICKET = sink.enqueue(first);
    REQUIRE(K_TICKET);
    sink.acknowledge_uploaded(*K_TICKET, 1);
    CHECK(sink.query_progress(*K_TICKET)->m_state == UpdateState::ACCEPTED);
    const auto K_ACTIVE = require_next(sink);
    sink.acknowledge_visible(*K_TICKET, K_ACTIVE.m_batch.m_snapshot, 1);
    CHECK_FALSE(sink.visible_snapshot());
    sink.acknowledge_uploaded(*K_TICKET, 1);
    sink.acknowledge_uploaded(*K_TICKET, 2);
    CHECK(sink.query_progress(*K_TICKET)->m_timeline_value == 1);
    const auto K_FIELD = GENERATE(0, 1, 2, 3);
    auto wrong = std::make_shared<SceneState>(*K_ACTIVE.m_batch.m_snapshot);
    switch (K_FIELD) {
    case 0:
        wrong.reset();
        break;
    case 1:
        wrong->m_id = { 123 };
        break;
    case 2:
        wrong->m_epoch = { 123 };
        break;
    case 3:
        wrong->m_revision = { 123 };
        break;
    default:
        FAIL("unknown validation mutation");
    }
    sink.acknowledge_visible(*K_TICKET, wrong, 1);
    CHECK_FALSE(sink.visible_snapshot());
    CHECK(sink.query_progress(*K_TICKET)->m_state == UpdateState::UPLOADED);
    sink.acknowledge_visible(*K_TICKET, K_ACTIVE.m_batch.m_snapshot, 1);
    CHECK(sink.visible_snapshot() == K_ACTIVE.m_batch.m_snapshot);
    CHECK(sink.queued_bytes() == 0);
}

TEST_CASE("renderer receipt eviction waits for retirement and requires resync for old tickets",
          "[validation][replay]") {
    RendererUpdateSink sink;
    UpdateTicket first_ticket;
    for (std::uint64_t revision { 1 }; revision <= 256; ++revision) {
        auto batch = batch_at({ revision }, { revision }, revision == 1);
        const auto K_TICKET = sink.enqueue(batch);
        REQUIRE(K_TICKET);
        if (revision == 1) {
            first_ticket = *K_TICKET;
        }
        make_visible(sink, *K_TICKET, revision);
    }
    auto next = batch_at({ 257 }, { 257 });
    const auto K_REJECTED = sink.enqueue(next);
    REQUIRE_FALSE(K_REJECTED);
    CHECK(K_REJECTED.error().m_code == ErrorCode::WOULD_BLOCK);
    CHECK(next.m_snapshot);
    CHECK(sink.query_progress(first_ticket)->m_state == UpdateState::RENDER_VISIBLE);
    sink.retire_completed(256);
    REQUIRE(sink.enqueue(next));
    const auto K_EVICTED = sink.query_progress(first_ticket);
    REQUIRE_FALSE(K_EVICTED);
    CHECK(K_EVICTED.error().m_code == ErrorCode::NOT_FOUND);
}
