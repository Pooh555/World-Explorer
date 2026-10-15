#include "world_explorer/application/scenes.hpp"
#include "world_explorer/render_core/update_sink.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
using namespace world_explorer;
namespace {
scene_delta_batch batch_at(scene_revision_id revision, operation_id operation, bool full = false) {
    auto scene = std::make_shared<scene_state>(*make_diagnostic_scene());
    scene->revision = revision;
    auto batch = make_snapshot_batch(scene, operation).value();
    batch.full_snapshot = full;
    batch.base_revision = {revision.value - 1};
    return batch;
}
admitted_update require_next(renderer_update_sink& sink) {
    if (auto admitted = sink.take_next())
        return std::move(*admitted);
    FAIL("expected a queued renderer update");
    return {};
}
void make_visible(renderer_update_sink& sink, update_ticket ticket, std::uint64_t timeline) {
    auto admitted = require_next(sink);
    REQUIRE(admitted.ticket == ticket);
    sink.acknowledge_uploaded(ticket, timeline);
    sink.acknowledge_visible(ticket, admitted.batch.snapshot, timeline);
}
} // namespace
TEST_CASE("renderer admission is idempotent and rejects stale or modified retries") {
    renderer_update_sink sink;
    auto initial = batch_at({1}, {1}, true);
    const auto retry = initial;
    auto ticket = sink.enqueue(initial);
    REQUIRE(ticket);
    REQUIRE_FALSE(initial.snapshot);
    auto duplicate = retry;
    const auto retried = sink.enqueue(duplicate);
    REQUIRE(retried);
    REQUIRE(*retried == *ticket);
    REQUIRE(duplicate.snapshot);
    auto modified = std::make_shared<scene_state>(*retry.snapshot);
    modified->chunks.clear();
    duplicate.snapshot = modified;
    REQUIRE_FALSE(sink.enqueue(duplicate));
    make_visible(sink, *ticket, 1);
    auto stale = batch_at({3}, {3});
    REQUIRE_FALSE(sink.enqueue(stale));
    REQUIRE(stale.snapshot);
    auto wrong_epoch = batch_at({2}, {2});
    wrong_epoch.epoch = {1};
    REQUIRE_FALSE(sink.enqueue(wrong_epoch));
}

TEST_CASE("renderer reports an oversized new snapshot as invalid before residency admission") {
    renderer_update_sink sink({sizeof(gaussian) - 1, 250000, 4});
    auto first = batch_at({1}, {1}, true);
    const auto retained = first.snapshot;
    const auto rejected = sink.enqueue(first);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().code == error_code::RESOURCE_EXHAUSTED);
    CHECK(first.snapshot == retained);
    CHECK_FALSE(sink.take_next());
}
TEST_CASE("old residency remains charged until GPU retirement and duplicate ack is harmless") {
    const auto bytes = validate_scene(*make_diagnostic_scene()).value();
    renderer_update_sink sink({bytes * 2, 250000, 4});
    auto first = batch_at({1}, {1}, true);
    auto first_ticket = sink.enqueue(first);
    REQUIRE(first_ticket);
    make_visible(sink, *first_ticket, 1);
    auto second = batch_at({2}, {2});
    auto second_ticket = sink.enqueue(second);
    REQUIRE(second_ticket);
    make_visible(sink, *second_ticket, 2);
    sink.acknowledge_visible(*second_ticket, sink.visible_snapshot(), 2);
    REQUIRE(sink.queued_bytes() == 0);
    auto third = batch_at({3}, {3});
    REQUIRE_FALSE(sink.enqueue(third));
    sink.retire_completed(1);
    REQUIRE_FALSE(sink.enqueue(third));
    sink.retire_completed(2);
    REQUIRE(sink.enqueue(third));
    REQUIRE(sink.query_progress(*first_ticket)->state == update_state::RETIRED);
}
TEST_CASE("cancelling queued updates preserves the in-flight revision chain") {
    renderer_update_sink sink;
    auto first = batch_at({1}, {1}, true);
    auto ticket = sink.enqueue(first);
    REQUIRE(ticket);
    const auto active = require_next(sink);
    auto second = batch_at({2}, {2});
    REQUIRE(sink.enqueue(second));
    sink.cancel_pending();
    REQUIRE_FALSE(sink.take_next());
    sink.acknowledge_uploaded(*ticket, 1);
    sink.acknowledge_visible(*ticket, active.batch.snapshot, 1);
    auto replacement = batch_at({2}, {3});
    REQUIRE(sink.enqueue(replacement));
    auto uploading = require_next(sink);
    sink.fail_update(uploading.ticket, {error_code::BACKEND_FAILURE, "injected upload failure"});
    REQUIRE(sink.visible_snapshot()->revision.value == 1);
    REQUIRE(sink.queued_bytes() == 0);
    auto resync = batch_at({5}, {4}, true);
    REQUIRE(sink.enqueue(resync));
}

TEST_CASE("renderer rejects malformed admission without consuming caller data", "[validation]") {
    renderer_update_sink sink;
    auto first = batch_at({1}, {1}, true);
    const auto ticket = sink.enqueue(first);
    REQUIRE(ticket);
    make_visible(sink, *ticket, 1);
    const auto visible = sink.visible_snapshot();
    auto next = batch_at({2}, {2});
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8);
    CAPTURE(field);
    auto expected = error_code::INVALID_DATA;
    switch (field) {
    case 0:
        next.snapshot.reset();
        break;
    case 1:
        next.operation = {};
        break;
    case 2:
        next.id = {123};
        break;
    case 3:
        next.epoch = {123};
        break;
    case 4:
        next.new_revision = {123};
        break;
    case 5: {
        auto invalid = std::make_shared<scene_state>(*next.snapshot);
        invalid->id = {};
        next.snapshot = invalid;
        break;
    }
    case 6: {
        auto wrong_epoch = std::make_shared<scene_state>(*next.snapshot);
        wrong_epoch->epoch = {123};
        next.snapshot = wrong_epoch;
        next.epoch = {123};
        expected = error_code::WRONG_EPOCH;
        break;
    }
    case 7:
        next.base_revision = {0};
        expected = error_code::RESYNC_REQUIRED;
        break;
    case 8: {
        auto non_increasing = std::make_shared<scene_state>(*next.snapshot);
        non_increasing->revision = {1};
        next.snapshot = non_increasing;
        next.new_revision = {1};
        break;
    }
    default:
        FAIL("unknown validation mutation");
    }
    const auto retained = next.snapshot;
    const auto rejected = sink.enqueue(next);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().code == expected);
    CHECK(next.snapshot == retained);
    CHECK(sink.visible_snapshot() == visible);
    CHECK(sink.queued_bytes() == 0);
    CHECK_FALSE(sink.take_next());
    auto repair = batch_at({2}, {2});
    REQUIRE(sink.enqueue(repair));
}

TEST_CASE("renderer rejects modified retry metadata within the receipt window",
          "[validation][replay]") {
    renderer_update_sink sink;
    auto first = batch_at({1}, {1}, true);
    const auto original = first;
    REQUIRE(sink.enqueue(first));
    auto retry = original;
    const auto field = GENERATE(0, 1, 2, 3);
    switch (field) {
    case 0:
        retry.id = {123};
        break;
    case 1:
        retry.epoch = {123};
        break;
    case 2:
        retry.base_revision = {123};
        break;
    case 3:
        retry.new_revision = {123};
        break;
    default:
        FAIL("unknown validation mutation");
    }
    const auto rejected = sink.enqueue(retry);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().code == error_code::DUPLICATE_OPERATION);
    CHECK(retry.snapshot == original.snapshot);
}

TEST_CASE("renderer budget changes account for pending visible and retired assets",
          "[validation]") {
    const auto bytes = validate_scene(*make_diagnostic_scene()).value();
    renderer_update_sink sink({bytes * 4, 250000, 1});
    for (const auto budget :
         {residency_budget{0, 1, 1}, residency_budget{1, 0, 1}, residency_budget{1, 1, 0}})
        CHECK_FALSE(sink.set_residency_budget(budget));
    auto first = batch_at({1}, {1}, true);
    const auto first_ticket = sink.enqueue(first);
    REQUIRE(first_ticket);
    CHECK_FALSE(sink.set_residency_budget({bytes - 1, 250000, 1}));
    auto second = batch_at({2}, {2});
    CHECK_FALSE(sink.enqueue(second));
    CHECK(second.snapshot);
    const auto active = require_next(sink);
    auto resync = batch_at({9}, {9}, true);
    CHECK_FALSE(sink.enqueue(resync));
    const auto second_ticket = sink.enqueue(second);
    REQUIRE(second_ticket);
    CHECK_FALSE(sink.enqueue(resync));
    sink.acknowledge_uploaded(*first_ticket, 1);
    sink.acknowledge_visible(*first_ticket, active.batch.snapshot, 1);
    make_visible(sink, *second_ticket, 2);
    CHECK_FALSE(sink.set_residency_budget({bytes, 250000, 1}));
    sink.retire_completed(2);
    CHECK_FALSE(sink.set_residency_budget({bytes, 1, 1}));
    REQUIRE(sink.set_residency_budget({bytes * 2, 250000, 1}));
    REQUIRE(sink.enqueue(resync));
    sink.cancel_pending();
    CHECK(sink.query_progress(*second_ticket)->state == update_state::RENDER_VISIBLE);
}

TEST_CASE("renderer ignores out-of-order acknowledgements and preserves the active update",
          "[validation]") {
    renderer_update_sink sink;
    CHECK_FALSE(sink.query_progress({999}));
    sink.acknowledge_uploaded({999}, 1);
    sink.acknowledge_visible({999}, nullptr, 1);
    auto first = batch_at({1}, {1}, true);
    const auto ticket = sink.enqueue(first);
    REQUIRE(ticket);
    sink.acknowledge_uploaded(*ticket, 1);
    CHECK(sink.query_progress(*ticket)->state == update_state::ACCEPTED);
    const auto active = require_next(sink);
    sink.acknowledge_visible(*ticket, active.batch.snapshot, 1);
    CHECK_FALSE(sink.visible_snapshot());
    sink.acknowledge_uploaded(*ticket, 1);
    sink.acknowledge_uploaded(*ticket, 2);
    CHECK(sink.query_progress(*ticket)->timeline_value == 1);
    const auto field = GENERATE(0, 1, 2, 3);
    auto wrong = std::make_shared<scene_state>(*active.batch.snapshot);
    switch (field) {
    case 0:
        wrong.reset();
        break;
    case 1:
        wrong->id = {123};
        break;
    case 2:
        wrong->epoch = {123};
        break;
    case 3:
        wrong->revision = {123};
        break;
    default:
        FAIL("unknown validation mutation");
    }
    sink.acknowledge_visible(*ticket, wrong, 1);
    CHECK_FALSE(sink.visible_snapshot());
    CHECK(sink.query_progress(*ticket)->state == update_state::UPLOADED);
    sink.acknowledge_visible(*ticket, active.batch.snapshot, 1);
    CHECK(sink.visible_snapshot() == active.batch.snapshot);
    CHECK(sink.queued_bytes() == 0);
}

TEST_CASE("renderer receipt eviction waits for retirement and requires resync for old tickets",
          "[validation][replay]") {
    renderer_update_sink sink;
    update_ticket first_ticket;
    for (std::uint64_t revision = 1; revision <= 256; ++revision) {
        auto batch = batch_at({revision}, {revision}, revision == 1);
        const auto ticket = sink.enqueue(batch);
        REQUIRE(ticket);
        if (revision == 1)
            first_ticket = *ticket;
        make_visible(sink, *ticket, revision);
    }
    auto next = batch_at({257}, {257});
    const auto rejected = sink.enqueue(next);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().code == error_code::WOULD_BLOCK);
    CHECK(next.snapshot);
    CHECK(sink.query_progress(first_ticket)->state == update_state::RENDER_VISIBLE);
    sink.retire_completed(256);
    REQUIRE(sink.enqueue(next));
    const auto evicted = sink.query_progress(first_ticket);
    REQUIRE_FALSE(evicted);
    CHECK(evicted.error().code == error_code::NOT_FOUND);
}
