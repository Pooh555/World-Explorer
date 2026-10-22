#include "../fixtures/collision_support_fixture.hpp"
#include "world_explorer/collision/kinematic_capsule.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

using namespace world_explorer;
using namespace world_explorer::testing;

namespace {

double minimum_corner_distance_squared(const CapsuleSweep& sweep, double upper) {
    const double K_X { 4 - sweep.m_start_feet.m_x };
    const double K_DX { sweep.m_start_feet.m_x - sweep.m_end_feet.m_x };
    const double K_Y { sweep.m_start_feet.m_y + sweep.m_radius - upper };
    const double K_DY { sweep.m_end_feet.m_y - sweep.m_start_feet.m_y };
    std::vector<double> breaks { 0, 1 };
    for (const auto K_PAIR :
         { std::array<double, 2uz> { K_X, K_DX }, std::array<double, 2uz> { K_Y, K_DY } }) {
        if (K_PAIR[1uz] != 0) {
            const double K_ROOT { -K_PAIR[0uz] / K_PAIR[1uz] };
            if (K_ROOT > 0 && K_ROOT < 1) {
                breaks.push_back(K_ROOT);
            }
        }
    }
    std::sort(breaks.begin(), breaks.end());
    const auto K_DISTANCE = [&](double t) {
        const double K_A { std::max(0., K_X + t * K_DX) };
        const double K_B { std::max(0., K_Y + t * K_DY) };
        return K_A * K_A + K_B * K_B;
    };
    double result { std::numeric_limits<double>::infinity() };
    for (std::size_t index { 1uz }; index < breaks.size(); ++index) {
        const double K_MIDDLE { (breaks[index - 1uz] + breaks[index]) / 2 };
        const bool K_X_ACTIVE { K_X + K_MIDDLE * K_DX > 0 };
        const bool K_Y_ACTIVE { K_Y + K_MIDDLE * K_DY > 0 };
        const double K_A { K_X_ACTIVE ? K_DX : 0 }, K_B { K_Y_ACTIVE ? K_DY : 0 };
        const double K_DENOMINATOR { K_A * K_A + K_B * K_B };
        result = std::min({ result, K_DISTANCE(breaks[index - 1uz]), K_DISTANCE(breaks[index]) });
        if (K_DENOMINATOR > 0) {
            const double K_T { std::clamp(
                -(K_X * K_A + K_Y * K_B) / K_DENOMINATOR, breaks[index - 1uz], breaks[index]) };
            result = std::min(result, K_DISTANCE(K_T));
        }
    }
    return result;
}

}  // namespace

TEST_CASE("synthetic corner sweeps prove curved clearance without cutting the riser",
          "[collision-step]") {
    const float K_HEIGHT { std::nextafter(.3F, 0.F) };
    const auto K_SOURCE = collision_source(false, false, "NEG_X", 0, 0, K_HEIGHT);
    auto document = collision_step_document(K_SOURCE, K_HEIGHT, true);
    const auto K_ADMIT = [&](const nlohmann::json& candidate) {
        const auto K_TEXT = candidate.dump();
        return admit_collision_support(K_SOURCE, K_TEXT, collision_digest(K_TEXT));
    };
    const auto K_SUPPORT = K_ADMIT(document);
    REQUIRE(K_SUPPORT);

    struct CornerCircle {
        double m_radius {}, m_gap {};
    };

    const auto K_POINT = [&](CornerCircle circle, double angle) {
        const double K_ANGLE { angle * std::numbers::pi / 180 };
        return Vec3d { 4 - (circle.m_radius + circle.m_gap) * std::cos(K_ANGLE),
                      K_HEIGHT - circle.m_radius +
                          (circle.m_radius + circle.m_gap) * std::sin(K_ANGLE),
                      4.5 };
    };
    for (const double K_RADIUS : { .05, .25, .75 }) {
        for (const double K_ANGLE : { 75., 79., 83. }) {
            const CapsuleSweep K_SAFE { K_POINT({ K_RADIUS, .006 }, K_ANGLE),
                                       K_POINT({ K_RADIUS, .006 }, K_ANGLE + 2),
                                       K_RADIUS,
                                       2 * K_RADIUS + .5 };
            // Independent chord formula: its nearest point has radius R*cos(delta/2).
            CHECK((K_RADIUS + .006) * std::cos(std::numbers::pi / 180) > K_RADIUS);
            const auto K_FORWARD = probe_step_motion(**K_SUPPORT, K_SOURCE, K_SAFE);
            const auto K_REVERSE = probe_step_motion(
                **K_SUPPORT,
                K_SOURCE,
                { K_SAFE.m_end_feet, K_SAFE.m_start_feet, K_SAFE.m_radius, K_SAFE.m_height });
            REQUIRE(K_FORWARD);
            REQUIRE(K_REVERSE);
            CHECK(K_FORWARD->m_state == ClearanceState::CLEAR_DIAGNOSTIC);
            CHECK(K_REVERSE->m_state == ClearanceState::CLEAR_DIAGNOSTIC);
            CHECK(K_FORWARD->m_checked_cells > 0uz);
        }
    }
    const CapsuleSweep K_CUT { K_POINT({ .25, .001 }, 45), K_POINT({ .25, .001 }, 85), .25, 1.7 };
    CHECK(.251 * std::cos(20 * std::numbers::pi / 180) < .25);
    for (const auto K_FEET : { K_CUT.m_start_feet, K_CUT.m_end_feet }) {
        const auto K_STATIC = probe_step_motion(**K_SUPPORT, K_SOURCE, { K_FEET, K_FEET, .25, 1.7 });
        REQUIRE(K_STATIC);
        CHECK(K_STATIC->m_state == ClearanceState::CLEAR_DIAGNOSTIC);
    }
    const auto K_REJECTED = probe_step_motion(**K_SUPPORT, K_SOURCE, K_CUT);
    REQUIRE(K_REJECTED);
    CHECK(K_REJECTED->m_state == ClearanceState::UNSUPPORTED_FLOOR);
    const CapsuleSweep K_CORNER { { 3.8, .21, 3.5 }, { 3.81, .222, 3.5 }, .25, 1.7 };
    const auto K_CLEAR = probe_step_motion(**K_SUPPORT, K_SOURCE, K_CORNER);
    REQUIRE(K_CLEAR);
    CHECK(K_CLEAR->m_state == ClearanceState::CLEAR_DIAGNOSTIC);
    const auto K_TANGENT =
        probe_step_motion(**K_SUPPORT, K_SOURCE, { { 3.75, 0, 3.5 }, { 3.75, 0, 3.5 }, .25, 1.7 });
    REQUIRE(K_TANGENT);
    CHECK(K_TANGENT->m_state == ClearanceState::UNSUPPORTED_FLOOR);
    const auto K_EDGE =
        probe_step_motion(**K_SUPPORT, K_SOURCE, { { .1, 0, 3.5 }, { .1, 0, 3.5 }, .25, 1.7 });
    REQUIRE(K_EDGE);
    CHECK(K_EDGE->m_state == ClearanceState::UNKNOWN);
    for (const char K_STATE : { '?', '#' }) {
        document["grid"]["states"].get_ref<std::string&>()[4uz + 8uz * (0uz + 4uz * 3uz)] = K_STATE;
        const auto K_BLOCK_SUPPORT = K_ADMIT(document);
        REQUIRE(K_BLOCK_SUPPORT);
        const auto K_BLOCK = probe_step_motion(**K_BLOCK_SUPPORT, K_SOURCE, K_CORNER);
        REQUIRE(K_BLOCK);
        CHECK(K_BLOCK->m_state ==
              (K_STATE == '?' ? ClearanceState::UNKNOWN : ClearanceState::OCCUPIED));
    }
    document = collision_step_document(K_SOURCE, K_HEIGHT, true);
    document["grid"]["states"].get_ref<std::string&>()[5uz + 8uz * (1uz + 4uz * 3uz)] = '?';
    const auto K_TOUCH_SUPPORT = K_ADMIT(document);
    REQUIRE(K_TOUCH_SUPPORT);
    const auto K_TOUCH = probe_step_motion(
        **K_TOUCH_SUPPORT, K_SOURCE, { { 4.75, K_HEIGHT, 3.5 }, { 4.75, K_HEIGHT, 3.5 }, .25, 1.7 });
    REQUIRE(K_TOUCH);
    CHECK(K_TOUCH->m_state == ClearanceState::UNKNOWN);
    document = collision_step_document(K_SOURCE, K_HEIGHT);
    const auto K_LEGACY = K_ADMIT(document);
    REQUIRE(K_LEGACY);
    const auto K_LEGACY_PROBE = probe_step_motion(**K_LEGACY, K_SOURCE, K_CORNER);
    REQUIRE(K_LEGACY_PROBE);
    CHECK(K_LEGACY_PROBE->m_state == ClearanceState::UNSUPPORTED_FLOOR);
    auto corrected = K_SOURCE;
    ++corrected.m_revision.m_value;
    CHECK(!probe_step_motion(**K_SUPPORT, corrected, K_CORNER));
    std::stop_source cancelled;
    cancelled.request_stop();
    const auto K_CANCELLED =
        probe_step_motion(**K_SUPPORT, K_SOURCE, K_CORNER, cancelled.get_token());
    REQUIRE(!K_CANCELLED);
    CHECK(K_CANCELLED.error().m_code == ErrorCode::CANCELLED);
    CHECK(!probe_step_motion(
        **K_SUPPORT, K_SOURCE, { K_CORNER.m_start_feet, K_CORNER.m_end_feet, .01, 1.7 }));
}

TEST_CASE("synthetic corner sweeps clip cell evidence at the selected upper floor",
          "[collision-step]") {
    const float K_HEIGHT { std::nextafter(.3F, 0.F) };
    const auto K_SOURCE = collision_source(false, false, "NEG_X", 0, 0, K_HEIGHT);
    auto document = collision_step_document(K_SOURCE, K_HEIGHT, true);
    document["grid"]["cell_size"] = .125;
    document["grid"]["dimensions"] = { 64U, 32U, 64U };
    document["grid"]["states"] = std::string(64uz * 32uz * 64uz, '.');
    document["step"]["boundary_x"] = 32U;
    const auto K_ADMIT = [&](const nlohmann::json& candidate) {
        const auto K_TEXT = candidate.dump();
        return admit_collision_support(K_SOURCE, K_TEXT, collision_digest(K_TEXT));
    };
    const CapsuleSweep K_CORNER { { 3.8, .21, 3.5 }, { 3.81, .222, 3.5 }, .25, 1.7 };
    auto& states = document["grid"]["states"].get_ref<std::string&>();
    states[32uz + 64uz * (1uz + 32uz * 28uz)] = '?';
    const auto K_BELOW = K_ADMIT(document);
    REQUIRE(K_BELOW);
    const auto K_BELOW_PROBE = probe_step_motion(**K_BELOW, K_SOURCE, K_CORNER);
    REQUIRE(K_BELOW_PROBE);
    CHECK(K_BELOW_PROBE->m_state == ClearanceState::CLEAR_DIAGNOSTIC);
    states[32uz + 64uz * (2uz + 32uz * 28uz)] = '?';
    const auto K_ABOVE = K_ADMIT(document);
    REQUIRE(K_ABOVE);
    const auto K_ABOVE_PROBE = probe_step_motion(**K_ABOVE, K_SOURCE, K_CORNER);
    REQUIRE(K_ABOVE_PROBE);
    CHECK(K_ABOVE_PROBE->m_state == ClearanceState::UNKNOWN);
}

TEST_CASE("selected synthetic step admits both landings and an exact riser", "[collision-step]") {
    const float K_HEIGHT { std::nextafter(.3F, 0.F) };
    auto source =
        std::make_shared<const SceneState>(collision_source(false, false, "NEG_X", 0, 0, K_HEIGHT));
    auto document = collision_step_document(*source, K_HEIGHT);
    const auto K_TEXT = document.dump();
    const auto K_SUPPORT = admit_collision_support(*source, K_TEXT, collision_digest(K_TEXT));
    REQUIRE(K_SUPPORT);
    CHECK((*K_SUPPORT)->format_version() == 4U);
    CHECK((*K_SUPPORT)->floor_triangles().size() == 4uz);
    CHECK((*K_SUPPORT)->riser_triangles().size() == 2uz);
    CHECK((*K_SUPPORT)->floor_height({ 3.5, 0, 1.5 }) == 0);
    CHECK((*K_SUPPORT)->floor_height({ 4.5, 0, 1.5 }) == K_HEIGHT);
    const auto K_COOK = cook_synthetic_collision(source, *K_SUPPORT);
    REQUIRE(K_COOK);
    CHECK((*K_COOK)->parameters().m_format_version == 4U);
    CHECK((*K_COOK)->floor_triangle_count() == 4uz);
    CHECK((*K_COOK)->riser_triangle_count() == 2uz);
    CHECK((*K_COOK)->wall_triangle_count() == 0uz);
    CHECK((*K_COOK)->triangles().size() == 6uz);
    const auto K_SHAPE = build_jolt_static_collision(*K_COOK, *source);
    REQUIRE(K_SHAPE);
    const auto K_RAY = (*K_SHAPE)->ray_cast(*source, { 3, .15, 1 }, { 5, .15, 1 });
    REQUIRE(K_RAY);
    REQUIRE(K_RAY->has_value());
    CHECK(K_RAY->value_or(-1) == Catch::Approx(.5).margin(1e-5));
    const auto K_PROBE =
        probe_collision_support(**K_SUPPORT, *source, { { 3.4, 0, 3.5 }, { 3.4, 0, 3.5 }, .25, 1.7 });
    REQUIRE(K_PROBE);
    CHECK(K_PROBE->m_state == ClearanceState::UNSUPPORTED_FLOOR);
    const auto K_GAP =
        probe_collision_support(**K_SUPPORT, *source, { { 2, 0, 2 }, { 2, 0, 2 }, .25, 1.7 });
    REQUIRE(K_GAP);
    CHECK(K_GAP->m_state == ClearanceState::UNSUPPORTED_FLOOR);
    const auto K_CONTROLLER =
        SyntheticCapsuleController::create(*K_SHAPE, *source, { .25, 1.7 }, { 3.4, 0, 3.5 });
    CHECK(!K_CONTROLLER);
    CHECK(!SyntheticCapsuleController::inspect_step_motion(
        *K_SHAPE, *source, { .25, 1.7 }, { 3.4, 0, 3.5 }, { 3, 0, 0 }, 1uz));
    auto corrected = *source;
    ++corrected.m_revision.m_value;
    CHECK(!(*K_COOK)->validate_source(corrected));
}

TEST_CASE("synthetic step certificate checks both full-capsule routes", "[collision-step]") {
    const float K_HEIGHT { std::nextafter(.3F, 0.F) };
    const auto K_SOURCE = collision_source(false, false, "NEG_X", 0, 0, K_HEIGHT);
    auto document = collision_step_document(K_SOURCE, K_HEIGHT, true);
    const auto K_ADMIT = [&](const nlohmann::json& candidate) {
        const auto K_TEXT = candidate.dump();
        return admit_collision_support(K_SOURCE, K_TEXT, collision_digest(K_TEXT));
    };
    const auto K_SUPPORT = K_ADMIT(document);
    REQUIRE(K_SUPPORT);
    const CapsuleSweep K_ASCENT { { 3.4, 0, 3.5 }, { 5.4, K_HEIGHT, 3.5 }, .25, 1.7 };
    const CapsuleSweep K_DESCENT {
        K_ASCENT.m_end_feet, K_ASCENT.m_start_feet, K_ASCENT.m_radius, K_ASCENT.m_height };
    const auto K_UP = probe_step_transition(**K_SUPPORT, K_SOURCE, K_ASCENT);
    const auto K_DOWN = probe_step_transition(**K_SUPPORT, K_SOURCE, K_DESCENT);
    REQUIRE(K_UP);
    REQUIRE(K_DOWN);
    CHECK(K_UP->m_state == ClearanceState::CLEAR_DIAGNOSTIC);
    CHECK(K_DOWN->m_state == ClearanceState::CLEAR_DIAGNOSTIC);
    CHECK(K_UP->m_checked_cells > 0uz);
    const auto K_COOK =
        cook_synthetic_collision(std::make_shared<const SceneState>(K_SOURCE), *K_SUPPORT);
    REQUIRE(K_COOK);
    const auto K_SHAPE = build_jolt_static_collision(*K_COOK, K_SOURCE);
    REQUIRE(K_SHAPE);
    const auto K_CONTROLLER =
        SyntheticCapsuleController::create(*K_SHAPE, K_SOURCE, { .25, 1.7 }, K_ASCENT.m_start_feet);
    CHECK(!K_CONTROLLER);
    const auto K_UPPER_LANDING = probe_collision_support(
        **K_SUPPORT, K_SOURCE, { { 5.4, K_HEIGHT, 3.5 }, { 5.5, K_HEIGHT, 3.5 }, .25, 1.7 });
    REQUIRE(K_UPPER_LANDING);
    CHECK(K_UPPER_LANDING->m_state == ClearanceState::CLEAR_DIAGNOSTIC);
    const auto K_DIRECT = probe_collision_support(**K_SUPPORT, K_SOURCE, K_ASCENT);
    REQUIRE(K_DIRECT);
    CHECK(K_DIRECT->m_state == ClearanceState::UNSUPPORTED_FLOOR);
    auto corrected = K_SOURCE;
    ++corrected.m_revision.m_value;
    const auto K_STALE = probe_step_transition(**K_SUPPORT, corrected, K_ASCENT);
    REQUIRE(!K_STALE);
    CHECK(K_STALE.error().m_code == ErrorCode::WRONG_BASE);

    SECTION("legacy V4 evidence does not grant clearance") {
        document["step"].erase("clearance_profile");
        const auto K_LEGACY = K_ADMIT(document);
        REQUIRE(K_LEGACY);
        const auto K_BLOCKED = probe_step_transition(**K_LEGACY, K_SOURCE, K_ASCENT);
        REQUIRE(K_BLOCKED);
        CHECK(K_BLOCKED->m_state == ClearanceState::UNSUPPORTED_FLOOR);
        document["step"]["clearance_profile"] = "UNKNOWN_PROFILE";
        CHECK(!K_ADMIT(document));
        document["step"]["clearance_profile"] = true;
        CHECK(!K_ADMIT(document));
    }

    SECTION("unknown and occupied upper space block the transition") {
        for (const char K_STATE : { '?', '#' }) {
            auto& cells = document["grid"]["states"].get_ref<std::string&>();
            cells[4uz + 8uz * (1uz + 4uz * 3uz)] = K_STATE;
            const auto K_BLOCKED_SUPPORT = K_ADMIT(document);
            REQUIRE(K_BLOCKED_SUPPORT);
            const auto K_BLOCKED = probe_step_transition(**K_BLOCKED_SUPPORT, K_SOURCE, K_ASCENT);
            REQUIRE(K_BLOCKED);
            CHECK(K_BLOCKED->m_state ==
                  (K_STATE == '?' ? ClearanceState::UNKNOWN : ClearanceState::OCCUPIED));
        }
    }
    SECTION("missing support or out-of-grid capsule blocks the path") {
        document["floors"].erase(1uz);
        CHECK(!K_ADMIT(document));
        const CapsuleSweep K_EDGE { { 3.4, 0, .1 }, { 5.4, K_HEIGHT, .1 }, .25, 1.7 };
        const auto K_UNKNOWN = probe_step_transition(**K_SUPPORT, K_SOURCE, K_EDGE);
        REQUIRE(K_UNKNOWN);
        CHECK(K_UNKNOWN->m_state == ClearanceState::UNKNOWN);
    }
    SECTION("a blocked corner on the diagonal approach blocks the whole route") {
        auto& cells = document["grid"]["states"].get_ref<std::string&>();
        cells[3uz + 8uz * (0uz + 4uz * 4uz)] = '#';
        const auto K_CORNER_SUPPORT = K_ADMIT(document);
        REQUIRE(K_CORNER_SUPPORT);
        CapsuleSweep corner { K_ASCENT };
        corner.m_end_feet.m_z = 4.5;
        const auto K_CORNER = probe_step_transition(**K_CORNER_SUPPORT, K_SOURCE, corner);
        REQUIRE(K_CORNER);
        CHECK(K_CORNER->m_state == ClearanceState::OCCUPIED);
    }
    SECTION("a cell touched at the capsule boundary is checked") {
        document["grid"]["states"].get_ref<std::string&>()[5uz + 8uz * (0uz + 4uz * 3uz)] = '?';
        const auto K_BOUNDARY_SUPPORT = K_ADMIT(document);
        REQUIRE(K_BOUNDARY_SUPPORT);
        const auto K_BOUNDARY =
            probe_collision_support(**K_BOUNDARY_SUPPORT,
                                    K_SOURCE,
                                    { { 4.75, K_HEIGHT, 3.5 }, { 4.75, K_HEIGHT, 3.5 }, .25, 1.7 });
        REQUIRE(K_BOUNDARY);
        CHECK(K_BOUNDARY->m_state == ClearanceState::UNKNOWN);
    }
    SECTION("short crossing and airborne endpoints do not get a certificate") {
        const CapsuleSweep K_SHORT { { 3.8, 0, 2 }, { 4.2, K_HEIGHT, 2 }, .25, 1.7 };
        const auto K_SHORT_RESULT = probe_step_transition(**K_SUPPORT, K_SOURCE, K_SHORT);
        REQUIRE(K_SHORT_RESULT);
        CHECK(K_SHORT_RESULT->m_state == ClearanceState::UNSUPPORTED_FLOOR);
        const CapsuleSweep K_AIR { { 2, .1, 2 }, { 6, K_HEIGHT, 2 }, .25, 1.7 };
        const auto K_AIR_RESULT = probe_step_transition(**K_SUPPORT, K_SOURCE, K_AIR);
        REQUIRE(K_AIR_RESULT);
        CHECK(K_AIR_RESULT->m_state == ClearanceState::UNSUPPORTED_FLOOR);
    }
}

TEST_CASE("certified step cursors bound slow ascent and descent without corner shortcuts",
          "[collision-step]") {
    const float K_HEIGHT { std::nextafter(.3F, 0.F) };
    const auto K_SOURCE = collision_source(false, false, "NEG_X", 0, 0, K_HEIGHT);
    const auto K_TEXT = collision_step_document(K_SOURCE, K_HEIGHT, true).dump();
    const auto K_SUPPORT = admit_collision_support(K_SOURCE, K_TEXT, collision_digest(K_TEXT));
    REQUIRE(K_SUPPORT);
    const auto K_COOK =
        cook_synthetic_collision(std::make_shared<const SceneState>(K_SOURCE), *K_SUPPORT);
    REQUIRE(K_COOK);
    for (const double K_SPEED : { .3, 3., 10. }) {
        for (const bool K_DESCEND : { false, true }) {
            const Vec3d K_LOWER { 3.4, 0, 3.5 }, K_UPPER { 5.4, K_HEIGHT, 3.5 };
            const CapsuleSweep K_ROUTE {
                K_DESCEND ? K_UPPER : K_LOWER, K_DESCEND ? K_LOWER : K_UPPER, .25, 1.7 };
            auto cursor = SyntheticStepTransition::create(*K_COOK, K_SOURCE, K_ROUTE);
            REQUIRE(cursor);
            std::size_t ticks { 0uz }, airborne { 0uz };
            while (!cursor->state().m_finished && ticks < 1200uz) {
                const auto K_BEFORE = cursor->state();
                const auto K_TICK = cursor->advance_tick(K_SOURCE, K_SPEED);
                REQUIRE(K_TICK);
                CHECK(K_TICK->m_start_feet == K_BEFORE.m_feet);
                CHECK(K_TICK->m_end_feet == cursor->state().m_feet);
                CHECK(std::sqrt(dot(K_TICK->m_end_feet - K_TICK->m_start_feet,
                                    K_TICK->m_end_feet - K_TICK->m_start_feet)) <=
                      K_SPEED / 120 + 1e-12);
                CHECK(minimum_corner_distance_squared(*K_TICK, K_HEIGHT) >= .25 * .25);
                CHECK(std::min(K_TICK->m_start_feet.m_y, K_TICK->m_end_feet.m_y) >= 0);
                const auto K_DIRECT = (*K_COOK)->probe_motion(K_SOURCE, *K_TICK);
                REQUIRE(K_DIRECT);
                airborne += K_DIRECT->m_state != ClearanceState::CLEAR_DIAGNOSTIC;
                ++ticks;
            }
            REQUIRE(cursor->state().m_finished);
            CHECK(cursor->state().m_feet == K_ROUTE.m_end_feet);
            CHECK(airborne > 0uz);
            const auto K_LANDING =
                (*K_COOK)->probe(K_SOURCE, { K_ROUTE.m_end_feet, K_ROUTE.m_end_feet, .25, 1.7 });
            REQUIRE(K_LANDING);
            CHECK(K_LANDING->m_state == ClearanceState::CLEAR_DIAGNOSTIC);
            const auto K_HELD = cursor->advance_tick(K_SOURCE, K_SPEED);
            REQUIRE(K_HELD);
            CHECK(K_HELD->m_start_feet == K_HELD->m_end_feet);
        }
    }
}

TEST_CASE("certified step cursors retain phase on pause failure and reversal", "[collision-step]") {
    const float K_HEIGHT { std::nextafter(.3F, 0.F) };
    const auto K_SOURCE = collision_source(false, false, "NEG_X", 0, 0, K_HEIGHT);
    const auto K_TEXT = collision_step_document(K_SOURCE, K_HEIGHT, true).dump();
    const auto K_SUPPORT = admit_collision_support(K_SOURCE, K_TEXT, collision_digest(K_TEXT));
    REQUIRE(K_SUPPORT);
    const auto K_COOK =
        cook_synthetic_collision(std::make_shared<const SceneState>(K_SOURCE), *K_SUPPORT);
    REQUIRE(K_COOK);
    const CapsuleSweep K_ROUTE { { 3.4, 0, 3.5 }, { 5.4, K_HEIGHT, 3.5 }, .25, 1.7 };
    auto cursor = SyntheticStepTransition::create(*K_COOK, K_SOURCE, K_ROUTE);
    REQUIRE(cursor);
    for (std::size_t tick { 0uz }; tick < 20uz; ++tick) {
        REQUIRE(cursor->advance_tick(K_SOURCE, 3));
    }
    REQUIRE(cursor->state().m_feet.m_y > 0);
    REQUIRE(cursor->state().m_feet.m_y < K_HEIGHT);
    const auto K_BEFORE = cursor->state();
    const auto K_PAUSE = cursor->advance_tick(K_SOURCE, 0);
    REQUIRE(K_PAUSE);
    CHECK(K_PAUSE->m_start_feet == K_PAUSE->m_end_feet);
    CHECK(cursor->state().m_segment == K_BEFORE.m_segment);
    std::stop_source stop;
    stop.request_stop();
    const auto K_CANCEL = cursor->advance_tick(K_SOURCE, 3, stop.get_token());
    REQUIRE(!K_CANCEL);
    CHECK(K_CANCEL.error().m_code == ErrorCode::CANCELLED);
    auto corrected = K_SOURCE;
    ++corrected.m_revision.m_value;
    const auto K_STALE = cursor->advance_tick(corrected, 0);
    REQUIRE(!K_STALE);
    CHECK(K_STALE.error().m_code == ErrorCode::WRONG_BASE);
    for (const double K_BAD : { 10.01,
                               -10.01,
                               std::numeric_limits<double>::quiet_NaN(),
                               std::numeric_limits<double>::infinity() }) {
        CHECK(!cursor->advance_tick(K_SOURCE, K_BAD));
    }
    CHECK(cursor->state().m_feet == K_BEFORE.m_feet);
    CHECK(cursor->state().m_segment == K_BEFORE.m_segment);
    auto replay = *cursor;
    CHECK(cursor->advance_tick(K_SOURCE, 3)->m_end_feet ==
          replay.advance_tick(K_SOURCE, 3)->m_end_feet);
    for (std::size_t tick { 0uz }; tick < 100uz; ++tick) {
        const auto K_BACK = cursor->advance_tick(K_SOURCE, -3);
        REQUIRE(K_BACK);
        CHECK(std::sqrt(dot(K_BACK->m_end_feet - K_BACK->m_start_feet,
                            K_BACK->m_end_feet - K_BACK->m_start_feet)) <= 3. / 120 + 1e-12);
        CHECK(minimum_corner_distance_squared(*K_BACK, K_HEIGHT) >= .25 * .25);
    }
    CHECK(cursor->state().m_feet == K_ROUTE.m_start_feet);
    CHECK(!cursor->state().m_finished);
}

TEST_CASE("step path ownership requires a complete current certificate", "[collision-step]") {
    const float K_HEIGHT { std::nextafter(.3F, 0.F) };
    const auto K_SOURCE = collision_source(false, false, "NEG_X", 0, 0, K_HEIGHT);
    auto document = collision_step_document(K_SOURCE, K_HEIGHT, true);
    const auto K_ADMIT = [&](const nlohmann::json& candidate) {
        const auto K_TEXT = candidate.dump();
        return admit_collision_support(K_SOURCE, K_TEXT, collision_digest(K_TEXT));
    };
    const auto K_SUPPORT = K_ADMIT(document);
    REQUIRE(K_SUPPORT);
    const auto K_COOK =
        cook_synthetic_collision(std::make_shared<const SceneState>(K_SOURCE), *K_SUPPORT);
    REQUIRE(K_COOK);
    const CapsuleSweep K_ROUTE { { 3.4, 0, 3.5 }, { 5.4, K_HEIGHT, 3.5 }, .25, 1.7 };
    const auto K_CERTIFICATE = (*K_COOK)->certify_transition(K_SOURCE, K_ROUTE);
    REQUIRE(K_CERTIFICATE);
    REQUIRE(K_CERTIFICATE->m_path);
    const auto K_PROBE = (*K_COOK)->probe_transition(K_SOURCE, K_ROUTE);
    REQUIRE(K_PROBE);
    CHECK(K_CERTIFICATE->m_probe.m_state == K_PROBE->m_state);
    CHECK(K_CERTIFICATE->m_probe.m_checked_cells == K_PROBE->m_checked_cells);
    if (const auto K_PATH = K_CERTIFICATE->m_path; K_PATH.has_value()) {
        CHECK(K_PATH->front() == K_ROUTE.m_start_feet);
        CHECK(K_PATH->back() == K_ROUTE.m_end_feet);
    }
    CHECK(!SyntheticStepTransition::create({}, K_SOURCE, K_ROUTE));
    for (const auto K_DIMENSIONS : { SyntheticCapsuleSettings { .049, 1.7 },
                                    SyntheticCapsuleSettings { 1.01, 3 },
                                    SyntheticCapsuleSettings { .25, .5 },
                                    SyntheticCapsuleSettings { .25, 3.01 } }) {
        auto invalid = K_ROUTE;
        invalid.m_radius = K_DIMENSIONS.m_radius;
        invalid.m_height = K_DIMENSIONS.m_height;
        const auto K_BAD = SyntheticStepTransition::create(*K_COOK, K_SOURCE, invalid);
        REQUIRE(!K_BAD);
        CHECK(K_BAD.error().m_code == ErrorCode::INVALID_ARGUMENT);
    }
    std::stop_source stop;
    stop.request_stop();
    const auto K_CANCEL =
        SyntheticStepTransition::create(*K_COOK, K_SOURCE, K_ROUTE, stop.get_token());
    REQUIRE(!K_CANCEL);
    CHECK(K_CANCEL.error().m_code == ErrorCode::CANCELLED);
    for (const char K_STATE : { '?', '#' }) {
        document["grid"]["states"].get_ref<std::string&>()[4uz + 8uz * (1uz + 4uz * 3uz)] = K_STATE;
        const auto K_BLOCKED_SUPPORT = K_ADMIT(document);
        REQUIRE(K_BLOCKED_SUPPORT);
        const auto K_BLOCKED_CERTIFICATE =
            certify_step_transition(**K_BLOCKED_SUPPORT, K_SOURCE, K_ROUTE);
        REQUIRE(K_BLOCKED_CERTIFICATE);
        CHECK(!K_BLOCKED_CERTIFICATE->m_path);
        CHECK(K_BLOCKED_CERTIFICATE->m_probe.m_state ==
              (K_STATE == '?' ? ClearanceState::UNKNOWN : ClearanceState::OCCUPIED));
        const auto K_BLOCKED_COOK = cook_synthetic_collision(
            std::make_shared<const SceneState>(K_SOURCE), *K_BLOCKED_SUPPORT);
        REQUIRE(K_BLOCKED_COOK);
        const auto K_BLOCKED = SyntheticStepTransition::create(*K_BLOCKED_COOK, K_SOURCE, K_ROUTE);
        REQUIRE(!K_BLOCKED);
        CHECK(K_BLOCKED.error().m_code == ErrorCode::WOULD_BLOCK);
    }
    document["step"].erase("clearance_profile");
    const auto K_LEGACY = K_ADMIT(document);
    REQUIRE(K_LEGACY);
    const auto K_LEGACY_CERTIFICATE = certify_step_transition(**K_LEGACY, K_SOURCE, K_ROUTE);
    REQUIRE(K_LEGACY_CERTIFICATE);
    CHECK(!K_LEGACY_CERTIFICATE->m_path);
    const CapsuleSweep K_SHORT { { 3.8, 0, 3.5 }, { 4.2, K_HEIGHT, 3.5 }, .25, 1.7 };
    const auto K_SHORT_RESULT = SyntheticStepTransition::create(*K_COOK, K_SOURCE, K_SHORT);
    REQUIRE(!K_SHORT_RESULT);
    CHECK(K_SHORT_RESULT.error().m_code == ErrorCode::WOULD_BLOCK);
    auto corrected = K_SOURCE;
    ++corrected.m_epoch.m_value;
    const auto K_STALE = SyntheticStepTransition::create(*K_COOK, corrected, K_ROUTE);
    REQUIRE(!K_STALE);
    CHECK(K_STALE.error().m_code == ErrorCode::WRONG_EPOCH);
    CHECK((*K_COOK)->validate_source(K_SOURCE));
}

TEST_CASE("synthetic step rejects over-height and source-gap mutations", "[collision-step]") {
    const float K_HEIGHT { std::nextafter(.3F, 0.F) };
    const auto K_SOURCE = collision_source(false, false, "NEG_X", 0, 0, K_HEIGHT);
    auto document = collision_step_document(K_SOURCE, K_HEIGHT);
    const auto K_ADMIT = [&](const nlohmann::json& candidate) {
        const auto K_TEXT = candidate.dump();
        return admit_collision_support(K_SOURCE, K_TEXT, collision_digest(K_TEXT));
    };
    REQUIRE(K_ADMIT(document));
    document["step"]["height"] = .300001;
    CHECK(!K_ADMIT(document));
    document = collision_step_document(K_SOURCE, K_HEIGHT);
    document["step"]["riser"]["triangles"] = { 0U };
    CHECK(!K_ADMIT(document));
    document = collision_step_document(K_SOURCE, K_HEIGHT);
    document["step"]["boundary_x"] = 5U;
    CHECK(!K_ADMIT(document));
    document = collision_step_document(K_SOURCE, K_HEIGHT);
    document["floors"].erase(1uz);
    CHECK(!K_ADMIT(document));
    document = collision_step_document(K_SOURCE, K_HEIGHT);
    document["step"]["riser"]["chunk_sha256"] = std::string(64uz, '0');
    CHECK(!K_ADMIT(document));
    const float K_ABOVE { std::nextafter(.3F, 1.F) };
    const auto K_HIGH_SOURCE = collision_source(false, false, "NEG_X", 0, 0, K_ABOVE);
    const auto K_HIGH_DOCUMENT = collision_step_document(K_HIGH_SOURCE, K_ABOVE);
    const auto K_HIGH_TEXT = K_HIGH_DOCUMENT.dump();
    CHECK(!admit_collision_support(K_HIGH_SOURCE, K_HIGH_TEXT, collision_digest(K_HIGH_TEXT)));
}

TEST_CASE("bounded Jolt step proposals retain rejection and respect tick travel",
          "[collision-step]") {
    const float K_HEIGHT { std::nextafter(.3F, 0.F) };
    const auto K_SOURCE =
        std::make_shared<const SceneState>(collision_source(false, false, "NEG_X", 0, 0, K_HEIGHT));
    const auto K_TEXT = collision_step_document(*K_SOURCE, K_HEIGHT, true).dump();
    const auto K_HASH = scene_content_hash(*K_SOURCE);
    const auto K_SUPPORT = admit_collision_support(*K_SOURCE, K_TEXT, collision_digest(K_TEXT));
    REQUIRE(K_SUPPORT);
    const auto K_COOK = cook_synthetic_collision(K_SOURCE, *K_SUPPORT);
    REQUIRE(K_COOK);
    const auto K_SHAPE = build_jolt_static_collision(*K_COOK, *K_SOURCE);
    REQUIRE(K_SHAPE);
    for (const double K_SPEED : { .3, 3., 10. }) {
        const auto K_TICKS = static_cast<std::size_t>(std::ceil(1.5 / K_SPEED * 120));
        const auto K_PROPOSALS = SyntheticCapsuleController::inspect_step_motion(
            *K_SHAPE, *K_SOURCE, { .25, 1.7 }, { 3.4, 0, 3.5 }, { K_SPEED, 0, 0 }, K_TICKS);
        REQUIRE(K_PROPOSALS);
        REQUIRE(K_PROPOSALS->size() == K_TICKS);
        bool rejected_corner {};
        std::size_t proved_corners {};
        Vec3d previous { 3.4, 0, 3.5 };
        const auto K_QUERY = [&](Vec3d point) {
            const double K_FLOOR { (*K_SUPPORT)->floor_height(point) };
            if (std::abs(point.m_y - K_FLOOR) <= .002) {
                point.m_y = K_FLOOR;
            }
            return point;
        };
        for (const auto& proposal : *K_PROPOSALS) {
            CHECK(proposal.m_horizontal_distance <= K_SPEED / 120 + 1e-5);
            const CapsuleSweep K_SWEEP { K_QUERY(previous), K_QUERY(proposal.m_raw_feet), .25, 1.7 };
            const auto K_MOTION = probe_step_motion(**K_SUPPORT, *K_SOURCE, K_SWEEP);
            REQUIRE(K_MOTION);
            if (K_MOTION->m_state == ClearanceState::CLEAR_DIAGNOSTIC) {
                // Piecewise-quadratic minimum is independent of the interval proof.
                CHECK(minimum_corner_distance_squared(K_SWEEP, K_HEIGHT) >= .0625 - 1e-12);
                CHECK(std::min(K_SWEEP.m_start_feet.m_y, K_SWEEP.m_end_feet.m_y) >= 0);
                proved_corners +=
                    proposal.m_straight_probe.m_state == ClearanceState::UNSUPPORTED_FLOOR ? 1uz
                                                                                           : 0uz;
            }
            previous = proposal.m_raw_feet;
            if (proposal.m_raw_feet.m_y > .01 && proposal.m_raw_feet.m_y < K_HEIGHT - .01) {
                rejected_corner = true;
                CHECK(proposal.m_straight_probe.m_state == ClearanceState::UNSUPPORTED_FLOOR);
                CHECK(proposal.m_transition_probe.m_state == ClearanceState::UNSUPPORTED_FLOOR);
            }
        }
        if (K_SPEED == .3) {
            // With no minimum-distance jump Jolt cannot climb this riser at
            // the slow input. Retain that rejection rather than invent travel.
            CHECK(!rejected_corner);
            CHECK(K_PROPOSALS->back().m_raw_feet.m_x < 3.75);
            CHECK(K_PROPOSALS->back().m_raw_feet.m_y == Catch::Approx(0).margin(.002));
        } else {
            CHECK(rejected_corner);
            if (K_SPEED == 3) {
                CHECK(proved_corners > 0uz);
            }
            CHECK(K_PROPOSALS->back().m_raw_feet.m_x > 4.5);
            CHECK(K_PROPOSALS->back().m_raw_feet.m_y == Catch::Approx(K_HEIGHT).margin(.002));
        }
    }
    const auto K_DOWN = SyntheticCapsuleController::inspect_step_motion(
        *K_SHAPE, *K_SOURCE, { .25, 1.7 }, { 5.4, K_HEIGHT, 3.5 }, { -3, 0, 0 }, 90uz);
    REQUIRE(K_DOWN);
    CHECK(K_DOWN->back().m_raw_feet.m_x < 3.5);
    CHECK(K_DOWN->back().m_raw_feet.m_y == Catch::Approx(0).margin(.002));
    CHECK(scene_content_hash(*K_SOURCE) == K_HASH);
    CHECK(!SyntheticCapsuleController::create(*K_SHAPE, *K_SOURCE, { .25, 1.7 }, { 3.4, 0, 3.5 }));

    auto corrected = *K_SOURCE;
    ++corrected.m_revision.m_value;
    const auto K_STALE = SyntheticCapsuleController::inspect_step_motion(
        *K_SHAPE, corrected, { .25, 1.7 }, { 3.4, 0, 3.5 }, { 3, 0, 0 }, 1uz);
    REQUIRE(!K_STALE);
    CHECK(K_STALE.error().m_code == ErrorCode::WRONG_BASE);
    std::stop_source cancelled;
    cancelled.request_stop();
    const auto K_CANCELLED = SyntheticCapsuleController::inspect_step_motion(
        *K_SHAPE, *K_SOURCE, { .25, 1.7 }, { 3.4, 0, 3.5 }, { 3, 0, 0 }, 1uz, cancelled.get_token());
    REQUIRE(!K_CANCELLED);
    CHECK(K_CANCELLED.error().m_code == ErrorCode::CANCELLED);
    for (const auto K_TICKS : { 0uz, 601uz }) {
        CHECK(!SyntheticCapsuleController::inspect_step_motion(
            *K_SHAPE, *K_SOURCE, { .25, 1.7 }, { 3.4, 0, 3.5 }, { 3, 0, 0 }, K_TICKS));
    }
    CHECK(!SyntheticCapsuleController::inspect_step_motion(
        *K_SHAPE, *K_SOURCE, { .25, 1.7 }, { 3.4, 0, 3.5 }, { 3, 1, 0 }, 1uz));
}
