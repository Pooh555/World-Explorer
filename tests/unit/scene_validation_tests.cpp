#include "world_explorer/scene_store/scene_store.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <limits>

using namespace world_explorer;
namespace {
std::shared_ptr<scene_chunk> valid_chunk(chunk_id id = {1}) {
    auto chunk = std::make_shared<scene_chunk>();
    chunk->id = id;
    chunk->bounds = {{-1, -1, -1}, {1, 1, 1}};
    chunk->pose_revision = {1};
    chunk->geometry_valid = true;
    chunk->source_frames = {{1}, {2}};
    chunk->splats.emplace_back();
    return chunk;
}
scene_transaction initial_transaction() {
    return {{1}, {0}, {0}, {1}, {1}, {{scene_operation_kind::CREATE, {1}, valid_chunk(), {}, {}}}};
}
} // namespace

TEST_CASE("optical camera admission rejects nonfinite intrinsics and pixels",
          "[contracts][validation][coordinates]") {
    pinhole_camera camera{711, 529, 319.25, 241.75};
    auto point = vec3d{1, 2, 3};
    const auto infinity = std::numeric_limits<double>::infinity();
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6);
    CAPTURE(field);
    switch (field) {
    case 0:
        camera.fx = infinity;
        break;
    case 1:
        camera.fy = infinity;
        break;
    case 2:
        camera.fx = 0;
        break;
    case 3:
        camera.fy = 0;
        break;
    case 4:
        camera.cx = infinity;
        break;
    case 5:
        camera.cy = infinity;
        break;
    case 6:
        point.z = 0;
        break;
    default:
        FAIL("unknown camera mutation");
    }
    const auto rejected = project(camera, point);
    REQUIRE(!rejected);
    CHECK(rejected.error().code == error_code::INVALID_ARGUMENT);
    CHECK_FALSE(unproject({711, 529, 319.25, 241.75}, {infinity, 0}, 3));
    CHECK_FALSE(unproject({711, 529, 319.25, 241.75}, {0, infinity}, 3));
}

TEST_CASE("hostile chunk fields are rejected before scene admission", "[contracts][validation]") {
    auto chunk = valid_chunk();
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
                                19, 20, 21, 22, 23, 24, 25, 26, 27);
    CAPTURE(field);
    const auto infinity = std::numeric_limits<float>::infinity();
    switch (field) {
    case 0:
        chunk->id = {};
        break;
    case 1:
        chunk->generation = 0;
        break;
    case 2:
        chunk->bounds.minimum.x = 2;
        break;
    case 3:
        chunk->bounds.minimum.y = 2;
        break;
    case 4:
        chunk->bounds.minimum.z = 2;
        break;
    case 5:
        chunk->world_from_chunk.scale = 0;
        break;
    case 6:
        chunk->world_from_chunk.translation.y = infinity;
        break;
    case 7:
        chunk->pose_revision = {};
        break;
    case 8:
        chunk->splats[0].sh_degree = 4;
        break;
    case 9:
        chunk->splats[0].opacity = infinity;
        break;
    case 10:
        chunk->splats[0].opacity = -0.1F;
        break;
    case 11:
        chunk->splats[0].opacity = 1.1F;
        break;
    case 12:
        chunk->splats[0].rotation = {infinity, 0, 0, 0};
        break;
    case 13:
        chunk->splats[0].scale[1] = infinity;
        break;
    case 14:
        chunk->splats[0].scale[2] = 0;
        break;
    case 15:
        chunk->splats[0].mean[0] = -2;
        break;
    case 16:
        chunk->splats[0].mean[1] = -2;
        break;
    case 17:
        chunk->splats[0].mean[2] = -2;
        break;
    case 18:
        chunk->splats[0].mean[0] = 2;
        break;
    case 19:
        chunk->splats[0].mean[1] = 2;
        break;
    case 20:
        chunk->splats[0].sh[47] = infinity;
        break;
    case 21:
        chunk->splats[0].mean[2] = 2;
        break;
    case 22:
        chunk->world_from_chunk.translation.x = infinity;
        break;
    case 23:
        chunk->world_from_chunk.translation.z = infinity;
        break;
    case 24:
        chunk->world_from_chunk.rotation = {0, 0, 0, 0};
        break;
    case 25:
        chunk->world_from_chunk.scale = infinity;
        break;
    case 26:
        chunk->bounds.minimum.x = infinity;
        break;
    case 27:
        chunk->bounds.maximum.z = infinity;
        break;
    default:
        FAIL("unknown validation mutation");
    }
    const auto rejected = validate_chunk(*chunk);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().code == error_code::INVALID_DATA);
}

TEST_CASE("scene metadata and aggregate budgets reject hostile snapshots",
          "[contracts][validation]") {
    scene_state scene;
    scene.id = {1};
    scene.chunks.emplace(chunk_id{1}, valid_chunk());
    scene_limits limits;
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12);
    CAPTURE(field);
    auto expected = error_code::RESOURCE_EXHAUSTED;
    switch (field) {
    case 0:
        scene.id = {};
        expected = error_code::INVALID_DATA;
        break;
    case 1:
        scene.provenance[""] = "empty key";
        break;
    case 2:
        scene.provenance[std::string(129, 'x')] = "long key";
        break;
    case 3:
        scene.provenance["key"] = std::string(4097, 'x');
        break;
    case 4:
        for (unsigned index = 0; index < 33; ++index)
            scene.provenance[std::to_string(index)] = "value";
        break;
    case 5:
        limits.max_chunks = 0;
        break;
    case 6:
        scene.chunks[{1}] = nullptr;
        expected = error_code::INVALID_DATA;
        break;
    case 7:
        scene.chunks[{1}] = valid_chunk({2});
        expected = error_code::INVALID_DATA;
        break;
    case 8:
        limits.max_payload_bytes = sizeof(gaussian) - 1;
        break;
    case 9:
        limits.max_source_frames = 1;
        break;
    case 10:
        scene.chunks.emplace(chunk_id{2}, valid_chunk({2}));
        limits.max_payload_bytes = sizeof(gaussian);
        break;
    case 11:
        scene.chunks.emplace(chunk_id{2}, valid_chunk({2}));
        limits.max_splats = 1;
        break;
    case 12:
        scene.scale = static_cast<scale_status>(255);
        expected = error_code::INVALID_DATA;
        break;
    default:
        FAIL("unknown validation mutation");
    }
    const auto rejected = validate_scene(scene, limits);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().code == expected);
}

TEST_CASE("rejected scene operations preserve snapshots and reusable operation IDs",
          "[contracts][validation][replay]") {
    scene_store_limits limits;
    limits.scene.max_chunks = 2;
    scene_store store({1}, limits);
    REQUIRE(store.commit(initial_transaction()));
    const auto before = store.acquire_snapshot();
    scene_transaction transaction{
        {1}, {0}, {1}, {2}, {2}, {{scene_operation_kind::REPLACE, {1}, valid_chunk(), {}, {}}}};
    auto replacement = std::const_pointer_cast<scene_chunk>(transaction.changes[0].chunk);
    replacement->generation = 2;
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11);
    CAPTURE(field);
    switch (field) {
    case 0:
        transaction.id = {2};
        break;
    case 1:
        transaction.operation = {};
        break;
    case 2:
        transaction.new_revision = {3};
        break;
    case 3:
        transaction.changes.resize(3, transaction.changes[0]);
        break;
    case 4:
        transaction.changes.push_back(transaction.changes[0]);
        break;
    case 5:
        transaction.changes[0].chunk.reset();
        break;
    case 6:
        replacement->id = {2};
        break;
    case 7:
        transaction.changes[0].kind = scene_operation_kind::CREATE;
        break;
    case 8:
        transaction.changes[0].id = {2};
        replacement->id = {2};
        break;
    case 9:
        replacement->generation = 1;
        break;
    case 10:
        transaction.changes[0] = {scene_operation_kind::ERASE, {2}, nullptr, {}, {}};
        break;
    case 11:
        transaction.changes[0] = {static_cast<scene_operation_kind>(255), {1}, nullptr, {}, {}};
        break;
    default:
        FAIL("unknown validation mutation");
    }
    REQUIRE_FALSE(store.commit(transaction));
    CHECK(store.acquire_snapshot() == before);
    scene_transaction repair{
        {1}, {0}, {1},
        {2}, {2}, {{scene_operation_kind::INVALIDATE_GEOMETRY, {1}, nullptr, {}, {2}}}};
    REQUIRE(store.commit(repair));
    CHECK_FALSE(store.acquire_snapshot()->chunks.at({1})->geometry_valid);
    CHECK(before->chunks.at({1})->geometry_valid);
    REQUIRE(store.commit(repair));
}

TEST_CASE("scene reset and replay honor empty windows and exhausted counters",
          "[contracts][validation][replay]") {
    scene_store_limits limits;
    limits.journal_entries = 0;
    scene_store store({1}, limits);
    auto empty_replay = store.changes_since({}, {0});
    REQUIRE(empty_replay);
    CHECK(empty_replay->operation.value == 1);
    CHECK_FALSE(store.reset(nullptr));
    REQUIRE(store.commit(initial_transaction()));
    CHECK_FALSE(store.changes_since({}, {100000}));
    REQUIRE(store.changes_since({1}, {100000}));
    auto reset = std::make_shared<scene_state>(*store.acquire_snapshot());
    CHECK_FALSE(store.reset(reset));
    reset->epoch = {1};
    reset->id = {2};
    CHECK_FALSE(store.reset(reset));
    reset->id = {1};
    reset->revision = {std::numeric_limits<std::uint64_t>::max()};
    auto chunk = valid_chunk();
    chunk->generation = std::numeric_limits<std::uint64_t>::max();
    reset->chunks[{1}] = chunk;
    REQUIRE(store.reset(reset));
    const auto before = store.acquire_snapshot();
    scene_transaction overflow{{1}, {1}, reset->revision, {0}, {2}, {}};
    REQUIRE_FALSE(store.commit(overflow));
    CHECK(store.acquire_snapshot() == before);
    reset->epoch = {2};
    reset->revision = {1};
    REQUIRE(store.reset(reset));
    const auto reset_snapshot = store.acquire_snapshot();
    overflow.epoch = {2};
    overflow.base_revision = {1};
    overflow.new_revision = {2};
    overflow.changes = {{scene_operation_kind::TRANSFORM, {1}, nullptr, {}, {2}}};
    REQUIRE_FALSE(store.commit(overflow));
    CHECK(store.acquire_snapshot() == reset_snapshot);
    chunk->generation = 1;
    CHECK(reset_snapshot->chunks.at({1})->generation == std::numeric_limits<std::uint64_t>::max());
    auto invalid = std::make_shared<scene_state>(*reset);
    invalid->id = {};
    CHECK_FALSE(store.reset(invalid));
    CHECK_FALSE(make_snapshot_batch(nullptr, {1}));
    CHECK_FALSE(make_snapshot_batch(reset_snapshot, {}));
    CHECK_FALSE(make_snapshot_batch(invalid, {1}));
}
