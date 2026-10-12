#include "checkpoint_generated.h"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/map_store/map_store.hpp"
#include "world_explorer/map_store/pose_asset.hpp"
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <fstream>
#include <nlohmann/json.hpp>
#include <thread>
using namespace world_explorer;
namespace {
struct directory {
    std::filesystem::path path;
    directory() {
        static std::atomic<unsigned> count{};
        path = std::filesystem::temp_directory_path() /
               ("we_persistent_map_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
                std::to_string(count++));
        REQUIRE(std::filesystem::create_directory(path));
    }
    ~directory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};
map_transaction first_transaction() {
    map_transaction tx;
    tx.operation = {100};
    tx.new_revision = {1};
    tx.calibrations = {{{7},
                        {711, 529, 319.25, 241.75},
                        640,
                        480,
                        calibration_model::OPENCV,
                        {.1, -.2, .003, -.004},
                        {.5, .75, -3.25, 2.5}}};
    tx.observations = {
        {{1}, {8}, {7}, {-7, 1001, 30000}, std::chrono::nanoseconds{123}, std::string(64, 'a')},
        {{2},
         {8},
         {7},
         {11, 1001, 30000},
         std::chrono::nanoseconds{367033333},
         std::string(64, 'b')}};
    tx.alignments = {{{55}, {1}, {{0, 1, 0, 0}, {10, -20, 30}, 2.5}}};
    tx.poses = {{{1},
                 {55},
                 {{.5, .5, .5, .5}, {1, 2, 3}, 1},
                 std::chrono::nanoseconds{123},
                 {1},
                 tracking_state::TRACKING,
                 {}},
                {{2},
                 {55},
                 {{0, 1, 0, 0}, {-1, 3, -2}, 1},
                 std::chrono::nanoseconds{367033333},
                 {1},
                 tracking_state::TRACKING,
                 {}}};
    return tx;
}
std::unique_ptr<map_store> open(const std::filesystem::path& root, bool create = false,
                                map_store_limits limits = {}) {
    auto store =
        map_store::open_persistent({root, limits, {std::size_t{256} * 1024 * 1024}, create});
    REQUIRE(store);
    return std::move(*store);
}
std::string manifest_digest(const std::filesystem::path& root) {
    const auto hash = sha256_file(root / "manifest.json", 65536);
    REQUIRE(hash);
    return *hash;
}
nlohmann::json manifest(const std::filesystem::path& root) {
    return nlohmann::json::parse(std::ifstream(root / "manifest.json"));
}
std::vector<std::byte> checkpoint_bytes(const std::filesystem::path& root,
                                        const nlohmann::json& metadata) {
    const auto path =
        root / "assets" / ("checkpoint-" + metadata["sha256"].get<std::string>() + ".fb");
    std::ifstream input(path, std::ios::binary);
    std::vector<std::byte> bytes(metadata["bytes"].get<std::size_t>());
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(input.gcount() == static_cast<std::streamsize>(bytes.size()));
    return bytes;
}
void rehash_checkpoint(const std::filesystem::path& root, std::span<const std::byte> bytes,
                       nlohmann::json metadata) {
    const auto hash = sha256(bytes);
    std::ofstream output(root / "assets" / ("checkpoint-" + hash + ".fb"), std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    output.close();
    metadata["sha256"] = hash;
    metadata["bytes"] = bytes.size();
    std::ofstream(root / "manifest.json") << metadata.dump();
}
} // namespace
TEST_CASE("persistent maps reload snapshots and operation receipts without losing correction "
          "invalidation",
          "[map][restart]") {
    directory temporary;
    const auto root = temporary.path / "map";
    auto store = open(root, true);
    const auto first = first_transaction();
    REQUIRE(store->commit(first));
    const auto original = store->acquire_snapshot();
    auto correction = map_transaction{};
    correction.operation = {101};
    correction.base_revision = {1};
    correction.new_revision = {2};
    auto pose = first.poses[0];
    pose.revision = {2};
    pose.submap_from_camera.translation = {5, -2, 8};
    correction.poses = {pose};
    REQUIRE(store->commit(correction));
    const auto corrected = store->acquire_snapshot();
    const auto hash = manifest_digest(root);
    store.reset();
    store = open(root);
    CHECK(store->acquire_snapshot()->revision.value == 2);
    CHECK(store->acquire_snapshot()->invalidated_frames == std::set<frame_id>{{1}});
    CHECK(store->acquire_snapshot()->poses.at({1}).submap_from_camera.translation ==
          vec3d{5, -2, 8});
    CHECK(store->acquire_snapshot()->calibrations.at({7}).image_transform[2] == -3.25);
    CHECK(store->acquire_snapshot()->observations.at({1}).timestamp.pts == -7);
    REQUIRE(store->commit(first));
    REQUIRE(store->commit(correction)); // old base is a recognized retry
    CHECK(manifest_digest(root) == hash);
    auto collision = first;
    collision.calibrations[0].distortion[0] = .11;
    const auto failed = store->commit(collision);
    REQUIRE(!(failed));
    CHECK(failed.error().code == error_code::DUPLICATE_OPERATION);
    CHECK(manifest_digest(root) == hash);
    CHECK(original->poses.at({1}).submap_from_camera.translation.x == 1);
    CHECK(corrected->poses.at({1}).submap_from_camera.translation.x == 5);
    map_transaction deletion;
    deletion.operation = {102};
    deletion.base_revision = {2};
    deletion.new_revision = {3};
    deletion.deleted_frames = {{1}};
    REQUIRE(store->commit(deletion));
    store.reset();
    store = open(root);
    CHECK(!(store->acquire_snapshot()->observations.contains({1})));
    CHECK(!(store->acquire_snapshot()->poses.contains({1})));
    CHECK(store->acquire_snapshot()->invalidated_frames.contains({1}));
    CHECK(corrected->observations.size() == 2);
    REQUIRE(store->commit(deletion));
}
TEST_CASE("persistent map writer ownership prevents racing processes and serializes thread commits",
          "[map][ownership]") {
    directory temporary;
    const auto root = temporary.path / "map";
    auto store = open(root, true);
    const auto second = map_store::open_persistent({.directory = root});
    REQUIRE(!(second));
    CHECK(second.error().code == error_code::WOULD_BLOCK);
    const auto tx = first_transaction();
    std::atomic<unsigned> accepted{};
    const auto producer = [&] {
        if (store->commit(tx))
            ++accepted;
    };
    {
        std::jthread first(producer), second_writer(producer);
    }
    CHECK(accepted == 2);
    CHECK(store->acquire_snapshot()->revision.value == 1);
    store.reset();
    auto reopened = open(root);
    CHECK(reopened->acquire_snapshot()->revision.value == 1);
}
TEST_CASE("persistent map cancellation and budget failure preserve the prior manifest and RAM",
          "[map][failure]") {
    directory temporary;
    const auto root = temporary.path / "map";
    const auto disk_limit = GENERATE(false, true);
    auto store =
        map_store::open_persistent({root,
                                    {},
                                    {disk_limit ? std::size_t{256} * 1024 * 1024 : 512},
                                    true,
                                    {disk_limit ? 512 : std::size_t{8} * 1024 * 1024 * 1024}});
    REQUIRE(store);
    const auto initial = (*store)->acquire_snapshot();
    const auto hash = manifest_digest(root);
    const auto tx = first_transaction();
    const auto failed = (*store)->commit(tx);
    REQUIRE(!(failed));
    CHECK(failed.error().code == error_code::RESOURCE_EXHAUSTED);
    CHECK((*store)->acquire_snapshot() == initial);
    CHECK(manifest_digest(root) == hash);
    std::stop_source stop;
    stop.request_stop();
    CHECK((*store)->commit(tx, stop.get_token()).error().code == error_code::CANCELLED);
    CHECK((*store)->reset({1}, stop.get_token()).error().code == error_code::CANCELLED);
    CHECK((*store)->acquire_snapshot() == initial);
    CHECK(manifest_digest(root) == hash);
    CHECK(!(map_store::open_persistent({.directory = root}, stop.get_token())));
    store->reset();
    auto recovered = open(root);
    CHECK(recovered->acquire_snapshot()->revision.value == 0);
    std::ofstream(root / "assets" / "checkpoint-orphan.fb") << "interrupted unreferenced asset";
    std::filesystem::create_directory(root / ".we-map-tmp-interrupted");
    recovered.reset();
    auto with_orphans = open(root);
    CHECK(with_orphans->acquire_snapshot()->revision.value == 0);
    CHECK(std::filesystem::exists(root / "assets" / "checkpoint-orphan.fb"));
}
TEST_CASE(
    "persistent map reset reloads a fresh epoch and does not silently truncate the retry window",
    "[map][epoch]") {
    directory temporary;
    const auto root = temporary.path / "map";
    auto store = open(root, true, {.remembered_operations = 2});
    const auto first = first_transaction();
    REQUIRE(store->commit(first));
    for (std::uint64_t revision = 2; revision <= 3; ++revision) {
        map_transaction tx;
        tx.operation = {100 + revision};
        tx.base_revision = {revision - 1};
        tx.new_revision = {revision};
        REQUIRE(store->commit(tx));
    }
    store.reset();
    const auto narrow =
        map_store::open_persistent({.directory = root, .records = {.remembered_operations = 1}});
    REQUIRE(!(narrow));
    CHECK(narrow.error().code == error_code::RESOURCE_EXHAUSTED);
    store = open(root, false, {.remembered_operations = 2});
    CHECK(store->commit(first).error().code ==
          error_code::WRONG_BASE); // expired receipt requires resync
    auto wrong = map_transaction{};
    wrong.operation = {1000};
    wrong.epoch = {1};
    wrong.base_revision = {3};
    wrong.new_revision = {4};
    CHECK(store->commit(wrong).error().code == error_code::WRONG_EPOCH);
    REQUIRE(store->reset({8}));
    store.reset();
    store = open(root);
    CHECK(store->acquire_snapshot()->epoch.value == 8);
    CHECK(store->acquire_snapshot()->revision.value == 0);
    CHECK(store->acquire_snapshot()->observations.empty());
    CHECK(store->commit(first).error().code == error_code::WRONG_EPOCH);
    CHECK(store->reset({8}).error().code == error_code::WRONG_EPOCH);
    map_transaction fresh;
    fresh.operation = {100};
    fresh.epoch = {8};
    fresh.new_revision = {1};
    REQUIRE(store->commit(fresh));
    store.reset();
    store = open(root);
    REQUIRE(store->commit(fresh));
}
TEST_CASE("persistent recovery rejects corrupt rehashed checkpoints instead of falling back",
          "[map][corruption]") {
    directory temporary;
    const auto root = temporary.path / "map";
    auto store = open(root, true);
    REQUIRE(store->commit(first_transaction()));
    map_transaction second;
    second.operation = {101};
    second.base_revision = {1};
    second.new_revision = {2};
    REQUIRE(store->commit(second));
    store.reset();
    auto metadata = manifest(root);
    auto bytes = checkpoint_bytes(root, metadata);
    auto* checkpoint = map_wire::GetMutablecheckpoint(bytes.data());
    const auto mode = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8);
    switch (mode) {
    case 0:
        bytes.resize(bytes.size() / 2);
        break;
    case 1:
        REQUIRE(checkpoint->mutate_schema_version(2));
        break;
    case 2:
        REQUIRE(checkpoint->mutate_revision(77));
        break;
    case 3:
        REQUIRE(checkpoint->mutable_receipts()->GetMutableObject(1)->mutate_operation(100));
        break;
    case 4:
        REQUIRE(checkpoint->mutable_receipts()->GetMutableObject(1)->mutate_revision(1));
        break;
    case 5:
        REQUIRE(checkpoint->mutable_poses()->GetMutableObject(0)->mutate_submap(999));
        break;
    case 6:
        REQUIRE(reinterpret_cast<flatbuffers::Table*>(checkpoint)
                    ->SetField<std::uint8_t>(map_wire::checkpoint::VT_SCALE, 255, 0));
        break;
    case 7:
        checkpoint->mutable_calibrations()->GetMutableObject(0)->mutable_intrinsics()->mutate_a(-1);
        break;
    case 8:
        REQUIRE(checkpoint->mutable_receipts()->GetMutableObject(1)->mutate_revision(3));
        break;
    default:
        FAIL("Missing corruption mode");
        break;
    }
    rehash_checkpoint(root, bytes, metadata);
    const auto result = map_store::open_persistent({.directory = root});
    CHECK(!(result));
    CHECK(std::filesystem::exists(root / "assets" /
                                  ("checkpoint-" + metadata["sha256"].get<std::string>() + ".fb")));
}
TEST_CASE("persistent initialization and manifest tampering preserve caller data and demand resync",
          "[map][manifest]") {
    directory temporary;
    std::ofstream(temporary.path / "caller.txt") << "caller data";
    CHECK(!(map_store::open_persistent({.directory = temporary.path, .create_if_missing = true})));
    std::ifstream caller(temporary.path / "caller.txt");
    std::string text;
    std::getline(caller, text);
    CHECK(text == "caller data");
    const auto root = temporary.path / "map";
    auto store = open(root, true);
    REQUIRE(store->commit(first_transaction()));
    const auto snapshot = store->acquire_snapshot();
    auto metadata = manifest(root);
    metadata["revision"] = 99;
    std::ofstream(root / "manifest.json") << metadata.dump();
    const auto changed = manifest_digest(root);
    map_transaction tx;
    tx.operation = {101};
    tx.base_revision = {1};
    tx.new_revision = {2};
    CHECK(store->commit(tx).error().code == error_code::RESYNC_REQUIRED);
    CHECK(store->acquire_snapshot() == snapshot);
    CHECK(manifest_digest(root) == changed);
    store.reset();
    CHECK(!(map_store::open_persistent({.directory = root})));
    std::filesystem::remove(root / "manifest.json");
    CHECK(!(map_store::open_persistent({.directory = root, .create_if_missing = true})));
    CHECK(!(std::filesystem::exists(root / "manifest.json")));
}
TEST_CASE("persistent canonical import keeps its checked source dependency after the external "
          "asset disappears",
          "[map][provenance]") {
    directory temporary;
    map_store memory;
    const auto tx = first_transaction();
    REQUIRE(memory.commit(tx));
    const auto source = temporary.path / "source.weposes";
    const auto hash = write_pose_asset(
        source, {memory.acquire_snapshot(),
                 {std::string(64, 'a'), std::string(64, 'b'), std::string(64, 'c'), {}}});
    REQUIRE(hash);
    const auto root = temporary.path / "map";
    auto store = open(root, true);
    REQUIRE(store->import_pose_asset(source, *hash, {777}));
    const auto saved = store->acquire_snapshot();
    CHECK(saved->initial_pose_asset_sha256 == *hash);
    const auto dependency = root / "assets" / ("poses-" + *hash + ".weposes");
    std::filesystem::remove(source);
    store.reset();
    store = open(root);
    CHECK(store->acquire_snapshot()->initial_pose_asset_sha256 == *hash);
    REQUIRE(store->import_pose_asset(dependency, *hash, {777}));
    const auto digest = manifest_digest(root);
    CHECK(!(store->import_pose_asset(dependency, *hash, {778})));
    CHECK(manifest_digest(root) == digest);
    store.reset();
    std::fstream changed(dependency, std::ios::binary | std::ios::in | std::ios::out);
    changed.put('!');
    changed.close();
    CHECK(!(map_store::open_persistent({.directory = root})));
    CHECK(saved->poses.size() == 2);
}

TEST_CASE("a full retained asset index rejects another checkpoint while the committed map stays "
          "recoverable",
          "[map][limits]") {
    directory temporary;
    const auto root = temporary.path / "map";
    auto store = open(root, true);
    const auto digest = manifest_digest(root);
    for (unsigned index = 0; index < 4095; ++index)
        std::ofstream(root / "assets" / ("orphan-" + std::to_string(index))) << "x";
    const auto result = store->commit(first_transaction());
    REQUIRE(!result);
    CHECK(result.error().code == error_code::RESOURCE_EXHAUSTED);
    CHECK(manifest_digest(root) == digest);
    CHECK(store->acquire_snapshot()->revision.value == 0);
    store.reset();
    store = open(root);
    CHECK(store->acquire_snapshot()->revision.value == 0);
}
