#include "world_explorer/scene_store/scene_store.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <limits>

using namespace world_explorer;

namespace {

std::shared_ptr<SceneChunk> valid_chunk(ChunkId id = { 1 }) {
    auto chunk = std::make_shared<SceneChunk>();
    chunk->m_id = id;
    chunk->m_bounds = { { -1, -1, -1 }, { 1, 1, 1 } };
    chunk->m_pose_revision = { 1 };
    chunk->m_geometry_valid = true;
    chunk->m_source_frames = { { 1 }, { 2 } };
    chunk->m_splats.emplace_back();
    return chunk;
}

SceneTransaction initial_transaction() {
    return { { 1 }, { 0 }, { 0 }, { 1 }, { 1 }, { { SceneOperationKind::CREATE, { 1 }, valid_chunk(), {}, {} } } };
}

}  // namespace

TEST_CASE("optical camera admission rejects nonfinite intrinsics and pixels",
          "[contracts][validation][coordinates]") {
    PinholeCamera camera { 711, 529, 319.25, 241.75 };
    auto point = Vec3d { 1, 2, 3 };
    const auto K_INFINITY = std::numeric_limits<double>::infinity();
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6);
    CAPTURE(K_FIELD);
    switch (K_FIELD) {
    case 0:
        camera.m_fx = K_INFINITY;
        break;
    case 1:
        camera.m_fy = K_INFINITY;
        break;
    case 2:
        camera.m_fx = 0;
        break;
    case 3:
        camera.m_fy = 0;
        break;
    case 4:
        camera.m_cx = K_INFINITY;
        break;
    case 5:
        camera.m_cy = K_INFINITY;
        break;
    case 6:
        point.m_z = 0;
        break;
    default:
        FAIL("unknown camera mutation");
    }
    const auto K_REJECTED = project(camera, point);
    REQUIRE(!K_REJECTED);
    CHECK(K_REJECTED.error().m_code == ErrorCode::INVALID_ARGUMENT);
    CHECK_FALSE(unproject({ 711, 529, 319.25, 241.75 }, { K_INFINITY, 0 }, 3));
    CHECK_FALSE(unproject({ 711, 529, 319.25, 241.75 }, { 0, K_INFINITY }, 3));
}

TEST_CASE("hostile chunk fields are rejected before scene admission", "[contracts][validation]") {
    auto chunk = valid_chunk();
    const auto K_FIELD = GENERATE(0,
                                  1,
                                  2,
                                  3,
                                  4,
                                  5,
                                  6,
                                  7,
                                  8,
                                  9,
                                  10,
                                  11,
                                  12,
                                  13,
                                  14,
                                  15,
                                  16,
                                  17,
                                  18,
                                  19,
                                  20,
                                  21,
                                  22,
                                  23,
                                  24,
                                  25,
                                  26,
                                  27);
    CAPTURE(K_FIELD);
    const auto K_INFINITY = std::numeric_limits<float>::infinity();
    switch (K_FIELD) {
    case 0:
        chunk->m_id = {};
        break;
    case 1:
        chunk->m_generation = 0;
        break;
    case 2:
        chunk->m_bounds.m_minimum.m_x = 2;
        break;
    case 3:
        chunk->m_bounds.m_minimum.m_y = 2;
        break;
    case 4:
        chunk->m_bounds.m_minimum.m_z = 2;
        break;
    case 5:
        chunk->m_world_from_chunk.m_scale = 0;
        break;
    case 6:
        chunk->m_world_from_chunk.m_translation.m_y = K_INFINITY;
        break;
    case 7:
        chunk->m_pose_revision = {};
        break;
    case 8:
        chunk->m_splats[0].m_sh_degree = 4;
        break;
    case 9:
        chunk->m_splats[0].m_opacity = K_INFINITY;
        break;
    case 10:
        chunk->m_splats[0].m_opacity = -0.1F;
        break;
    case 11:
        chunk->m_splats[0].m_opacity = 1.1F;
        break;
    case 12:
        chunk->m_splats[0].m_rotation = { K_INFINITY, 0, 0, 0 };
        break;
    case 13:
        chunk->m_splats[0].m_scale[1] = K_INFINITY;
        break;
    case 14:
        chunk->m_splats[0].m_scale[2] = 0;
        break;
    case 15:
        chunk->m_splats[0].m_mean[0] = -2;
        break;
    case 16:
        chunk->m_splats[0].m_mean[1] = -2;
        break;
    case 17:
        chunk->m_splats[0].m_mean[2] = -2;
        break;
    case 18:
        chunk->m_splats[0].m_mean[0] = 2;
        break;
    case 19:
        chunk->m_splats[0].m_mean[1] = 2;
        break;
    case 20:
        chunk->m_splats[0].m_sh[47] = K_INFINITY;
        break;
    case 21:
        chunk->m_splats[0].m_mean[2] = 2;
        break;
    case 22:
        chunk->m_world_from_chunk.m_translation.m_x = K_INFINITY;
        break;
    case 23:
        chunk->m_world_from_chunk.m_translation.m_z = K_INFINITY;
        break;
    case 24:
        chunk->m_world_from_chunk.m_rotation = { 0, 0, 0, 0 };
        break;
    case 25:
        chunk->m_world_from_chunk.m_scale = K_INFINITY;
        break;
    case 26:
        chunk->m_bounds.m_minimum.m_x = K_INFINITY;
        break;
    case 27:
        chunk->m_bounds.m_maximum.m_z = K_INFINITY;
        break;
    default:
        FAIL("unknown validation mutation");
    }
    const auto K_REJECTED = validate_chunk(*chunk);
    REQUIRE_FALSE(K_REJECTED);
    CHECK(K_REJECTED.error().m_code == ErrorCode::INVALID_DATA);
}

TEST_CASE("scene metadata and aggregate budgets reject hostile snapshots",
          "[contracts][validation]") {
    SceneState scene;
    scene.m_id = { 1 };
    scene.m_chunks.emplace(ChunkId { 1 }, valid_chunk());
    SceneLimits limits;
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12);
    CAPTURE(K_FIELD);
    auto expected = ErrorCode::RESOURCE_EXHAUSTED;
    switch (K_FIELD) {
    case 0:
        scene.m_id = {};
        expected = ErrorCode::INVALID_DATA;
        break;
    case 1:
        scene.m_provenance[""] = "empty key";
        break;
    case 2:
        scene.m_provenance[std::string(129, 'x')] = "long key";
        break;
    case 3:
        scene.m_provenance["key"] = std::string(4097, 'x');
        break;
    case 4:
        for (unsigned index { 0 }; index < 33; ++index) {
            scene.m_provenance[std::to_string(index)] = "value";
        }
        break;
    case 5:
        limits.m_max_chunks = 0;
        break;
    case 6:
        scene.m_chunks[{ 1 }] = nullptr;
        expected = ErrorCode::INVALID_DATA;
        break;
    case 7:
        scene.m_chunks[{ 1 }] = valid_chunk({ 2 });
        expected = ErrorCode::INVALID_DATA;
        break;
    case 8:
        limits.m_max_payload_bytes = sizeof(Gaussian) - 1;
        break;
    case 9:
        limits.m_max_source_frames = 1;
        break;
    case 10:
        scene.m_chunks.emplace(ChunkId { 2 }, valid_chunk({ 2 }));
        limits.m_max_payload_bytes = sizeof(Gaussian);
        break;
    case 11:
        scene.m_chunks.emplace(ChunkId { 2 }, valid_chunk({ 2 }));
        limits.m_max_splats = 1;
        break;
    case 12:
        scene.m_scale = static_cast<ScaleStatus>(255);
        expected = ErrorCode::INVALID_DATA;
        break;
    default:
        FAIL("unknown validation mutation");
    }
    const auto K_REJECTED = validate_scene(scene, limits);
    REQUIRE_FALSE(K_REJECTED);
    CHECK(K_REJECTED.error().m_code == expected);
}

TEST_CASE("rejected scene operations preserve snapshots and reusable operation IDs",
          "[contracts][validation][replay]") {
    SceneStoreLimits limits;
    limits.m_scene.m_max_chunks = 2;
    SceneStore store({ 1 }, limits);
    REQUIRE(store.commit(initial_transaction()));
    const auto K_BEFORE = store.acquire_snapshot();
    SceneTransaction transaction {
        { 1 }, { 0 }, { 1 }, { 2 }, { 2 }, { { SceneOperationKind::REPLACE, { 1 }, valid_chunk(), {}, {} } } };
    auto replacement = std::const_pointer_cast<SceneChunk>(transaction.m_changes[0].m_chunk);
    replacement->m_generation = 2;
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11);
    CAPTURE(K_FIELD);
    switch (K_FIELD) {
    case 0:
        transaction.m_id = { 2 };
        break;
    case 1:
        transaction.m_operation = {};
        break;
    case 2:
        transaction.m_new_revision = { 3 };
        break;
    case 3:
        transaction.m_changes.resize(3, transaction.m_changes[0]);
        break;
    case 4:
        transaction.m_changes.push_back(transaction.m_changes[0]);
        break;
    case 5:
        transaction.m_changes[0].m_chunk.reset();
        break;
    case 6:
        replacement->m_id = { 2 };
        break;
    case 7:
        transaction.m_changes[0].m_kind = SceneOperationKind::CREATE;
        break;
    case 8:
        transaction.m_changes[0].m_id = { 2 };
        replacement->m_id = { 2 };
        break;
    case 9:
        replacement->m_generation = 1;
        break;
    case 10:
        transaction.m_changes[0] = { SceneOperationKind::ERASE, { 2 }, nullptr, {}, {} };
        break;
    case 11:
        transaction.m_changes[0] = { static_cast<SceneOperationKind>(255), { 1 }, nullptr, {}, {} };
        break;
    default:
        FAIL("unknown validation mutation");
    }
    REQUIRE_FALSE(store.commit(transaction));
    CHECK(store.acquire_snapshot() == K_BEFORE);
    SceneTransaction repair { { 1 },
                             { 0 },
                             { 1 },
                             { 2 },
                             { 2 },
                             { { SceneOperationKind::INVALIDATE_GEOMETRY, { 1 }, nullptr, {}, { 2 } } } };
    REQUIRE(store.commit(repair));
    CHECK_FALSE(store.acquire_snapshot()->m_chunks.at({ 1 })->m_geometry_valid);
    CHECK(K_BEFORE->m_chunks.at({ 1 })->m_geometry_valid);
    REQUIRE(store.commit(repair));
}

TEST_CASE("scene reset and replay honor empty windows and exhausted counters",
          "[contracts][validation][replay]") {
    SceneStoreLimits limits;
    limits.m_journal_entries = 0;
    SceneStore store({ 1 }, limits);
    auto empty_replay = store.changes_since({}, { 0 });
    REQUIRE(empty_replay);
    CHECK(empty_replay->m_operation.m_value == 1);
    CHECK_FALSE(store.reset(nullptr));
    REQUIRE(store.commit(initial_transaction()));
    CHECK_FALSE(store.changes_since({}, { 100000 }));
    REQUIRE(store.changes_since({ 1 }, { 100000 }));
    auto reset = std::make_shared<SceneState>(*store.acquire_snapshot());
    CHECK_FALSE(store.reset(reset));
    reset->m_epoch = { 1 };
    reset->m_id = { 2 };
    CHECK_FALSE(store.reset(reset));
    reset->m_id = { 1 };
    reset->m_revision = { std::numeric_limits<std::uint64_t>::max() };
    auto chunk = valid_chunk();
    chunk->m_generation = std::numeric_limits<std::uint64_t>::max();
    reset->m_chunks[{ 1 }] = chunk;
    REQUIRE(store.reset(reset));
    const auto K_BEFORE = store.acquire_snapshot();
    SceneTransaction overflow { { 1 }, { 1 }, reset->m_revision, { 0 }, { 2 }, {} };
    REQUIRE_FALSE(store.commit(overflow));
    CHECK(store.acquire_snapshot() == K_BEFORE);
    reset->m_epoch = { 2 };
    reset->m_revision = { 1 };
    REQUIRE(store.reset(reset));
    const auto K_RESET_SNAPSHOT = store.acquire_snapshot();
    overflow.m_epoch = { 2 };
    overflow.m_base_revision = { 1 };
    overflow.m_new_revision = { 2 };
    overflow.m_changes = { { SceneOperationKind::TRANSFORM, { 1 }, nullptr, {}, { 2 } } };
    REQUIRE_FALSE(store.commit(overflow));
    CHECK(store.acquire_snapshot() == K_RESET_SNAPSHOT);
    chunk->m_generation = 1;
    CHECK(K_RESET_SNAPSHOT->m_chunks.at({ 1 })->m_generation ==
          std::numeric_limits<std::uint64_t>::max());
    auto invalid = std::make_shared<SceneState>(*reset);
    invalid->m_id = {};
    CHECK_FALSE(store.reset(invalid));
    CHECK_FALSE(make_snapshot_batch(nullptr, { 1 }));
    CHECK_FALSE(make_snapshot_batch(K_RESET_SNAPSHOT, {}));
    CHECK_FALSE(make_snapshot_batch(invalid, { 1 }));
}
