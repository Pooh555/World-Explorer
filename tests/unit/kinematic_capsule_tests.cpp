#include "../fixtures/collision_support_fixture.hpp"
#include "world_explorer/collision/kinematic_capsule.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <limits>

using namespace world_explorer;
using namespace world_explorer::testing;
using namespace std::chrono_literals;

namespace {

std::shared_ptr<const JoltStaticCollision> capsule_shape(const SceneSnapshot& source,
                                                         const nlohmann::json& document) {
    const auto K_TEXT = document.dump();
    const auto K_SUPPORT = admit_collision_support(*source, K_TEXT, collision_digest(K_TEXT));
    REQUIRE(K_SUPPORT);
    const auto K_COOK = cook_synthetic_collision(source, *K_SUPPORT);
    REQUIRE(K_COOK);
    const auto K_SHAPE = build_jolt_static_collision(*K_COOK, *source);
    REQUIRE(K_SHAPE);
    return *K_SHAPE;
}

void check_same_state(SyntheticCapsuleState before, SyntheticCapsuleState after) {
    CHECK(before.m_feet.m_x == after.m_feet.m_x);
    CHECK(before.m_feet.m_y == after.m_feet.m_y);
    CHECK(before.m_feet.m_z == after.m_feet.m_z);
    CHECK(before.m_simulated_ticks == after.m_simulated_ticks);
}

}  // namespace

TEST_CASE("synthetic capsule uses an exact bounded 120 Hz clock", "[kinematic-capsule]") {
    auto source = std::make_shared<const SceneState>(collision_source());
    const auto K_SHAPE = capsule_shape(source, collision_document(*source));
    auto controller =
        SyntheticCapsuleController::create(K_SHAPE, *source, { .25, 1.7 }, { 1.5, 0, 1.5 });
    REQUIRE(controller);
    const auto K_ZERO = (*controller)->advance(*source, { 1, 0, 0 }, 0ns);
    REQUIRE(K_ZERO);
    CHECK(K_ZERO->m_steps == 0U);
    const auto K_ALMOST = (*controller)->advance(*source, { 1, 0, 0 }, 8333333ns);
    REQUIRE(K_ALMOST);
    CHECK(K_ALMOST->m_steps == 0U);
    const auto K_TICK = (*controller)->advance(*source, { 1, 0, 0 }, 1ns);
    REQUIRE(K_TICK);
    CHECK(K_TICK->m_steps == 1U);
    CHECK(K_TICK->m_state.m_feet.m_x == Catch::Approx(1.5 + 1.0 / 120.0).margin(1e-5));
    CHECK(K_TICK->m_state.m_feet.m_y == 0);
    const auto K_STALL = (*controller)->advance(*source, { 1, 0, 0 }, 1s);
    REQUIRE(K_STALL);
    CHECK(K_STALL->m_steps == 8U);
    CHECK(K_STALL->m_dropped_ticks == 112U);
    CHECK(K_STALL->m_state.m_simulated_ticks == 9U);
    CHECK(K_STALL->m_state.m_feet.m_x == Catch::Approx(1.575).margin(1e-5));
    const auto K_NO_DEBT = (*controller)->advance(*source, {}, 0ns);
    REQUIRE(K_NO_DEBT);
    CHECK(K_NO_DEBT->m_steps == 0U);
    CHECK(K_NO_DEBT->m_dropped_ticks == 0U);
    CHECK(walking_block_reason(*source) == WalkingBlockReason::SYNTHETIC_DIAGNOSTIC);
}

TEST_CASE("capsule swept walls slide while retaining occupied corner clearance",
          "[kinematic-capsule]") {
    auto source = std::make_shared<const SceneState>(collision_source(true));
    const auto K_DOCUMENT = collision_wall_document(*source);
    const auto K_SHAPE = capsule_shape(source, K_DOCUMENT);
    auto controller =
        SyntheticCapsuleController::create(K_SHAPE, *source, { .25, 1.7 }, { 2.5, 0, 1.3 });
    REQUIRE(controller);
    const auto K_SUPPORT_TEXT = K_DOCUMENT.dump();
    const auto K_SUPPORT =
        admit_collision_support(*source, K_SUPPORT_TEXT, collision_digest(K_SUPPORT_TEXT));
    REQUIRE(K_SUPPORT);
    for (std::size_t frame { 0uz }; frame < 100uz; ++frame) {
        const auto K_BEFORE = (*controller)->state();
        const auto K_FRAME = (*controller)->advance(*source, { 3, 0, .4 }, 10ms);
        REQUIRE(K_FRAME);
        const auto K_FEET = K_FRAME->m_state.m_feet;
        CHECK(K_FEET.m_x < 2.75);
        CHECK(K_FEET.m_y == 0);
        const auto K_CHECK =
            probe_collision_support(**K_SUPPORT, *source, { K_BEFORE.m_feet, K_FEET, .25, 1.7 });
        REQUIRE(K_CHECK);
        CHECK(K_CHECK->m_state == ClearanceState::CLEAR_DIAGNOSTIC);
    }
    const auto K_END = (*controller)->state();
    CHECK(K_END.m_feet.m_x > 2.70);
    CHECK(K_END.m_feet.m_z > 1.65);
    CHECK(K_END.m_simulated_ticks == 120U);
    // Approach the occupied corner from the face's back: the ledger blocks
    // even though only the NEG_X face was selected for Jolt geometry.
    auto corner = SyntheticCapsuleController::create(K_SHAPE, *source, { .25, 1.7 }, { 3.5, 0, 2.5 });
    REQUIRE(corner);
    bool blocked {};
    for (std::size_t frame { 0uz }; frame < 30uz; ++frame) {
        const auto K_FRAME = (*corner)->advance(*source, { -1, 0, -3 }, 10ms);
        REQUIRE(K_FRAME);
        blocked = blocked || K_FRAME->m_clearance == ClearanceState::OCCUPIED;
        CHECK(K_FRAME->m_state.m_feet.m_z > 2.25);
    }
    CHECK(blocked);
}

TEST_CASE("capsule cannot tunnel through unknown or unsupported cells", "[kinematic-capsule]") {
    auto source = std::make_shared<const SceneState>(collision_source());
    auto document = collision_document(*source);
    document["grid"]["states"].get_ref<std::string&>()[3uz + 8uz * (0uz + 4uz * 1uz)] = '?';
    const auto K_SHAPE = capsule_shape(source, document);
    auto controller =
        SyntheticCapsuleController::create(K_SHAPE, *source, { .25, 1.7 }, { 2.5, 0, 1.5 });
    REQUIRE(controller);
    bool blocked {};
    for (std::size_t frame { 0uz }; frame < 50uz; ++frame) {
        const auto K_FRAME = (*controller)->advance(*source, { 10, 0, 0 }, 20ms);
        REQUIRE(K_FRAME);
        blocked = blocked || K_FRAME->m_clearance == ClearanceState::UNKNOWN;
        CHECK(K_FRAME->m_state.m_feet.m_x < 2.75);
    }
    CHECK(blocked);
    CHECK((*controller)->state().m_simulated_ticks == 120U);
    const auto K_BAD_SPAWN =
        SyntheticCapsuleController::create(K_SHAPE, *source, { .25, 1.7 }, { 3.5, 0, 1.5 });
    REQUIRE_FALSE(K_BAD_SPAWN);
    CHECK(K_BAD_SPAWN.error().m_code == ErrorCode::WOULD_BLOCK);
    auto unsupported =
        SyntheticCapsuleController::create(K_SHAPE, *source, { .25, 1.7 }, { 2.5, 0, 3.5 });
    REQUIRE(unsupported);
    bool unsupported_floor {};
    for (std::size_t frame { 0uz }; frame < 50uz; ++frame) {
        const auto K_FRAME = (*unsupported)->advance(*source, { 5, 0, 0 }, 10ms);
        REQUIRE(K_FRAME);
        unsupported_floor =
            unsupported_floor || K_FRAME->m_clearance == ClearanceState::UNSUPPORTED_FLOOR;
        CHECK(K_FRAME->m_state.m_feet.m_x < 3.75);
    }
    CHECK(unsupported_floor);
}

TEST_CASE("capsule sweeps stop at every admitted wall orientation", "[kinematic-capsule]") {
    const std::string_view K_FACE { GENERATE("NEG_X", "POS_X", "NEG_Z", "POS_Z") };
    auto source = std::make_shared<const SceneState>(collision_source(true, false, K_FACE));
    const auto K_SHAPE = capsule_shape(source, collision_wall_document(*source, K_FACE));
    Vec3d start { 2.5, 0, 1.5 }, velocity { 10, 0, 0 };
    if (K_FACE == "POS_X") {
        start = { 4.5, 0, 1.5 };
        velocity = { -10, 0, 0 };
    } else if (K_FACE == "NEG_Z") {
        start = { 3.5, 0, .5 };
        velocity = { 0, 0, 10 };
    } else if (K_FACE == "POS_Z") {
        start = { 3.5, 0, 2.5 };
        velocity = { 0, 0, -10 };
    }
    auto controller = SyntheticCapsuleController::create(K_SHAPE, *source, { .25, 1.7 }, start);
    REQUIRE(controller);
    for (std::size_t frame { 0uz }; frame < 20uz; ++frame) {
        REQUIRE((*controller)->advance(*source, velocity, 20ms));
    }
    const auto K_END = (*controller)->state().m_feet;
    const double K_DISTANCE { K_FACE == "NEG_X"   ? 3 - K_END.m_x
                             : K_FACE == "POS_X" ? K_END.m_x - 4
                             : K_FACE == "NEG_Z" ? 1 - K_END.m_z
                                                 : K_END.m_z - 2 };
    CHECK(K_DISTANCE > .25);
    CHECK(K_DISTANCE < .27);
}

TEST_CASE("capsule failures preserve owner position and fractional clock", "[kinematic-capsule]") {
    auto source = std::make_shared<const SceneState>(collision_source(true));
    const auto K_SHAPE = capsule_shape(source, collision_wall_document(*source));
    auto controller =
        SyntheticCapsuleController::create(K_SHAPE, *source, { .25, 1.7 }, { 1.5, 0, 1.5 });
    REQUIRE(controller);
    REQUIRE((*controller)->advance(*source, {}, 8ms));
    const auto K_BEFORE = (*controller)->state();
    auto corrected = *source;
    ++corrected.m_revision.m_value;
    const auto K_STALE = (*controller)->advance(corrected, { 1, 0, 0 }, 1s);
    REQUIRE_FALSE(K_STALE);
    CHECK(K_STALE.error().m_code == ErrorCode::WRONG_BASE);
    corrected = *source;
    ++corrected.m_epoch.m_value;
    const auto K_EPOCH = (*controller)->advance(corrected, { 1, 0, 0 }, 1s);
    REQUIRE_FALSE(K_EPOCH);
    CHECK(K_EPOCH.error().m_code == ErrorCode::WRONG_EPOCH);
    corrected = *source;
    auto wall = std::make_shared<SceneChunk>(*corrected.m_chunks.at(ChunkId { 9007199254740994ULL }));
    ++wall->m_pose_revision.m_value;
    corrected.m_chunks[wall->m_id] = wall;
    REQUIRE_FALSE((*controller)->advance(corrected, { 1, 0, 0 }, 1s));
    std::stop_source cancelled;
    cancelled.request_stop();
    const auto K_STOPPED = (*controller)->advance(*source, { 1, 0, 0 }, 1s, cancelled.get_token());
    REQUIRE_FALSE(K_STOPPED);
    CHECK(K_STOPPED.error().m_code == ErrorCode::CANCELLED);
    REQUIRE_FALSE(SyntheticCapsuleController::create(
        K_SHAPE, *source, { .25, 1.7 }, { 1.5, 0, 1.5 }, cancelled.get_token()));
    REQUIRE_FALSE((*controller)->advance(*source, { 11, 0, 0 }, 1ms));
    REQUIRE_FALSE((*controller)->advance(*source, { 0, 1, 0 }, 1ms));
    REQUIRE_FALSE(
        (*controller)->advance(*source, { std::numeric_limits<double>::quiet_NaN(), 0, 0 }, 1ms));
    REQUIRE_FALSE((*controller)->advance(*source, {}, -1ns));
    REQUIRE_FALSE((*controller)->advance(*source, {}, 61s));
    check_same_state(K_BEFORE, (*controller)->state());
    const auto K_RECOVERED = (*controller)->advance(*source, { 1, 0, 0 }, 333334ns);
    REQUIRE(K_RECOVERED);
    CHECK(K_RECOVERED->m_steps == 1U);
    CHECK(K_RECOVERED->m_state.m_feet.m_x > 1.5);
    const auto K_RAY = K_SHAPE->ray_cast(*source, { 2, .5, 1.5 }, { 4, .5, 1.5 });
    REQUIRE(K_RAY);
    REQUIRE(K_RAY->has_value());
    CHECK(K_RAY->value_or(-1.0) == Catch::Approx(.5));
    REQUIRE_FALSE(SyntheticCapsuleController::create({}, *source, { .25, 1.7 }, { 1.5, 0, 1.5 }));
    REQUIRE_FALSE(SyntheticCapsuleController::create(K_SHAPE, *source, { -.25, 1.7 }, { 1.5, 0, 1.5 }));
    REQUIRE_FALSE(SyntheticCapsuleController::create(K_SHAPE, *source, { .25, .4 }, { 1.5, 0, 1.5 }));
    REQUIRE_FALSE(SyntheticCapsuleController::create(K_SHAPE, *source, { .25, 1.7 }, { 1.5, .1, 1.5 }));
}
