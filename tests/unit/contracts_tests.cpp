#include "world_explorer/core/hash.hpp"
#include "world_explorer/map_store/map_store.hpp"
#include "world_explorer/scene_store/bundle.hpp"
#include "world_explorer/scene_store/scene_store.hpp"
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <fstream>
#include <nlohmann/json.hpp>
#include <thread>

using namespace world_explorer;

namespace {
std::shared_ptr<scene_chunk> fixture_chunk(chunk_id id = {1}, std::uint64_t generation = 1) {
    auto chunk = std::make_shared<scene_chunk>();
    chunk->id = id;
    chunk->generation = generation;
    chunk->bounds = {{-5, -5, -5}, {5, 5, 5}};
    chunk->pose_revision = {1};
    chunk->geometry_valid = true;
    gaussian splat;
    splat.mean = {1, 2, 3};
    splat.scale = {0.3F, 0.6F, 0.9F};
    splat.opacity = 0.7F;
    splat.sh[0] = 0.2F;
    chunk->splats.push_back(splat);
    return chunk;
}
scene_transaction create_transaction() {
    return {{42}, {0},   {0},
            {1},  {100}, {{scene_operation_kind::CREATE, {1}, fixture_chunk(), {}, {}}}};
}
struct temporary_directory {
    std::filesystem::path path;
    temporary_directory() {
        static std::atomic<std::uint64_t> count{};
        path = std::filesystem::temp_directory_path() /
               ("world_explorer_contracts_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
                std::to_string(count++));
        std::filesystem::create_directories(path);
    }
    ~temporary_directory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};
nlohmann::json read_json(const std::filesystem::path& path) {
    std::ifstream file(path);
    return nlohmann::json::parse(file);
}
void write_json(const std::filesystem::path& path, const nlohmann::json& value) {
    std::ofstream file(path);
    file << value.dump(2);
}
} // namespace

TEST_CASE("asymmetric optical projection and world pose round trip", "[contracts][coordinates]") {
    pinhole_camera camera{711, 529, 319.25, 241.75};
    const vec3d original{0.71, -0.22, 3.4};
    auto pixel = project(camera, original);
    REQUIRE(pixel);
    auto recovered = unproject(camera, *pixel, original.z);
    REQUIRE(recovered);
    CHECK(std::abs(recovered->x - original.x) < 1e-12);
    CHECK(std::abs(recovered->y - original.y) < 1e-12);
    const similarity_transform world_from_camera{{0, 1, 0, 0}, {2, -3, 7}, 2.5};
    const auto round_trip =
        inverse_transform_point(world_from_camera, transform_point(world_from_camera, original));
    CHECK(std::abs(round_trip.x - original.x) < 1e-12);
    CHECK(std::abs(round_trip.y - original.y) < 1e-12);
    CHECK(std::abs(round_trip.z - original.z) < 1e-12);
    CHECK_FALSE(project(camera, {0, 0, -1}));
    CHECK_FALSE(unproject({0, 1, 0, 0}, {1, 1}, 2));
}

TEST_CASE("scene transactions replay replace delete correction and epoch resync atomically",
          "[contracts][replay]") {
    scene_store store({42});
    auto create = create_transaction();
    REQUIRE(store.commit(create));
    const auto old = store.acquire_snapshot();
    REQUIRE(store.commit(create));
    CHECK(store.acquire_snapshot()->revision.value == 1);
    auto caller_alias = std::const_pointer_cast<scene_chunk>(create.changes[0].chunk);
    caller_alias->splats[0].mean[0] = 4;
    CHECK(old->chunks.at({1})->splats[0].mean[0] == 1);
    CHECK_FALSE(store.commit(create)); // reused ID with modified payload
    auto replacement = fixture_chunk({1}, 2);
    replacement->splats[0].opacity = 0.4F;
    scene_transaction replace{
        {42}, {0}, {1}, {2}, {101}, {{scene_operation_kind::REPLACE, {1}, replacement, {}, {}}}};
    REQUIRE(store.commit(replace));
    CHECK(old->chunks.at({1})->splats[0].opacity == 0.7F);
    auto wrong_base = replace;
    wrong_base.operation = {102};
    wrong_base.base_revision = {0};
    auto rejected = store.commit(wrong_base);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().code == error_code::WRONG_BASE);
    auto wrong_epoch = replace;
    wrong_epoch.operation = {103};
    wrong_epoch.epoch = {1};
    rejected = store.commit(wrong_epoch);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().code == error_code::WRONG_EPOCH);
    scene_transaction correction{
        {42},
        {0},
        {2},
        {3},
        {104},
        {{scene_operation_kind::TRANSFORM, {1}, nullptr, {{0, 1, 0, 0}, {10, 2, 3}, 2}, {3}}}};
    REQUIRE(store.commit(correction));
    const auto corrected = store.acquire_snapshot();
    CHECK_FALSE(corrected->chunks.at({1})->geometry_valid);
    CHECK(corrected->chunks.at({1})->pose_revision.value == 3);
    CHECK(corrected->chunks.at({1})->world_from_chunk.translation.x == 10);
    auto replay = store.changes_since({1}, {100000});
    REQUIRE(replay);
    CHECK(replay->base_revision.value == 1);
    CHECK(replay->new_revision.value == 3);
    CHECK_FALSE(store.changes_since({1}, {1}));
    scene_transaction deletion{{42}, {0},   {3},
                               {4},  {105}, {{scene_operation_kind::ERASE, {1}, nullptr, {}, {}}}};
    REQUIRE(store.commit(deletion));
    CHECK(store.acquire_snapshot()->chunks.empty());
    CHECK(corrected->chunks.size() == 1);
    auto reset = std::make_shared<scene_state>(*old);
    reset->epoch = {1};
    REQUIRE(store.reset(reset));
    CHECK_FALSE(store.commit(deletion));
    CHECK(store.acquire_snapshot()->epoch.value == 1);
}

TEST_CASE("invalid transactions leave all state untouched and replay window is bounded",
          "[contracts][budgets]") {
    scene_store_limits limits;
    limits.journal_entries = 1;
    limits.remembered_operations = 2;
    scene_store store({42}, limits);
    REQUIRE(store.commit(create_transaction()));
    auto invalid = fixture_chunk({2});
    invalid->splats[0].scale[0] = -1;
    scene_transaction transaction{{42},
                                  {0},
                                  {1},
                                  {2},
                                  {102},
                                  {{scene_operation_kind::ERASE, {1}, nullptr, {}, {}},
                                   {scene_operation_kind::CREATE, {2}, invalid, {}, {}}}};
    REQUIRE_FALSE(store.commit(transaction));
    CHECK(store.acquire_snapshot()->chunks.contains({1}));
    transaction.changes.pop_back();
    REQUIRE(store.commit(transaction));
    auto missed = store.changes_since({0}, {100000});
    REQUIRE_FALSE(missed);
    CHECK(missed.error().code == error_code::RESYNC_REQUIRED);
    CHECK_FALSE(store.changes_since({3}, {100000}));
    auto bad = fixture_chunk();
    bad->splats[0].rotation = {0, 0, 0, 0};
    CHECK_FALSE(validate_chunk(*bad));
    bad = fixture_chunk();
    bad->splats[0].mean[0] = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(validate_chunk(*bad));
    limits.scene.max_splats = 0;
    CHECK_FALSE(validate_chunk(*fixture_chunk(), limits.scene));
}

TEST_CASE("concurrent readers retain immutable scene revisions", "[contracts][threads]") {
    scene_store store({42});
    REQUIRE(store.commit(create_transaction()));
    std::atomic<bool> valid_snapshots{true};
    std::jthread reader([&] {
        for (int i = 0; i < 1000; ++i) {
            const auto snapshot = store.acquire_snapshot();
            if (snapshot->chunks.size() != 1 || snapshot->revision.value == 0)
                valid_snapshots = false;
        }
    });
    for (std::uint64_t i = 2; i < 30; ++i) {
        scene_transaction change{
            {42}, {0},       {i - 1},
            {i},  {100 + i}, {{scene_operation_kind::REPLACE, {1}, fixture_chunk({1}, i), {}, {}}}};
        REQUIRE(store.commit(change));
    }
    reader.join();
    CHECK(valid_snapshots.load());
}

TEST_CASE("map corrections invalidate dependencies and reject stale submissions",
          "[contracts][map]") {
    map_store store;
    map_transaction first;
    first.operation = {1};
    first.new_revision = {1};
    first.calibrations.push_back({{1}, {711, 529, 319.25, 241.75}, 640, 480});
    first.observations.push_back(
        {{1}, {1}, {1}, {3, 1, 30}, std::chrono::nanoseconds{100}, std::string(64, 'a')});
    first.alignments.push_back({{1}, {1}, {}});
    pose_estimate pose;
    pose.frame = {1};
    pose.submap = {1};
    pose.revision = {1};
    pose.tracking = tracking_state::TRACKING;
    first.poses.push_back(pose);
    REQUIRE(store.commit(first));
    REQUIRE(store.commit(first));
    const auto old = store.acquire_snapshot();
    map_transaction correction;
    correction.operation = {2};
    correction.base_revision = {1};
    correction.new_revision = {2};
    pose.revision = {2};
    pose.submap_from_camera.translation = {2, 3, 4};
    correction.poses.push_back(pose);
    REQUIRE(store.commit(correction));
    CHECK(store.acquire_snapshot()->invalidated_frames.contains({1}));
    CHECK(old->poses.at({1}).submap_from_camera.translation.x == 0);
    correction.operation = {3};
    CHECK_FALSE(store.commit(correction));
    map_transaction alignment;
    alignment.operation = {4};
    alignment.base_revision = {2};
    alignment.new_revision = {3};
    alignment.alignments.push_back({{1}, {3}, {{0, 1, 0, 0}, {4, 5, 6}, 2}});
    REQUIRE(store.commit(alignment));
    CHECK(store.acquire_snapshot()->invalidated_frames.contains({1}));
    REQUIRE(store.reset({1}));
    CHECK(store.acquire_snapshot()->poses.empty());
    CHECK_FALSE(store.commit(first));
}

TEST_CASE("SHA-256 matches an independent standard test vector", "[contracts][hash]") {
    const std::string text = "abc";
    CHECK(sha256(std::as_bytes(std::span(text.data(), text.size()))) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("bundle round trip enforces checksums paths conventions and allocation bounds",
          "[contracts][bundle]") {
    temporary_directory directory;
    const auto bundle_path =
        directory.path / GENERATE(std::filesystem::path("scene.wexplore"),
                                  std::filesystem::path(u8"room \u00e9\u623f.wexplore"));
    scene_store store({42});
    REQUIRE(store.commit(create_transaction()));
    auto snapshot = std::make_shared<scene_state>(*store.acquire_snapshot());
    const std::string utf8_label = "room \xc3\xa9\xe6\x88\xbf";
    snapshot->provenance["label"] = utf8_label;
    REQUIRE(write_bundle(bundle_path, snapshot));
    auto loaded = load_bundle(bundle_path);
    REQUIRE(loaded);
    CHECK((*loaded)->chunks.at({1})->splats[0].mean[1] == 2);
    CHECK((*loaded)->provenance.at("label") == utf8_label);
    auto manifest = read_json(bundle_path / "manifest.json");
    const auto original = manifest;
    manifest["index"] = "../outside.bin";
    write_json(bundle_path / "manifest.json", manifest);
    CHECK_FALSE(load_bundle(bundle_path));
    manifest = original;
    manifest["coordinates"]["quaternion_order"] = "xyzw";
    write_json(bundle_path / "manifest.json", manifest);
    CHECK_FALSE(load_bundle(bundle_path));
    for (const auto& malformed :
         {std::string{}, std::string("{"), std::string("{\"label\":\"\xff\"}"),
          std::string("{}\0trailing", 11)}) {
        std::ofstream input(bundle_path / "manifest.json", std::ios::binary);
        input.write(malformed.data(), static_cast<std::streamsize>(malformed.size()));
        input.close();
        CHECK_FALSE(load_bundle(bundle_path));
    }
    write_json(bundle_path / "manifest.json", original);
    bundle_limits limits;
    limits.scene.max_splats = 0;
    CHECK_FALSE(load_bundle(bundle_path, limits));
    limits = {};
    limits.max_manifest_bytes = 8;
    CHECK_FALSE(load_bundle(bundle_path, limits));
    const auto index = bundle_path / original.at("index").get<std::string>();
    std::ofstream corrupted(index, std::ios::binary | std::ios::app);
    corrupted.put('x');
    corrupted.close();
    CHECK_FALSE(load_bundle(bundle_path));
}

TEST_CASE("failed publication preserves previous valid bundle", "[contracts][bundle]") {
    temporary_directory directory;
    scene_store store({42});
    REQUIRE(store.commit(create_transaction()));
    REQUIRE(write_bundle(directory.path, store.acquire_snapshot()));
    auto invalid = std::make_shared<scene_state>(*store.acquire_snapshot());
    auto chunk = fixture_chunk();
    chunk->splats[0].scale[0] = 0;
    invalid->chunks[{1}] = chunk;
    REQUIRE_FALSE(write_bundle(directory.path, invalid));
    auto loaded = load_bundle(directory.path);
    REQUIRE(loaded);
    CHECK((*loaded)->chunks.at({1})->splats[0].scale[0] == 0.3F);
}

TEST_CASE(
    "gsplat interchange decoder converts logits scales quaternions and rejects ambiguous input",
    "[contracts][ply]") {
    temporary_directory directory;
    const auto path = directory.path / "gsplat.ply";
    const std::string header =
        "ply\nformat ascii 1.0\ncomment exporter gsplat-1.5.3\ncomment color_domain srgb\nelement "
        "vertex 1\nproperty float x\nproperty float y\nproperty float z\nproperty float "
        "f_dc_0\nproperty float f_dc_1\nproperty float f_dc_2\nproperty float opacity\nproperty "
        "float scale_0\nproperty float scale_1\nproperty float scale_2\nproperty float "
        "rot_0\nproperty float rot_1\nproperty float rot_2\nproperty float rot_3\nend_header\n";
    {
        std::ofstream output(path);
        output << header << "1 2 3 0.2 0.3 0.4 0 0 0 0 2 0 0 0\n";
    }
    auto loaded = load_gsplat_ply(path, {42});
    REQUIRE(loaded);
    const auto& chunk = *(*loaded)->chunks.at({1});
    CHECK(chunk.splats[0].opacity == 0.5F);
    CHECK(chunk.splats[0].scale[0] == 1);
    CHECK(chunk.splats[0].rotation[0] == 1);
    const auto transformed = transform_point(chunk.world_from_chunk, {1, 2, 3});
    CHECK(transformed.y == -2);
    CHECK(transformed.z == -3);
    {
        std::ofstream output(path);
        output << header << "1 2 3 0.2 0.3 0.4 0 1000 0 0 1 0 0 0\n";
    }
    CHECK_FALSE(load_gsplat_ply(path, {42}));
    {
        std::ofstream output(path);
        output << "ply\nformat ascii 1.0\nelement vertex 1\nend_header\n";
    }
    CHECK_FALSE(load_gsplat_ply(path, {42}));
}
