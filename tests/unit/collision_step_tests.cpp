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
        probe_collision_support(**K_SUPPORT, *source, { { 2, 0, 2 }, { 2, 0, 2 }, .25, 1.7 });
    REQUIRE(K_PROBE);
    CHECK(K_PROBE->m_state == ClearanceState::UNSUPPORTED_FLOOR);
    const auto K_CONTROLLER =
        SyntheticCapsuleController::create(*K_SHAPE, *source, { .25, 1.7 }, { 2, 0, 2 });
    CHECK(!K_CONTROLLER);
    auto corrected = *source;
    ++corrected.m_revision.m_value;
    CHECK(!(*K_COOK)->validate_source(corrected));
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
