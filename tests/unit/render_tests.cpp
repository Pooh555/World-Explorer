#include "world_explorer/application/scenes.hpp"
#include "world_explorer/render_core/update_sink.hpp"
#include <catch2/catch_test_macros.hpp>
using namespace world_explorer;
namespace {
scene_delta_batch batch_at(std::uint64_t revision, std::uint64_t operation, bool full = false) {
    auto scene = std::make_shared<scene_state>(*make_diagnostic_scene());
    scene->revision = {revision};
    auto batch = make_snapshot_batch(scene, {operation}).value();
    batch.full_snapshot = full;
    batch.base_revision = {revision - 1};
    return batch;
}
void make_visible(renderer_update_sink& sink, update_ticket ticket, std::uint64_t timeline) {
    auto admitted = sink.take_next();
    REQUIRE(admitted);
    REQUIRE(admitted->ticket == ticket);
    sink.acknowledge_uploaded(ticket, timeline);
    sink.acknowledge_visible(ticket, admitted->batch.snapshot, timeline);
}
} // namespace
TEST_CASE("renderer admission is idempotent and rejects stale or modified retries") {
    renderer_update_sink sink;
    auto initial = batch_at(1, 1, true);
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
    auto stale = batch_at(3, 3);
    REQUIRE_FALSE(sink.enqueue(stale));
    REQUIRE(stale.snapshot);
    auto wrong_epoch = batch_at(2, 2);
    wrong_epoch.epoch = {1};
    REQUIRE_FALSE(sink.enqueue(wrong_epoch));
}
TEST_CASE("old residency remains charged until GPU retirement and duplicate ack is harmless") {
    const auto bytes = validate_scene(*make_diagnostic_scene()).value();
    renderer_update_sink sink({bytes * 2, 250000, 4});
    auto first = batch_at(1, 1, true);
    auto first_ticket = sink.enqueue(first);
    REQUIRE(first_ticket);
    make_visible(sink, *first_ticket, 1);
    auto second = batch_at(2, 2);
    auto second_ticket = sink.enqueue(second);
    REQUIRE(second_ticket);
    make_visible(sink, *second_ticket, 2);
    sink.acknowledge_visible(*second_ticket, sink.visible_snapshot(), 2);
    REQUIRE(sink.queued_bytes() == 0);
    auto third = batch_at(3, 3);
    REQUIRE_FALSE(sink.enqueue(third));
    sink.retire_completed(1);
    REQUIRE_FALSE(sink.enqueue(third));
    sink.retire_completed(2);
    REQUIRE(sink.enqueue(third));
    REQUIRE(sink.query_progress(*first_ticket)->state == update_state::RETIRED);
}
TEST_CASE("cancelling queued updates preserves the in-flight revision chain") {
    renderer_update_sink sink;
    auto first = batch_at(1, 1, true);
    auto ticket = sink.enqueue(first);
    REQUIRE(ticket);
    const auto active = sink.take_next();
    REQUIRE(active);
    auto second = batch_at(2, 2);
    REQUIRE(sink.enqueue(second));
    sink.cancel_pending();
    REQUIRE_FALSE(sink.take_next());
    sink.acknowledge_uploaded(*ticket, 1);
    sink.acknowledge_visible(*ticket, active->batch.snapshot, 1);
    auto replacement = batch_at(2, 3);
    REQUIRE(sink.enqueue(replacement));
    auto uploading = sink.take_next();
    REQUIRE(uploading);
    sink.fail_update(uploading->ticket, {error_code::BACKEND_FAILURE, "injected upload failure"});
    REQUIRE(sink.visible_snapshot()->revision.value == 1);
    REQUIRE(sink.queued_bytes() == 0);
    auto resync = batch_at(5, 4, true);
    REQUIRE(sink.enqueue(resync));
}
