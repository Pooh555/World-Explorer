#include "../fixtures/collision_support_fixture.hpp"
#include "world_explorer/collision/static_shape.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <stop_token>

using namespace world_explorer;
using namespace world_explorer::testing;

namespace {

Result<std::shared_ptr<const CollisionSupport>> admit_wall(const SceneState& source,
                                                           const nlohmann::json& document) {
    const auto K_TEXT = document.dump();
    return admit_collision_support(source, K_TEXT, collision_digest(K_TEXT));
}

constexpr CapsuleSweep K_WALL_SWEEP { { 2.5, 0, 1.5 }, { 4.5, 0, 1.5 }, .2, .8 };

}  // namespace

TEST_CASE("V2 selects a complete occupied wall face and preserves the V1 floor stream",
          "[collision-wall]") {
    auto source = std::make_shared<const SceneState>(collision_source(true));
    const auto K_DOCUMENT = collision_wall_document(*source);
    const auto K_SUPPORT = admit_wall(*source, K_DOCUMENT);
    REQUIRE(K_SUPPORT);
    CHECK((*K_SUPPORT)->format_version() == 2U);
    CHECK((*K_SUPPORT)->floor_triangles().size() == 2uz);
    CHECK((*K_SUPPORT)->wall_triangles().size() == 2uz);
    const auto K_COOK = cook_synthetic_collision(source, *K_SUPPORT);
    REQUIRE(K_COOK);
    CHECK((*K_COOK)->parameters().m_format_version == 2U);
    CHECK((*K_COOK)->floor_triangle_count() == 2uz);
    CHECK((*K_COOK)->wall_triangle_count() == 2uz);
    const auto K_MAGIC = std::string_view { "WE_COLLISION_TRIANGLES_V2" };
    CHECK(std::equal(K_MAGIC.begin(),
                     K_MAGIC.end(),
                     reinterpret_cast<const char*>((*K_COOK)->canonical_bytes().data())));
    const auto K_SHAPE = build_jolt_static_collision(*K_COOK, *source);
    REQUIRE(K_SHAPE);
    CHECK((*K_SHAPE)->triangle_count() == 4uz);
    const Vec3d K_START { 2, .5, 1.5 };
    const Vec3d K_END { 4, .5, 1.5 };
    const auto K_HIT = (*K_SHAPE)->ray_cast(*source, K_START, K_END);
    REQUIRE(K_HIT);
    REQUIRE(K_HIT->has_value());
    CHECK(K_HIT->value_or(-1.0) == Catch::Approx(.5));
    const auto K_BLOCKED = (*K_COOK)->probe(*source, K_WALL_SWEEP);
    REQUIRE(K_BLOCKED);
    CHECK(K_BLOCKED->m_state == ClearanceState::OCCUPIED);
    CHECK(walking_block_reason(*source) == WalkingBlockReason::SYNTHETIC_DIAGNOSTIC);

    const auto K_V1 = collision_source();
    const auto K_V1_SUPPORT = admit_wall(K_V1, collision_document(K_V1));
    REQUIRE(K_V1_SUPPORT);
    CHECK((*K_V1_SUPPORT)->format_version() == 1U);
    CHECK((*K_V1_SUPPORT)->wall_triangles().empty());
    const auto K_V1_COOK =
        cook_synthetic_collision(std::make_shared<const SceneState>(K_V1), *K_V1_SUPPORT);
    REQUIRE(K_V1_COOK);
    CHECK((*K_V1_COOK)->parameters().m_format_version == 1U);
    CHECK((*K_V1_COOK)->wall_triangle_count() == 0uz);
    CHECK((*K_V1_COOK)->digest() ==
          "cdee4ccb1e8d70a8d9b5c90fba7a8376289a36c30bdabf3f5982861969160941");
}

TEST_CASE("V2 refuses malformed wall faces and changed occupied-cell evidence",
          "[collision-wall]") {
    const auto K_SOURCE = collision_source(true);
    auto document = collision_wall_document(K_SOURCE);
    const auto K_CASE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9);
    switch (K_CASE) {
    case 0:
        document["walls"] = nlohmann::json::array();
        break;
    case 1:
        document["walls"][0uz]["triangles"] = { 0U };
        break;
    case 2:
        document["walls"][0uz]["triangles"] = { 0U, 0U };
        break;
    case 3:
        document["walls"][0uz]["face"] = "POS_X";
        break;
    case 4:
        document["walls"][0uz]["occupied_cell"] = { 2U, 0U, 1U };
        break;
    case 5:
        document["walls"][0uz]["generation"] = 2U;
        break;
    case 6:
        document["walls"][0uz]["chunk_sha256"] = std::string(64uz, '0');
        break;
    case 7:
        document["grid"]["states"].get_ref<std::string&>()[3uz + 8uz * 4uz] = '?';
        break;
    case 8:
        document["walls"].push_back(document["walls"][0uz]);
        break;
    case 9:
        document["schema"] = "SYNTHETIC_COLLISION_SUPPORT_V1";
        break;
    default:
        FAIL("Unknown wall mutation");
    }
    INFO(K_CASE);
    CHECK_FALSE(admit_wall(K_SOURCE, document));
}

TEST_CASE("V2 rejects overlapping duplicate geometry instead of treating it as a full face",
          "[collision-wall]") {
    const auto K_SOURCE = collision_source(true, true);
    const auto K_DOCUMENT = collision_wall_document(K_SOURCE);
    const auto K_REJECTED = admit_wall(K_SOURCE, K_DOCUMENT);
    REQUIRE_FALSE(K_REJECTED);
    CHECK(K_REJECTED.error().m_message.find("full face") != std::string::npos);
}

TEST_CASE("all vertical wall directions retain outward winding in the Jolt shape",
          "[collision-wall]") {
    const std::string_view K_FACE { GENERATE("NEG_X", "POS_X", "NEG_Z", "POS_Z") };
    auto source = std::make_shared<const SceneState>(collision_source(true, false, K_FACE));
    const auto K_SUPPORT = admit_wall(*source, collision_wall_document(*source, K_FACE));
    REQUIRE(K_SUPPORT);
    const auto K_COOK = cook_synthetic_collision(source, *K_SUPPORT);
    REQUIRE(K_COOK);
    const auto K_SHAPE = build_jolt_static_collision(*K_COOK, *source);
    REQUIRE(K_SHAPE);
    Vec3d start { 2, .5, 1.5 }, end { 4, .5, 1.5 };
    if (K_FACE == "POS_X") {
        start = { 5, .5, 1.5 };
        end = { 3, .5, 1.5 };
    } else if (K_FACE == "NEG_Z") {
        start = { 3.5, .5, 0 };
        end = { 3.5, .5, 2 };
    } else if (K_FACE == "POS_Z") {
        start = { 3.5, .5, 3 };
        end = { 3.5, .5, 1 };
    }
    const auto K_HIT = (*K_SHAPE)->ray_cast(*source, start, end);
    REQUIRE(K_HIT);
    REQUIRE(K_HIT->has_value());
    CHECK(K_HIT->value_or(-1.0) == Catch::Approx(.5));
}

TEST_CASE("wall owner rejects corrected sources and cancellation without losing prior geometry",
          "[collision-wall]") {
    auto source = std::make_shared<const SceneState>(collision_source(true));
    const auto K_SUPPORT = admit_wall(*source, collision_wall_document(*source));
    REQUIRE(K_SUPPORT);
    const auto K_COOK = cook_synthetic_collision(source, *K_SUPPORT);
    REQUIRE(K_COOK);
    const auto K_SHAPE = build_jolt_static_collision(*K_COOK, *source);
    REQUIRE(K_SHAPE);
    auto corrected = *source;
    auto wall = std::make_shared<SceneChunk>(*corrected.m_chunks.at(ChunkId { 9007199254740994ULL }));
    ++wall->m_pose_revision.m_value;
    corrected.m_chunks[wall->m_id] = wall;
    const Vec3d K_START { 2, .5, 1.5 };
    const Vec3d K_END { 4, .5, 1.5 };
    const auto K_STALE = (*K_SHAPE)->ray_cast(corrected, K_START, K_END);
    REQUIRE_FALSE(K_STALE);
    CHECK(K_STALE.error().m_code == ErrorCode::WRONG_BASE);
    const auto K_RECOOK =
        cook_synthetic_collision(std::make_shared<const SceneState>(corrected), *K_SUPPORT);
    REQUIRE_FALSE(K_RECOOK);
    CHECK(K_RECOOK.error().m_code == ErrorCode::WRONG_BASE);
    std::stop_source cancelled;
    cancelled.request_stop();
    const auto K_STOPPED = build_jolt_static_collision(*K_COOK, *source, cancelled.get_token());
    REQUIRE_FALSE(K_STOPPED);
    CHECK(K_STOPPED.error().m_code == ErrorCode::CANCELLED);
    const auto K_PRIOR = (*K_SHAPE)->ray_cast(*source, K_START, K_END);
    REQUIRE(K_PRIOR);
    REQUIRE(K_PRIOR->has_value());
    CHECK(K_PRIOR->value_or(-1.0) == Catch::Approx(.5));
}
