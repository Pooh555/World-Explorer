#include "world_explorer/application/scenes.hpp"
#include "world_explorer/render_core/update_sink.hpp"
#include <array>
#include <barrier>
#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <thread>
#include <utility>

using namespace world_explorer;
namespace {
constexpr std::size_t PRODUCERS = 4;
scene_delta_batch batch(std::uint64_t revision, std::uint64_t operation = 0) {
    auto scene = std::make_shared<scene_state>(*make_diagnostic_scene());
    scene->revision = {revision};
    auto result = make_snapshot_batch(scene, {operation ? operation : revision}).value();
    result.base_revision = {revision - 1};
    result.full_snapshot = revision == 1;
    return result;
}
void publish(renderer_update_sink& sink, const admitted_update& active) {
    const auto timeline = active.batch.new_revision.value;
    sink.acknowledge_uploaded(active.ticket, timeline);
    sink.acknowledge_visible(active.ticket, active.batch.snapshot, timeline);
}
admitted_update require_next(renderer_update_sink& sink) {
    if (auto active = sink.take_next())
        return std::move(*active);
    FAIL("expected a queued renderer update");
    return {};
}
void initialize(renderer_update_sink& sink) {
    auto first = batch(1);
    REQUIRE(sink.enqueue(first));
    publish(sink, require_next(sink));
}

// Assertions remain on the test owner. Each producer writes only its own slot;
// the barrier releases all participants together and joins publish their results.
template <class consumer>
auto race_admission(renderer_update_sink& sink,
                    std::array<scene_delta_batch, PRODUCERS>& candidates, consumer consume) {
    std::array<result<update_ticket>, PRODUCERS> receipts;
    std::barrier start(static_cast<std::ptrdiff_t>(PRODUCERS + 1));
    std::array<std::jthread, PRODUCERS> producers;
    for (std::size_t index = 0; index < PRODUCERS; ++index)
        producers[index] = std::jthread([&, index] {
            start.arrive_and_wait();
            receipts[index] = sink.enqueue(candidates[index]);
        });
    start.arrive_and_wait();
    consume();
    for (auto& producer : producers)
        producer.join();
    return receipts;
}
auto copies(const scene_delta_batch& original) {
    std::array<scene_delta_batch, PRODUCERS> candidates;
    candidates.fill(original);
    return candidates;
}
void check_preserved(const scene_delta_batch& candidate, const scene_delta_batch& original) {
    CHECK(candidate.snapshot == original.snapshot);
    CHECK(candidate.id == original.id);
    CHECK(candidate.epoch == original.epoch);
    CHECK(candidate.base_revision == original.base_revision);
    CHECK(candidate.new_revision == original.new_revision);
    CHECK(candidate.operation == original.operation);
    CHECK(candidate.full_snapshot == original.full_snapshot);
    CHECK(candidate.upload_bytes == original.upload_bytes);
}
} // namespace

TEST_CASE("concurrent renderer producers share one receipt while the render owner consumes",
          "[render_threads]") {
    const auto bytes = validate_scene(*make_diagnostic_scene()).value();
    renderer_update_sink sink({bytes * 3, 250000, 1});
    initialize(sink);
    const auto original_visible = sink.visible_snapshot();
    const auto original_hash = scene_content_hash(*original_visible);
    for (std::uint64_t revision = 2; revision <= 33; ++revision) {
        CAPTURE(revision);
        const auto original = batch(revision);
        auto candidates = copies(original);
        std::optional<admitted_update> active;
        const auto receipts = race_admission(sink, candidates, [&] {
            active = sink.take_next();
            sink.retire_completed(revision - 1);
            const auto visible = sink.visible_snapshot();
            CHECK(visible);
            if (visible)
                CHECK(visible->revision.value == revision - 1);
        });
        const auto admitted = active ? std::move(*active) : require_next(sink);
        std::size_t moved{};
        for (std::size_t index = 0; index < PRODUCERS; ++index) {
            REQUIRE(receipts[index]);
            CHECK(*receipts[index] == admitted.ticket);
            if (!candidates[index].snapshot)
                ++moved;
            else
                check_preserved(candidates[index], original);
        }
        CHECK(moved == 1);
        CHECK_FALSE(sink.take_next());
        CHECK(sink.queued_bytes() == bytes);
        CHECK(admitted.batch.snapshot == original.snapshot);
        auto retry_candidates = copies(original);
        const auto retries =
            race_admission(sink, retry_candidates, [&] { publish(sink, admitted); });
        for (std::size_t index = 0; index < PRODUCERS; ++index) {
            REQUIRE(retries[index]);
            CHECK(*retries[index] == admitted.ticket);
            check_preserved(retry_candidates[index], original);
        }
        CHECK(sink.query_progress(admitted.ticket)->state == update_state::RENDER_VISIBLE);
        CHECK(sink.visible_snapshot() == original.snapshot);
        CHECK(sink.queued_bytes() == 0);
        CHECK(scene_content_hash(*original_visible) == original_hash);
    }
    sink.retire_completed(33);
    CHECK(sink.set_residency_budget({bytes, 250000, 1}));
}

TEST_CASE("concurrent renderer cancellation preserves admitted or cancelled retry identity",
          "[render_threads]") {
    for (unsigned iteration = 0; iteration < 32; ++iteration) {
        CAPTURE(iteration);
        renderer_update_sink sink;
        initialize(sink);
        const auto visible = sink.visible_snapshot();
        const auto original = batch(2);
        auto candidates = copies(original);
        std::optional<admitted_update> active;
        const auto receipts = race_admission(sink, candidates, [&] {
            active = sink.take_next();
            sink.cancel_pending();
            CHECK(sink.visible_snapshot() == visible);
        });
        REQUIRE(receipts[0]);
        std::size_t moved{};
        for (std::size_t index = 0; index < PRODUCERS; ++index) {
            REQUIRE(receipts[index]);
            CHECK(*receipts[index] == *receipts[0]);
            if (!candidates[index].snapshot)
                ++moved;
            else
                check_preserved(candidates[index], original);
        }
        CHECK(moved == 1);
        if (!active)
            active = sink.take_next();
        const auto progress = sink.query_progress(*receipts[0]);
        REQUIRE(progress);
        if (active) {
            CHECK(active.value().ticket == *receipts[0]);
            CHECK(progress->state == update_state::ASSETS_AVAILABLE);
            sink.cancel_pending();
            publish(sink, active.value());
            CHECK(sink.visible_snapshot() == original.snapshot);
            CHECK(sink.query_progress(active.value().ticket)->state ==
                  update_state::RENDER_VISIBLE);
        } else {
            CHECK(progress->state == update_state::CANCELLED);
            CHECK(sink.visible_snapshot() == visible);
        }
        CHECK(sink.queued_bytes() == 0);
        auto retry = original;
        const auto ticket = sink.enqueue(retry);
        REQUIRE(ticket);
        CHECK(*ticket == *receipts[0]);
        check_preserved(retry, original);
        CHECK_FALSE(sink.take_next());
    }
}

TEST_CASE("concurrent renderer backpressure preserves callers through pending cancellation",
          "[render_threads]") {
    const auto bytes = validate_scene(*make_diagnostic_scene()).value();
    for (unsigned iteration = 0; iteration < 16; ++iteration) {
        CAPTURE(iteration);
        renderer_update_sink sink({bytes * 3, 250000, 1});
        initialize(sink);
        const auto visible = sink.visible_snapshot();
        auto second = batch(2);
        REQUIRE(sink.enqueue(second));
        const auto active = require_next(sink);
        auto third = batch(3);
        const auto pending = sink.enqueue(third);
        REQUIRE(pending);
        const auto fourth = batch(4);
        auto candidates = copies(fourth);
        const auto receipts = race_admission(sink, candidates, [&] {
            CHECK_FALSE(sink.take_next());
            sink.retire_completed(1);
            CHECK(sink.query_progress(*pending)->state == update_state::ACCEPTED);
            CHECK(sink.visible_snapshot() == visible);
        });
        for (std::size_t index = 0; index < PRODUCERS; ++index) {
            REQUIRE_FALSE(receipts[index]);
            CHECK(receipts[index].error().code == error_code::WOULD_BLOCK);
            check_preserved(candidates[index], fourth);
        }
        sink.cancel_pending();
        CHECK(sink.query_progress(*pending)->state == update_state::CANCELLED);
        CHECK(sink.query_progress(active.ticket)->state == update_state::ASSETS_AVAILABLE);
        CHECK(sink.queued_bytes() == bytes);
        publish(sink, active);
        auto stale = fourth;
        const auto rejected = sink.enqueue(stale);
        REQUIRE_FALSE(rejected);
        CHECK(rejected.error().code == error_code::RESYNC_REQUIRED);
        check_preserved(stale, fourth);
        auto corrected = batch(3, fourth.operation.value);
        REQUIRE(sink.enqueue(corrected));
        CHECK_FALSE(corrected.snapshot);
        sink.cancel_pending();
        CHECK(sink.visible_snapshot() == active.batch.snapshot);
        CHECK(sink.queued_bytes() == 0);
        sink.retire_completed(2);
        CHECK(sink.set_residency_budget({bytes, 250000, 1}));
    }
}
