#include "../fixtures/collision_support_fixture.hpp"
#include "world_explorer/collision/triangle_cook.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/scene_store/world_frame.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <stop_token>
#include <type_traits>
#include <utility>

using namespace world_explorer;
using namespace world_explorer::testing;

namespace {

using Json = nlohmann::json;

Result<std::shared_ptr<const CollisionSupport>> support_for(const SceneState& source,
                                                            const Json& document) {
    const auto K_TEXT = document.dump();
    return admit_collision_support(source, K_TEXT, collision_digest(K_TEXT));
}

void set_cell(Json& document, std::array<std::size_t, 3uz> index, char state) {
    auto& cells = document["grid"]["states"].get_ref<std::string&>();
    cells.at(index[0uz] + 8uz * (index[1uz] + 4uz * index[2uz])) = state;
}

std::uint64_t read_u64(std::span<const std::byte> bytes, std::size_t offset) {
    std::uint64_t value {};
    for (std::size_t index { 0uz }; index < 8uz; ++index) {
        value |= std::to_integer<std::uint64_t>(bytes[offset + index]) << (8uz * index);
    }
    return value;
}

constexpr CapsuleSweep K_SWEEP {{1.5, 0, 1.5}, {4.5, 0, 1.5}, .2, 1.7};

}  // namespace

TEST_CASE("collision cook binds ordered source floor triangles and canonical bytes",
          "[collision-cook]") {
    STATIC_REQUIRE(!std::is_copy_constructible_v<CanonicalCollisionMesh>);
    auto source = std::make_shared<const SceneState>(collision_source());
    auto document = collision_document(*source);
    const auto K_SUPPORT = support_for(*source, document);
    REQUIRE(K_SUPPORT);
    const auto K_COOK = cook_synthetic_collision(source, *K_SUPPORT);
    REQUIRE(K_COOK);
    const auto K_REPEAT = cook_synthetic_collision(source, *K_SUPPORT);
    REQUIRE(K_REPEAT);
    CHECK((*K_COOK)->triangles().size() == 2uz);
    CHECK((*K_COOK)->parameters().m_format_version == 1U);
    CHECK(!(*K_COOK)->parameters().m_weld_vertices);
    CHECK(!(*K_COOK)->parameters().m_double_sided);
    CHECK((*K_COOK)->source_sha256() == scene_content_hash(*source));
    CHECK((*K_COOK)->support_sha256() == (*K_SUPPORT)->document_sha256());
    CHECK((*K_COOK)->digest() == (*K_REPEAT)->digest());
    CHECK((*K_COOK)->canonical_bytes().size() == (*K_REPEAT)->canonical_bytes().size());
    CHECK(std::equal((*K_COOK)->canonical_bytes().begin(),
                     (*K_COOK)->canonical_bytes().end(),
                     (*K_REPEAT)->canonical_bytes().begin()));
    CHECK((*K_COOK)->digest() == sha256((*K_COOK)->canonical_bytes()));
    const auto K_MAGIC = std::string_view {"WE_COLLISION_TRIANGLES_V1"};
    const auto K_BYTES = (*K_COOK)->canonical_bytes();
    CHECK(K_BYTES.size() ==
          K_MAGIC.size() + 6uz * 8uz + 2uz * 64uz + 8uz + 2uz * (4uz * 8uz + 9uz * 8uz));
    CHECK(read_u64(K_BYTES, K_MAGIC.size() + 6uz * 8uz + 2uz * 64uz) == 2U);
    CHECK(read_u64(K_BYTES, K_MAGIC.size() + 6uz * 8uz + 2uz * 64uz + 8uz) == 9007199254740993ULL);
    CHECK((*K_COOK)->triangles()[0uz].m_triangle_index == 0U);
    CHECK((*K_COOK)->triangles()[1uz].m_triangle_index == 1U);
    for (const auto& triangle : (*K_COOK)->triangles()) {
        const auto K_NORMAL =
            cross(triangle.m_world_vertices[1uz] - triangle.m_world_vertices[0uz],
                  triangle.m_world_vertices[2uz] - triangle.m_world_vertices[0uz]);
        CHECK(K_NORMAL.m_y > 0);
    }

    document["floors"][0]["triangles"] = { 1U, 0U };
    const auto K_REORDERED_SUPPORT = support_for(*source, document);
    REQUIRE(K_REORDERED_SUPPORT);
    const auto K_REORDERED = cook_synthetic_collision(source, *K_REORDERED_SUPPORT);
    REQUIRE(K_REORDERED);
    CHECK((*K_REORDERED)->triangles()[0uz].m_triangle_index == 0U);
    CHECK((*K_REORDERED)->triangles()[1uz].m_triangle_index == 1U);
    CHECK((*K_REORDERED)->digest() != (*K_COOK)->digest());
}

TEST_CASE("cooked synthetic floors retain barrier and unknown blocking", "[collision-cook]") {
    auto source = std::make_shared<const SceneState>(collision_source());
    auto document = collision_document(*source);
    const auto K_CLEAR_SUPPORT = support_for(*source, document);
    REQUIRE(K_CLEAR_SUPPORT);
    const auto K_CLEAR_COOK = cook_synthetic_collision(source, *K_CLEAR_SUPPORT);
    REQUIRE(K_CLEAR_COOK);
    const auto K_CLEAR = (*K_CLEAR_COOK)->probe(*source, K_SWEEP);
    REQUIRE(K_CLEAR);
    CHECK(K_CLEAR->m_state == ClearanceState::CLEAR_DIAGNOSTIC);
    for (const auto& [K_STATE, K_EXPECTED] :
         {std::pair {'#', ClearanceState::OCCUPIED}, std::pair {'?', ClearanceState::UNKNOWN}}) {
        set_cell(document, {3uz, 0uz, 1uz}, K_STATE);
        const auto K_SUPPORT = support_for(*source, document);
        REQUIRE(K_SUPPORT);
        const auto K_COOK = cook_synthetic_collision(source, *K_SUPPORT);
        REQUIRE(K_COOK);
        const auto K_PROBE = (*K_COOK)->probe(*source, K_SWEEP);
        REQUIRE(K_PROBE);
        CHECK(K_PROBE->m_state == K_EXPECTED);
        CHECK((*K_COOK)->digest() != (*K_CLEAR_COOK)->digest());
    }
    CHECK(walking_block_reason(*source) == WalkingBlockReason::SYNTHETIC_DIAGNOSTIC);
}

TEST_CASE("collision cook rejects corrected context and preserves its prior owner",
          "[collision-cook]") {
    auto source = std::make_shared<const SceneState>(collision_source());
    const auto K_SUPPORT = support_for(*source, collision_document(*source));
    REQUIRE(K_SUPPORT);
    const auto K_COOK = cook_synthetic_collision(source, *K_SUPPORT);
    REQUIRE(K_COOK);
    const auto K_SOURCE_HASH = scene_content_hash(*source);
    const auto K_DIGEST = (*K_COOK)->digest();
    auto changed = *source;
    ++changed.m_epoch.m_value;
    const auto K_EPOCH =
        cook_synthetic_collision(std::make_shared<const SceneState>(changed), *K_SUPPORT);
    REQUIRE(!K_EPOCH);
    CHECK(K_EPOCH.error().m_code == ErrorCode::WRONG_EPOCH);
    changed = *source;
    ++changed.m_revision.m_value;
    const auto K_REVISION = (*K_COOK)->validate_source(changed);
    REQUIRE(!K_REVISION);
    CHECK(K_REVISION.error().m_code == ErrorCode::WRONG_BASE);
    changed = *source;
    auto corrected = std::make_shared<SceneChunk>(*changed.m_chunks.begin()->second);
    ++corrected->m_pose_revision.m_value;
    changed.m_chunks[corrected->m_id] = corrected;
    const auto K_POSE =
        cook_synthetic_collision(std::make_shared<const SceneState>(changed), *K_SUPPORT);
    REQUIRE(!K_POSE);
    CHECK(K_POSE.error().m_code == ErrorCode::WRONG_BASE);
    std::stop_source cancelled;
    cancelled.request_stop();
    const auto K_STOPPED = cook_synthetic_collision(source, *K_SUPPORT, cancelled.get_token());
    REQUIRE(!K_STOPPED);
    CHECK(K_STOPPED.error().m_code == ErrorCode::CANCELLED);
    CHECK((*K_COOK)->digest() == K_DIGEST);
    CHECK(scene_content_hash(*source) == K_SOURCE_HASH);
    CHECK((*K_COOK)->probe(*source, K_SWEEP)->m_state == ClearanceState::CLEAR_DIAGNOSTIC);
}
