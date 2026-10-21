#include "../fixtures/collision_support_fixture.hpp"
#include "world_explorer/collision/kinematic_capsule.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>

using namespace world_explorer;
using namespace world_explorer::testing;
using namespace std::chrono_literals;

namespace {

Result<std::shared_ptr<const CollisionSupport>> ramp_support(const SceneState& source,
                                                             const nlohmann::json& document) {
    const auto K_TEXT = document.dump();
    return admit_collision_support(source, K_TEXT, collision_digest(K_TEXT));
}

Vec3d ramp_feet(double x, double z, double rise_x, double rise_z) {
    return { x, collision_ramp_feet_y(x, z, rise_x, rise_z), z };
}

}  // namespace

TEST_CASE("inclined source plane admission binds geometry and tilted space volumes",
          "[collision-ramp]") {
    const auto K_SOURCE = collision_source(false, false, "NEG_X", 1.F, 0);
    auto document = collision_ramp_document(K_SOURCE, 1, 0);
    const auto K_SUPPORT = ramp_support(K_SOURCE, document);
    REQUIRE(K_SUPPORT);
    CHECK((*K_SUPPORT)->format_version() == 3U);
    CHECK((*K_SUPPORT)->floor_gradient().m_x == 1);
    const auto K_FEET = ramp_feet(1.5, 1.5, 1, 0);
    const auto K_END = ramp_feet(2.5, 1.5, 1, 0);
    const auto K_CLEAR = probe_collision_support(**K_SUPPORT, K_SOURCE, { K_FEET, K_END, .25, 1.7 });
    REQUIRE(K_CLEAR);
    CHECK(K_CLEAR->m_state == ClearanceState::CLEAR_DIAGNOSTIC);
    auto below = K_FEET;
    below.m_y = 1.5 + .25 * (std::sqrt(2.) - 1) - 1e-6;
    const auto K_BELOW = probe_collision_support(**K_SUPPORT, K_SOURCE, { below, below, .25, 1.7 });
    REQUIRE(K_BELOW);
    CHECK(K_BELOW->m_state == ClearanceState::UNSUPPORTED_FLOOR);
    auto floating = K_FEET;
    floating.m_y += .011;
    const auto K_FLOATING =
        probe_collision_support(**K_SUPPORT, K_SOURCE, { floating, floating, .25, 1.7 });
    REQUIRE(K_FLOATING);
    CHECK(K_FLOATING->m_state == ClearanceState::UNSUPPORTED_FLOOR);
    // The top cap crosses relative-height band 2 although the center's
    // height plus 1.85 remains below that band. A center-only test misses it.
    auto& cells = document["grid"]["states"].get_ref<std::string&>();
    cells[1uz + 8uz * (2uz + 4uz * 1uz)] = '?';
    const auto K_UNKNOWN = ramp_support(K_SOURCE, document);
    REQUIRE(K_UNKNOWN);
    CHECK(K_FEET.m_y - 1.5 + 1.85 < 2);
    const auto K_CAP = probe_collision_support(**K_UNKNOWN, K_SOURCE, { K_FEET, K_FEET, .25, 1.85 });
    REQUIRE(K_CAP);
    CHECK(K_CAP->m_state == ClearanceState::UNKNOWN);
    const auto K_SHORT = probe_collision_support(**K_UNKNOWN, K_SOURCE, { K_FEET, K_FEET, .25, 1.7 });
    REQUIRE(K_SHORT);
    CHECK(K_SHORT->m_state == ClearanceState::CLEAR_DIAGNOSTIC);
    cells[1uz + 8uz * (2uz + 4uz * 1uz)] = '#';
    const auto K_OCCUPIED = ramp_support(K_SOURCE, document);
    REQUIRE(K_OCCUPIED);
    const auto K_BLOCKED =
        probe_collision_support(**K_OCCUPIED, K_SOURCE, { K_FEET, K_FEET, .25, 1.85 });
    REQUIRE(K_BLOCKED);
    CHECK(K_BLOCKED->m_state == ClearanceState::OCCUPIED);
}

TEST_CASE("Jolt capsule follows upward downward and diagonal source ramps", "[collision-ramp]") {
    const unsigned K_CASE { GENERATE(0U, 1U, 2U, 3U, 4U) };
    const std::array<std::array<float, 2uz>, 5uz> K_GRADIENTS {
        { { 1, 0 }, { -1, 0 }, { 0, 1 }, { .5F, .75F }, { 1.F - 1.F / 1024.F, 0 } } };
    const auto K_GRADIENT = K_GRADIENTS[K_CASE];
    auto source = std::make_shared<const SceneState>(
        collision_source(false, false, "NEG_X", K_GRADIENT[0uz], K_GRADIENT[1uz]));
    const auto K_SUPPORT =
        ramp_support(*source, collision_ramp_document(*source, K_GRADIENT[0uz], K_GRADIENT[1uz]));
    REQUIRE(K_SUPPORT);
    const auto K_COOK = cook_synthetic_collision(source, *K_SUPPORT);
    REQUIRE(K_COOK);
    CHECK((*K_COOK)->parameters().m_format_version == 3U);
    CHECK((*K_COOK)->triangles().size() == 2uz);
    const auto K_SHAPE = build_jolt_static_collision(*K_COOK, *source);
    REQUIRE(K_SHAPE);
    const auto K_START = ramp_feet(1.5, 1.5, K_GRADIENT[0uz], K_GRADIENT[1uz]);
    auto controller = SyntheticCapsuleController::create(*K_SHAPE, *source, { .25, 1.7 }, K_START);
    REQUIRE(controller);
    const Vec3d K_VELOCITY { K_CASE == 2U ? 0. : 1., 0, K_CASE == 2U ? 1. : 0. };
    for (std::size_t frame { 0uz }; frame < 50uz; ++frame) {
        const auto K_BEFORE = (*controller)->state().m_feet;
        const auto K_FRAME = (*controller)->advance(*source, K_VELOCITY, 10ms);
        REQUIRE(K_FRAME);
        CHECK(K_FRAME->m_clearance == ClearanceState::CLEAR_DIAGNOSTIC);
        const auto K_END = K_FRAME->m_state.m_feet;
        const auto K_CHECK =
            probe_collision_support(**K_SUPPORT, *source, { K_BEFORE, K_END, .25, 1.7 });
        REQUIRE(K_CHECK);
        CHECK(K_CHECK->m_state == ClearanceState::CLEAR_DIAGNOSTIC);
        // Independent plane-distance oracle for the entire lower cap.
        const Vec3d K_NORMAL { -K_GRADIENT[0uz], 1, -K_GRADIENT[1uz] };
        const auto K_CENTRE = K_END + Vec3d { 0, .25, 0 };
        CHECK(dot(K_NORMAL, K_CENTRE) / std::sqrt(dot(K_NORMAL, K_NORMAL)) >= .25);
    }
    const auto K_END = (*controller)->state().m_feet;
    CHECK((*controller)->state().m_simulated_ticks == 60U);
    const double K_DISTANCE { K_CASE == 2U ? K_END.m_z - K_START.m_z : K_END.m_x - K_START.m_x };
    CHECK(K_DISTANCE > .44);
    CHECK(K_DISTANCE < .56);
    for (std::size_t frame { 0uz }; frame < 50uz; ++frame) {
        REQUIRE((*controller)->advance(*source, {}, 10ms));
    }
    CHECK((*controller)->state().m_feet.m_x == Catch::Approx(K_END.m_x).margin(1e-5));
    CHECK((*controller)->state().m_feet.m_z == Catch::Approx(K_END.m_z).margin(1e-5));
    if (K_CASE == 1U) {
        CHECK(K_END.m_y < K_START.m_y - .44);
    } else {
        CHECK(K_END.m_y > K_START.m_y + .20);
    }
    CHECK(walking_block_reason(*source) == WalkingBlockReason::SYNTHETIC_DIAGNOSTIC);
}

TEST_CASE("steeper than 45 degree source geometry cannot spawn a walking capsule",
          "[collision-ramp]") {
    constexpr float K_RISE { 1.F + 1.F / 1024.F };
    auto source =
        std::make_shared<const SceneState>(collision_source(false, false, "NEG_X", K_RISE, 0));
    const auto K_SUPPORT = ramp_support(*source, collision_ramp_document(*source, K_RISE, 0));
    REQUIRE(K_SUPPORT);
    CHECK(std::ranges::none_of((*K_SUPPORT)->floors(), [](auto flag) {
        return flag != 0;
    }));
    const auto K_COOK = cook_synthetic_collision(source, *K_SUPPORT);
    REQUIRE(K_COOK);
    const auto K_SHAPE = build_jolt_static_collision(*K_COOK, *source);
    REQUIRE(K_SHAPE);
    const auto K_CONTROLLER = SyntheticCapsuleController::create(
        *K_SHAPE, *source, { .25, 1.7 }, ramp_feet(1.5, 1.5, K_RISE, 0));
    REQUIRE(!K_CONTROLLER);
    CHECK(K_CONTROLLER.error().m_code == ErrorCode::WOULD_BLOCK);
}

TEST_CASE("inclined wall faces stay source pinned in all four directions", "[collision-ramp]") {
    const std::string_view K_FACE { GENERATE("NEG_X", "POS_X", "NEG_Z", "POS_Z") };
    auto source =
        std::make_shared<const SceneState>(collision_source(true, false, K_FACE, .5F, .25F));
    const auto K_SUPPORT =
        ramp_support(*source, collision_ramp_document(*source, .5, .25, true, K_FACE));
    REQUIRE(K_SUPPORT);
    const auto K_COOK = cook_synthetic_collision(source, *K_SUPPORT);
    REQUIRE(K_COOK);
    CHECK((*K_COOK)->wall_triangle_count() == 2uz);
    const auto K_SHAPE = build_jolt_static_collision(*K_COOK, *source);
    REQUIRE(K_SHAPE);
    Vec3d start { 2, 0, 1.5 }, end { 4, 0, 1.5 };
    if (K_FACE == "POS_X") {
        start = { 5, 0, 1.5 };
        end = { 3, 0, 1.5 };
    } else if (K_FACE == "NEG_Z") {
        start = { 3.5, 0, 0 };
        end = { 3.5, 0, 2 };
    } else if (K_FACE == "POS_Z") {
        start = { 3.5, 0, 3 };
        end = { 3.5, 0, 1 };
    }
    start.m_y = .5 * start.m_x + .25 * start.m_z + .5;
    end.m_y = .5 * end.m_x + .25 * end.m_z + .5;
    const auto K_HIT = (*K_SHAPE)->ray_cast(*source, start, end);
    REQUIRE(K_HIT);
    REQUIRE(K_HIT->has_value());
    CHECK(K_HIT->value_or(-1) == Catch::Approx(.5));
}

TEST_CASE("inclined support rejects malformed or foreign plane proofs", "[collision-ramp]") {
    const unsigned K_MUTATION { GENERATE(0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U) };
    const auto K_SOURCE = collision_source(true, false, "NEG_X", .5F, 0);
    auto document = collision_ramp_document(K_SOURCE, .5, 0, true);
    switch (K_MUTATION) {
    case 0:
        document["plane"]["rise_x"] = .500001;
        break;
    case 1:
        document.erase("plane");
        break;
    case 2:
        document["plane"]["rise_x"] = ".5";
        break;
    case 3:
        document["plane"]["rise_z"] = 5;
        break;
    case 4:
        document["plane"]["extra"] = 0;
        break;
    case 5:
        document["schema"] = "SYNTHETIC_COLLISION_SUPPORT_V2";
        break;
    case 6:
        document["walls"] = nullptr;
        break;
    case 7:
        document["walls"].push_back(document["walls"][0uz]);
        break;
    default:
        throw std::runtime_error("Unknown ramp mutation");
    }
    CHECK(!ramp_support(K_SOURCE, document));
}

TEST_CASE("ramp cancellation and source corrections preserve the admitted owner",
          "[collision-ramp]") {
    auto source =
        std::make_shared<const SceneState>(collision_source(false, false, "NEG_X", .5F, 0));
    const auto K_DOCUMENT = collision_ramp_document(*source, .5, 0);
    const auto K_SUPPORT = ramp_support(*source, K_DOCUMENT);
    REQUIRE(K_SUPPORT);
    const auto K_COOK = cook_synthetic_collision(source, *K_SUPPORT);
    REQUIRE(K_COOK);
    const auto K_SHAPE = build_jolt_static_collision(*K_COOK, *source);
    REQUIRE(K_SHAPE);
    auto controller = SyntheticCapsuleController::create(
        *K_SHAPE, *source, { .25, 1.7 }, ramp_feet(1.5, 1.5, .5, 0));
    REQUIRE(controller);
    REQUIRE((*controller)->advance(*source, { 1, 0, 0 }, 10ms));
    const auto K_BEFORE = (*controller)->state();
    auto corrected = *source;
    ++corrected.m_revision.m_value;
    REQUIRE(!(*controller)->advance(corrected, { 1, 0, 0 }, 1s));
    std::stop_source cancelled;
    cancelled.request_stop();
    REQUIRE(!(*controller)->advance(*source, { 1, 0, 0 }, 1s, cancelled.get_token()));
    CHECK((*controller)->state().m_feet.m_y == K_BEFORE.m_feet.m_y);
    CHECK((*controller)->state().m_simulated_ticks == K_BEFORE.m_simulated_ticks);
    const auto K_AGAIN = (*controller)->advance(*source, { 1, 0, 0 }, 10ms);
    REQUIRE(K_AGAIN);
    CHECK(K_AGAIN->m_state.m_feet.m_x > K_BEFORE.m_feet.m_x);
}

TEST_CASE("inclined wall response retains sliding and blocks the occupied corner",
          "[collision-ramp]") {
    auto source =
        std::make_shared<const SceneState>(collision_source(true, false, "NEG_X", .5F, 0));
    const auto K_SUPPORT = ramp_support(*source, collision_ramp_document(*source, .5, 0, true));
    REQUIRE(K_SUPPORT);
    const auto K_COOK = cook_synthetic_collision(source, *K_SUPPORT);
    REQUIRE(K_COOK);
    const auto K_SHAPE = build_jolt_static_collision(*K_COOK, *source);
    REQUIRE(K_SHAPE);
    auto controller = SyntheticCapsuleController::create(
        *K_SHAPE, *source, { .25, 1.7 }, ramp_feet(2.5, 1.3, .5, 0));
    REQUIRE(controller);
    for (std::size_t frame { 0uz }; frame < 100uz; ++frame) {
        const auto K_ADVANCE = (*controller)->advance(*source, { 3, 0, .4 }, 10ms);
        REQUIRE(K_ADVANCE);
        CHECK(K_ADVANCE->m_clearance == ClearanceState::CLEAR_DIAGNOSTIC);
        CHECK(K_ADVANCE->m_state.m_feet.m_x < 2.75);
    }
    CHECK((*controller)->state().m_feet.m_z > 1.65);
    auto corner = SyntheticCapsuleController::create(
        *K_SHAPE, *source, { .25, 1.7 }, ramp_feet(3.5, 2.5, .5, 0));
    REQUIRE(corner);
    bool blocked {};
    for (std::size_t frame { 0uz }; frame < 30uz; ++frame) {
        const auto K_ADVANCE = (*corner)->advance(*source, { -1, 0, -3 }, 10ms);
        REQUIRE(K_ADVANCE);
        blocked = blocked || K_ADVANCE->m_clearance == ClearanceState::OCCUPIED;
        CHECK(K_ADVANCE->m_state.m_feet.m_z > 2.25);
    }
    CHECK(blocked);
}
