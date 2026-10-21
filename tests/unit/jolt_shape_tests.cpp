#include "../fixtures/collision_support_fixture.hpp"
#include "world_explorer/collision/static_shape.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <stop_token>

using namespace world_explorer;
using namespace world_explorer::testing;

TEST_CASE("Jolt static shape ray casts synthetic selected floors", "[jolt-shape]") {
    auto source = std::make_shared<const SceneState>(collision_source());
    const auto K_DOCUMENT = collision_document(*source).dump();
    const auto K_SUPPORT =
        admit_collision_support(*source, K_DOCUMENT, collision_digest(K_DOCUMENT));
    REQUIRE(K_SUPPORT);
    const auto K_COOK = cook_synthetic_collision(source, *K_SUPPORT);
    REQUIRE(K_COOK);
    const auto K_SHAPE = build_jolt_static_collision(*K_COOK, *source);
    REQUIRE(K_SHAPE);
    CHECK((*K_SHAPE)->triangle_count() == 2uz);
    CHECK((*K_SHAPE)->canonical_digest() == (*K_COOK)->digest());

    const Vec3d K_START { 2, 2, 2 };
    const Vec3d K_DOWN_END { 2, -1, 2 };
    const auto K_DOWN = (*K_SHAPE)->ray_cast(*source, K_START, K_DOWN_END);
    REQUIRE(K_DOWN);
    REQUIRE(K_DOWN->has_value());
    CHECK(K_DOWN->value_or(-1.0) == Catch::Approx(2.0 / 3.0));
    const Vec3d K_ABOVE_END { 4, 2, 2 };
    const auto K_ABOVE = (*K_SHAPE)->ray_cast(*source, K_START, K_ABOVE_END);
    REQUIRE(K_ABOVE);
    CHECK(!K_ABOVE->has_value());
    const auto K_ZERO = (*K_SHAPE)->ray_cast(*source, K_START, K_START);
    REQUIRE(!K_ZERO);
    CHECK(K_ZERO.error().m_code == ErrorCode::INVALID_ARGUMENT);
    const Vec3d K_PRECISION_START { 999999.01, 2, 2 };
    const Vec3d K_PRECISION_END { 999999.02, 2, 2 };
    const auto K_PRECISION = (*K_SHAPE)->ray_cast(*source, K_PRECISION_START, K_PRECISION_END);
    REQUIRE(!K_PRECISION);
    CHECK(K_PRECISION.error().m_code == ErrorCode::INVALID_DATA);
    CHECK(walking_block_reason(*source) == WalkingBlockReason::SYNTHETIC_DIAGNOSTIC);
}

TEST_CASE("Jolt static owner refuses corrections and preserves prior shape", "[jolt-shape]") {
    auto source = std::make_shared<const SceneState>(collision_source());
    const auto K_DOCUMENT = collision_document(*source).dump();
    const auto K_SUPPORT =
        admit_collision_support(*source, K_DOCUMENT, collision_digest(K_DOCUMENT));
    REQUIRE(K_SUPPORT);
    const auto K_COOK = cook_synthetic_collision(source, *K_SUPPORT);
    REQUIRE(K_COOK);
    const auto K_SHAPE = build_jolt_static_collision(*K_COOK, *source);
    REQUIRE(K_SHAPE);
    const auto K_DIGEST = (*K_SHAPE)->canonical_digest();
    auto corrected = *source;
    ++corrected.m_revision.m_value;
    const Vec3d K_START { 2, 2, 2 };
    const Vec3d K_DOWN_END { 2, -1, 2 };
    const auto K_STALE = (*K_SHAPE)->ray_cast(corrected, K_START, K_DOWN_END);
    REQUIRE(!K_STALE);
    CHECK(K_STALE.error().m_code == ErrorCode::WRONG_BASE);
    const auto K_REBUILD = build_jolt_static_collision(*K_COOK, corrected);
    REQUIRE(!K_REBUILD);
    CHECK(K_REBUILD.error().m_code == ErrorCode::WRONG_BASE);
    std::stop_source cancelled;
    cancelled.request_stop();
    const auto K_STOPPED = build_jolt_static_collision(*K_COOK, *source, cancelled.get_token());
    REQUIRE(!K_STOPPED);
    CHECK(K_STOPPED.error().m_code == ErrorCode::CANCELLED);
    const auto K_PRIOR = (*K_SHAPE)->ray_cast(*source, K_START, K_DOWN_END);
    REQUIRE(K_PRIOR);
    CHECK(K_PRIOR->has_value());
    CHECK((*K_SHAPE)->canonical_digest() == K_DIGEST);
}
