#include "../fixtures/collision_support_fixture.hpp"
#include "world_explorer/collision/kinematic_capsule.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace world_explorer;
using namespace world_explorer::testing;

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
        for (const auto& proposal : *K_PROPOSALS) {
            CHECK(proposal.m_horizontal_distance <= K_SPEED / 120 + 1e-5);
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
