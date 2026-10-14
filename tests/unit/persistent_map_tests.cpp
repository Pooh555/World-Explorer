#include "checkpoint_generated.h"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/map_store/map_store.hpp"
#include "world_explorer/map_store/pose_asset.hpp"
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <fstream>
#include <limits>
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
template <class value_type>
void shorten_vector(flatbuffers::Vector<value_type>* values, flatbuffers::uoffset_t count) {
    REQUIRE(values);
    REQUIRE(count <= values->size());
    flatbuffers::WriteScalar(reinterpret_cast<std::uint8_t*>(values), count);
}
void restore_manifest(const std::filesystem::path& root, const nlohmann::json& value) {
    std::ofstream output(root / "manifest.json");
    output << value.dump();
    REQUIRE(output);
}
canonical_pose_asset producer_asset() {
    map_store memory;
    REQUIRE(memory.commit(first_transaction()));
    return {memory.acquire_snapshot(),
            {std::string(64, 'a'), std::string(64, 'b'), std::string(64, 'c'), {}}};
}
std::map<std::string, std::string> tree_identity(const std::filesystem::path& root) {
    std::map<std::string, std::string> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        const auto name = entry.path().lexically_relative(root).generic_string();
        if (entry.is_directory())
            files.emplace(name, "directory");
        else {
            const auto digest = sha256_file(entry.path(), std::size_t{256} * 1024 * 1024);
            REQUIRE(digest);
            files.emplace(name, *digest);
        }
    }
    return files;
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

TEST_CASE("invalid persistent configuration fails before creating files or acquiring ownership",
          "[map_admission][configuration]") {
    directory temporary;
    const auto root = temporary.path / "must-remain-missing";
    persistent_map_config config{.directory = root, .create_if_missing = true};
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13);
    CAPTURE(field);
    switch (field) {
    case 0:
        config.directory.clear();
        break;
    case 1: {
        auto spelling = root.native();
        spelling.push_back(std::filesystem::path::value_type{});
        spelling += std::filesystem::path("suffix").native();
        config.directory = spelling;
        break;
    }
    case 2:
        config.checkpoint_bytes.bytes = 0;
        break;
    case 3:
        config.checkpoint_bytes.bytes = std::size_t{256} * 1024 * 1024 + 1;
        break;
    case 4:
        config.records.max_frames = 0;
        break;
    case 5:
        config.records.max_frames = 1000001;
        break;
    case 6:
        config.records.max_calibrations = 0;
        break;
    case 7:
        config.records.max_calibrations = 4097;
        break;
    case 8:
        config.records.max_submaps = 0;
        break;
    case 9:
        config.records.max_submaps = 4097;
        break;
    case 10:
        config.records.remembered_operations = 0;
        break;
    case 11:
        config.records.remembered_operations = 65537;
        break;
    case 12:
        config.retained_asset_bytes.bytes = 0;
        break;
    case 13:
        config.retained_asset_bytes.bytes = std::size_t{256} * 1024 * 1024 * 1024 + 1;
        break;
    default:
        FAIL("unknown persistent configuration mutation");
    }
    const auto result = map_store::open_persistent(config);
    REQUIRE(!result);
    CHECK(result.error().code == error_code::INVALID_ARGUMENT);
    CHECK(!std::filesystem::exists(root));
    CHECK(std::filesystem::is_empty(temporary.path));
    auto valid = open(root, true);
    REQUIRE(valid->commit(first_transaction()));
}

TEST_CASE("manifest schema and byte admission reject tampering without resetting a committed map",
          "[map_admission][manifest]") {
    directory temporary;
    const auto root = temporary.path / "map";
    auto store = open(root, true);
    const auto tx = first_transaction();
    REQUIRE(store->commit(tx));
    const auto saved = store->acquire_snapshot();
    const auto original = manifest(root);
    store.reset();
    auto value = original;
    auto expected = error_code::INVALID_DATA;
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    CAPTURE(field);
    switch (field) {
    case 0:
        value = nlohmann::json::array();
        break;
    case 1:
        value["extra"] = "unrecognized";
        break;
    case 2:
        value.erase("bytes");
        break;
    case 3:
        value["schema_version"] = "1";
        break;
    case 4:
        value["schema_version"] = 2;
        break;
    case 5:
        value["epoch"] = -1;
        break;
    case 6:
        value["revision"] = 1.5;
        break;
    case 7:
        value["sha256"] = nullptr;
        break;
    case 8:
        value["bytes"] = false;
        break;
    case 9:
        value["sha256"] = std::string(63, 'a');
        break;
    case 10:
        value["sha256"] = std::string(64, 'G');
        break;
    case 11:
        value["bytes"] = 0;
        expected = error_code::RESOURCE_EXHAUSTED;
        break;
    case 12:
        value["bytes"] = std::size_t{256} * 1024 * 1024 + 1;
        expected = error_code::RESOURCE_EXHAUSTED;
        break;
    case 13:
        value["bytes"] = original["bytes"].get<std::uint64_t>() + 1;
        break;
    case 14:
        value["sha256"] = std::string(64, '0');
        break;
    case 15:
        value["sha256"] = "nested";
        for (unsigned depth = 0; depth < 10; ++depth)
            value["sha256"] = nlohmann::json::array({value["sha256"]});
        break;
    default:
        FAIL("unknown manifest mutation");
    }
    restore_manifest(root, value);
    const auto changed_hash = manifest_digest(root);
    const auto rejected =
        map_store::open_persistent({.directory = root, .create_if_missing = true});
    REQUIRE(!rejected);
    CHECK(rejected.error().code == expected);
    CHECK(manifest_digest(root) == changed_hash);
    CHECK(saved->revision.value == 1);
    const auto original_checkpoint =
        root / "assets" / ("checkpoint-" + original["sha256"].get<std::string>() + ".fb");
    const auto original_hash = sha256_file(original_checkpoint, std::size_t{256} * 1024 * 1024);
    REQUIRE(original_hash);
    CHECK(*original_hash == original["sha256"].get<std::string>());
    restore_manifest(root, original);
    auto restored = open(root);
    REQUIRE(restored->commit(tx));
    CHECK(restored->acquire_snapshot()->revision.value == 1);
}

TEST_CASE("rehashed checkpoint receipts reject gaps malformed digests and missing committed tails",
          "[map_admission][receipt]") {
    directory temporary;
    const auto root = temporary.path / "map";
    auto store = open(root, true);
    REQUIRE(store->commit(first_transaction()));
    for (std::uint64_t revision = 2; revision <= 3; ++revision) {
        map_transaction transaction;
        transaction.base_revision = {revision - 1};
        transaction.new_revision = {revision};
        transaction.operation = {100 + revision};
        REQUIRE(store->commit(transaction));
    }
    const auto saved = store->acquire_snapshot();
    const auto original = manifest(root);
    store.reset();
    auto bytes = checkpoint_bytes(root, original);
    auto* checkpoint = map_wire::GetMutablecheckpoint(bytes.data());
    auto* receipts = checkpoint->mutable_receipts();
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6);
    CAPTURE(field);
    switch (field) {
    case 0:
        REQUIRE(receipts->GetMutableObject(0)->mutate_operation(0));
        break;
    case 1:
        receipts->GetMutableObject(0)->mutable_digest()->Mutate(0, 'G');
        break;
    case 2:
        shorten_vector(receipts, 0);
        break;
    case 3:
        shorten_vector(receipts, 2);
        break;
    case 4:
        REQUIRE(receipts->GetMutableObject(0)->mutate_revision(0));
        break;
    case 5:
        REQUIRE(receipts->GetMutableObject(1)->mutate_revision(3));
        break;
    case 6:
        REQUIRE(checkpoint->mutate_coordinates(pose_wire::coordinate_profile::UNKNOWN));
        break;
    default:
        FAIL("unknown receipt mutation");
    }
    rehash_checkpoint(root, bytes, original);
    const auto changed_hash = manifest_digest(root);
    const auto rejected =
        map_store::open_persistent({.directory = root, .create_if_missing = true});
    REQUIRE(!rejected);
    CHECK(rejected.error().code == error_code::INVALID_DATA);
    CHECK(manifest_digest(root) == changed_hash);
    CHECK(saved->revision.value == 3);
    restore_manifest(root, original);
    auto restored = open(root);
    map_transaction retry;
    retry.base_revision = {2};
    retry.new_revision = {3};
    retry.operation = {103};
    REQUIRE(restored->commit(retry));
    CHECK(restored->acquire_snapshot()->revision.value == 3);
}

TEST_CASE("revision-zero checkpoints cannot retain an initial canonical dependency",
          "[map_admission][dependency]") {
    directory temporary;
    map_store memory;
    REQUIRE(memory.commit(first_transaction()));
    const auto source = temporary.path / "synthetic.weposes";
    const auto hash = write_pose_asset(
        source, {memory.acquire_snapshot(),
                 {std::string(64, 'a'), std::string(64, 'b'), std::string(64, 'c'), {}}});
    REQUIRE(hash);
    const auto root = temporary.path / "map";
    auto store = open(root, true);
    REQUIRE(store->import_pose_asset(source, *hash, {777}));
    const auto saved = store->acquire_snapshot();
    const auto original = manifest(root);
    store.reset();
    auto bytes = checkpoint_bytes(root, original);
    auto* checkpoint = map_wire::GetMutablecheckpoint(bytes.data());
    auto metadata = original;
    const auto field = GENERATE(0, 1, 2);
    switch (field) {
    case 0:
        REQUIRE(checkpoint->mutate_revision(0));
        shorten_vector(checkpoint->mutable_calibrations(), 0);
        shorten_vector(checkpoint->mutable_observations(), 0);
        shorten_vector(checkpoint->mutable_alignments(), 0);
        shorten_vector(checkpoint->mutable_poses(), 0);
        shorten_vector(checkpoint->mutable_invalidated_frames(), 0);
        shorten_vector(checkpoint->mutable_receipts(), 0);
        metadata["revision"] = 0;
        break;
    case 1:
        checkpoint->mutable_initial_pose_asset_sha256()->Mutate(0, 'G');
        break;
    case 2:
        std::filesystem::rename(root / "assets" / ("poses-" + *hash + ".weposes"),
                                root / "caller-dependency.weposes");
        break;
    default:
        FAIL("unknown canonical dependency mutation");
    }
    rehash_checkpoint(root, bytes, metadata);
    const auto changed_hash = manifest_digest(root);
    const auto rejected = map_store::open_persistent({.directory = root});
    REQUIRE(!rejected);
    CHECK(rejected.error().code == error_code::INVALID_DATA);
    CHECK(manifest_digest(root) == changed_hash);
    CHECK(saved->initial_pose_asset_sha256 == *hash);
    if (field == 2)
        std::filesystem::rename(root / "caller-dependency.weposes",
                                root / "assets" / ("poses-" + *hash + ".weposes"));
    restore_manifest(root, original);
    auto restored = open(root);
    REQUIRE(restored->import_pose_asset(source, *hash, {777}));
    CHECK(restored->acquire_snapshot()->revision.value == 1);
}

TEST_CASE("rehashed checkpoint records reject duplicate identities and invalid pose dependencies",
          "[map_admission][records]") {
    directory temporary;
    const auto root = temporary.path / "map";
    auto store = open(root, true);
    auto first = first_transaction();
    auto calibration = first.calibrations[0];
    calibration.id = {8};
    first.calibrations.push_back(calibration);
    first.alignments.push_back({{56}, {1}, {}});
    auto& covariance = first.poses[0].covariance.emplace();
    for (std::size_t axis = 0; axis < 6; ++axis)
        covariance[axis * 6 + axis] = 1;
    REQUIRE(store->commit(first));
    map_transaction second;
    second.base_revision = {1};
    second.new_revision = {2};
    second.operation = {101};
    second.poses = first.poses;
    for (auto& pose : second.poses) {
        pose.revision = {2};
        pose.submap_from_camera.translation.x += 1;
    }
    REQUIRE(store->commit(second));
    const auto saved = store->acquire_snapshot();
    const auto original = manifest(root);
    store.reset();
    auto bytes = checkpoint_bytes(root, original);
    auto* checkpoint = map_wire::GetMutablecheckpoint(bytes.data());
    auto* observation = checkpoint->mutable_observations()->GetMutableObject(0);
    auto* pose = checkpoint->mutable_poses()->GetMutableObject(0);
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
                                19, 20, 21, 22, 23, 24, 25);
    CAPTURE(field);
    switch (field) {
    case 0:
        REQUIRE(checkpoint->mutable_calibrations()->GetMutableObject(1)->mutate_id(7));
        break;
    case 1:
        REQUIRE(checkpoint->mutable_observations()->GetMutableObject(1)->mutate_frame(1));
        break;
    case 2:
        REQUIRE(checkpoint->mutable_alignments()->GetMutableObject(1)->mutate_submap(55));
        break;
    case 3:
        REQUIRE(checkpoint->mutable_poses()->GetMutableObject(1)->mutate_frame(1));
        break;
    case 4:
        checkpoint->mutable_invalidated_frames()->Mutate(1, 1);
        break;
    case 5:
        observation->mutable_image_sha256()->Mutate(0, 'G');
        break;
    case 6:
        observation->mutable_image_sha256()->Mutate(63, '\0');
        flatbuffers::WriteScalar(
            reinterpret_cast<std::uint8_t*>(observation->mutable_image_sha256()),
            std::uint32_t{63});
        break;
    case 7:
        REQUIRE(observation->mutate_capture(0));
        break;
    case 8:
        REQUIRE(observation->mutate_time_base_numerator(0));
        break;
    case 9:
        REQUIRE(observation->mutate_time_base_denominator(0));
        break;
    case 10:
        REQUIRE(observation->mutate_capture_nanoseconds(-1));
        break;
    case 11:
        REQUIRE(observation->mutate_calibration_id(999));
        break;
    case 12:
        REQUIRE(pose->mutate_capture_nanoseconds(42));
        break;
    case 13:
        REQUIRE(pose->mutate_revision(0));
        break;
    case 14:
        REQUIRE(pose->mutate_revision(3));
        break;
    case 15:
        pose->mutable_world_from_camera()->mutate_tx(1234);
        break;
    case 16:
        pose->mutable_world_from_camera()->mutate_qw(0);
        pose->mutable_world_from_camera()->mutate_qx(1);
        pose->mutable_world_from_camera()->mutate_qy(0);
        pose->mutable_world_from_camera()->mutate_qz(0);
        break;
    case 17:
        pose->mutable_submap_from_camera()->mutate_scale(2);
        pose->mutable_world_from_camera()->mutate_scale(5);
        break;
    case 18:
        REQUIRE(checkpoint->mutable_alignments()->GetMutableObject(1)->mutate_submap(0));
        break;
    case 19:
        checkpoint->mutable_alignments()
            ->GetMutableObject(1)
            ->mutable_world_from_submap()
            ->mutate_scale(0);
        break;
    case 20:
        REQUIRE(checkpoint->mutable_calibrations()->GetMutableObject(1)->mutate_id(0));
        break;
    case 21:
        shorten_vector(pose->mutable_covariance(), 35);
        break;
    case 22:
        pose->mutable_covariance()->Mutate(0, -1);
        break;
    case 23:
        for (std::size_t index = 0; index < 36; ++index)
            pose->mutable_covariance()->Mutate(static_cast<flatbuffers::uoffset_t>(index), 0);
        pose->mutable_covariance()->Mutate(1, 1);
        pose->mutable_covariance()->Mutate(6, 1);
        break;
    case 24:
        checkpoint->mutable_invalidated_frames()->Mutate(0, 0);
        break;
    case 25:
        REQUIRE(pose->mutate_frame(42));
        break;
    default:
        FAIL("unknown checkpoint record mutation");
    }
    rehash_checkpoint(root, bytes, original);
    const auto changed_hash = manifest_digest(root);
    const auto rejected = map_store::open_persistent({.directory = root});
    REQUIRE(!rejected);
    CHECK(rejected.error().code == error_code::INVALID_DATA);
    CHECK(manifest_digest(root) == changed_hash);
    CHECK(saved->revision.value == 2);
    CHECK(saved->invalidated_frames.size() == 2);
    restore_manifest(root, original);
    auto restored = open(root);
    REQUIRE(restored->commit(second));
    CHECK(restored->acquire_snapshot()->revision.value == 2);
}

TEST_CASE("persistent opening refuses missing or occupied roots without inventing a blank map",
          "[map_admission][ownership]") {
    directory temporary;
    const auto root = temporary.path / "map";
    const auto mode = GENERATE(0, 1, 2, 3, 4);
    persistent_map_config config{.directory = root};
    auto expected = error_code::NOT_FOUND;
    switch (mode) {
    case 0:
        break;
    case 1:
        std::ofstream(root) << "caller data";
        expected = error_code::INVALID_DATA;
        break;
    case 2:
        REQUIRE(std::filesystem::create_directory(root));
        break;
    case 3:
        REQUIRE(std::filesystem::create_directory(root));
        std::ofstream(root / "assets") << "caller assets file";
        config.create_if_missing = true;
        expected = error_code::INVALID_DATA;
        break;
    case 4:
        config.create_if_missing = true;
        config.checkpoint_bytes.bytes = 1;
        expected = error_code::RESOURCE_EXHAUSTED;
        break;
    default:
        FAIL("unknown root admission mode");
    }
    const auto result = map_store::open_persistent(config);
    REQUIRE(!result);
    CHECK(result.error().code == expected);
    if (mode == 0)
        CHECK(!std::filesystem::exists(root));
    else if (mode == 1) {
        std::ifstream input(root);
        std::string text;
        std::getline(input, text);
        CHECK(text == "caller data");
    } else {
        CHECK(!std::filesystem::exists(root / "manifest.json"));
        if (mode == 3) {
            std::ifstream input(root / "assets");
            std::string text;
            std::getline(input, text);
            CHECK(text == "caller assets file");
        } else {
            auto restored = open(root, true);
            REQUIRE(restored->commit(first_transaction()));
        }
    }
}

TEST_CASE("retained asset admission failures preserve committed RAM manifest and retry receipts",
          "[map_admission][assets]") {
    directory temporary;
    const auto root = temporary.path / "map";
    auto store = open(root, true);
    const auto first = first_transaction();
    REQUIRE(store->commit(first));
    const auto saved = store->acquire_snapshot();
    const auto original_hash = manifest_digest(root);
    const auto mode = GENERATE(0, 1, 2);
    const auto obstruction = root / "assets" / "caller-directory";
    if (mode == 0)
        REQUIRE(std::filesystem::create_directory(obstruction));
    else if (mode == 1) {
        store.reset();
        const auto rejected =
            map_store::open_persistent({.directory = root, .retained_asset_bytes = {1}});
        REQUIRE(!rejected);
        CHECK(rejected.error().code == error_code::RESOURCE_EXHAUSTED);
    } else {
        store.reset();
        std::filesystem::rename(root / "assets", root / "caller-assets");
        const auto rejected = map_store::open_persistent({.directory = root});
        REQUIRE(!rejected);
        CHECK(rejected.error().code == error_code::INVALID_DATA);
    }
    if (mode == 0) {
        map_transaction next;
        next.base_revision = {1};
        next.new_revision = {2};
        next.operation = {101};
        const auto rejected = store->commit(next);
        REQUIRE(!rejected);
        CHECK(rejected.error().code == error_code::INVALID_DATA);
        CHECK(store->acquire_snapshot() == saved);
        CHECK(manifest_digest(root) == original_hash);
        REQUIRE(store->commit(first));
        std::filesystem::remove(obstruction);
        REQUIRE(store->commit(next));
        CHECK(store->acquire_snapshot()->revision.value == 2);
        store.reset();
    } else {
        CHECK(manifest_digest(root) == original_hash);
        if (mode == 2)
            std::filesystem::rename(root / "caller-assets", root / "assets");
    }
    auto restored = open(root);
    REQUIRE(restored->commit(first));
    CHECK(saved->revision.value == 1);
}

TEST_CASE("record budgets reject retained checkpoints without discarding records",
          "[map_admission][limits]") {
    directory temporary;
    const auto root = temporary.path / "map";
    auto store = open(root, true);
    auto first = first_transaction();
    auto calibration = first.calibrations[0];
    calibration.id = {8};
    first.calibrations.push_back(calibration);
    first.alignments.push_back({{56}, {1}, {}});
    REQUIRE(store->commit(first));
    const auto original = manifest(root);
    const auto original_hash = manifest_digest(root);
    store.reset();
    persistent_map_config config{.directory = root};
    const auto mode = GENERATE(0, 1, 2);
    switch (mode) {
    case 0:
        config.records.max_calibrations = 1;
        break;
    case 1:
        config.records.max_frames = 1;
        break;
    case 2:
        config.records.max_submaps = 1;
        break;
    default:
        FAIL("unknown record budget");
    }
    const auto rejected = map_store::open_persistent(config);
    REQUIRE(!rejected);
    CHECK(rejected.error().code == error_code::RESOURCE_EXHAUSTED);
    CHECK(manifest_digest(root) == original_hash);
    CHECK(manifest(root) == original);
    auto restored = open(root);
    REQUIRE(restored->commit(first));
    CHECK(restored->acquire_snapshot()->observations.size() == 2);
}

TEST_CASE("pose and invalidation counts have independent recovery budgets",
          "[map_admission][limits]") {
    directory temporary;
    const auto root = temporary.path / "map";
    auto store = open(root, true);
    REQUIRE(store->commit(first_transaction()));
    const auto mode = GENERATE(0, 1);
    map_transaction deletion;
    deletion.base_revision = {1};
    deletion.new_revision = {2};
    deletion.operation = {101};
    deletion.deleted_frames = {{1}, {2}};
    if (mode == 1)
        REQUIRE(store->commit(deletion));
    const auto saved = store->acquire_snapshot();
    const auto original = manifest(root);
    store.reset();
    if (mode == 0) {
        auto bytes = checkpoint_bytes(root, original);
        shorten_vector(map_wire::GetMutablecheckpoint(bytes.data())->mutable_observations(), 0);
        rehash_checkpoint(root, bytes, original);
    }
    const auto changed_hash = manifest_digest(root);
    const auto rejected =
        map_store::open_persistent({.directory = root, .records = {.max_frames = 1}});
    REQUIRE(!rejected);
    CHECK(rejected.error().code == error_code::RESOURCE_EXHAUSTED);
    CHECK(manifest_digest(root) == changed_hash);
    restore_manifest(root, original);
    auto restored = open(root);
    if (mode == 0)
        CHECK(restored->acquire_snapshot()->poses.size() == 2);
    else {
        REQUIRE(restored->commit(deletion));
        CHECK(restored->acquire_snapshot()->invalidated_frames.size() == 2);
        CHECK(saved->observations.empty());
    }
}

TEST_CASE("antipodal baked world quaternions preserve valid canonical poses on recovery",
          "[map_admission][coordinates]") {
    directory temporary;
    const auto root = temporary.path / "map";
    auto store = open(root, true);
    const auto first = first_transaction();
    REQUIRE(store->commit(first));
    const auto saved = store->acquire_snapshot();
    const auto original = manifest(root);
    store.reset();
    auto bytes = checkpoint_bytes(root, original);
    auto* world = map_wire::GetMutablecheckpoint(bytes.data())
                      ->mutable_poses()
                      ->GetMutableObject(0)
                      ->mutable_world_from_camera();
    world->mutate_qw(-world->qw());
    world->mutate_qx(-world->qx());
    world->mutate_qy(-world->qy());
    world->mutate_qz(-world->qz());
    rehash_checkpoint(root, bytes, original);
    auto restored = open(root);
    REQUIRE(restored->commit(first));
    CHECK(restored->acquire_snapshot()->poses.at({1}).submap_from_camera.rotation.w ==
          saved->poses.at({1}).submap_from_camera.rotation.w);
    CHECK(restored->acquire_snapshot()->scale == scale_status::ARBITRARY);
}

TEST_CASE("canonical producer identity and transform failures leave the persistent map untouched",
          "[canonical_admission][producer]") {
    directory temporary;
    const auto root = temporary.path / "map";
    auto store = open(root, true);
    const auto original = producer_asset();
    const auto original_bytes = encode_pose_asset(original);
    REQUIRE(original_bytes);
    auto state = std::make_shared<map_state>(*original.snapshot);
    auto invalid = original;
    invalid.snapshot = state;
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
    CAPTURE(field);
    switch (field) {
    case 0:
        state->calibrations.at({7}).id = {8};
        break;
    case 1:
        state->observations.at({1}).id = {42};
        break;
    case 2: {
        auto observation = state->observations.extract({1});
        observation.key() = {0};
        observation.mapped().id = {0};
        state->observations.insert(std::move(observation));
        break;
    }
    case 3:
        state->poses.at({1}).frame = {42};
        break;
    case 4:
        state->poses.at({1}).submap = {56};
        break;
    case 5:
        state->poses.at({1}).submap_from_camera.rotation = {0, 0, 0, 0};
        break;
    case 6:
        state->poses.at({1}).submap_from_camera.translation.x =
            std::numeric_limits<double>::infinity();
        break;
    case 7:
        state->world_from_submap.at({55}) = {{}, {}, std::numeric_limits<double>::max()};
        state->poses.at({1}).submap_from_camera = {{}, {2, 0, 0}, 1};
        break;
    case 8:
        state->initial_pose_asset_sha256 = std::string(64, 'G');
        break;
    case 9:
        state->poses.at({1}).submap_from_camera.scale = 2;
        break;
    case 10:
        state->observations.erase({1});
        break;
    default:
        FAIL("unknown producer mutation");
    }
    const auto before = store->acquire_snapshot();
    const auto files = tree_identity(root);
    const auto rejected = store->import_pose_asset(invalid, {777});
    REQUIRE(!rejected);
    CHECK(rejected.error().code == error_code::INVALID_DATA);
    CHECK(invalid.snapshot == state);
    CHECK(state->calibrations.size() == 1);
    CHECK(state->poses.size() == 2);
    CHECK(store->acquire_snapshot() == before);
    CHECK(tree_identity(root) == files);
    const auto unchanged = encode_pose_asset(original);
    REQUIRE(unchanged);
    CHECK(*unchanged == *original_bytes);
    // A rejected operation must not reserve its ID or poison the later authentic retry.
    REQUIRE(store->import_pose_asset(original, {777}));
    const auto installed = tree_identity(root);
    const auto committed = store->acquire_snapshot();
    const auto rejected_retry = store->import_pose_asset(invalid, {777});
    REQUIRE(!rejected_retry);
    CHECK(rejected_retry.error().code == error_code::INVALID_DATA);
    CHECK(store->acquire_snapshot() == committed);
    CHECK(tree_identity(root) == installed);
    store.reset();
    auto restored = open(root);
    REQUIRE(restored->import_pose_asset(original, {777}));
    CHECK(restored->acquire_snapshot()->revision.value == 1);
    CHECK(tree_identity(root) == installed);
}

TEST_CASE("canonical producer provenance and required records fail before dependency publication",
          "[canonical_admission][producer]") {
    directory temporary;
    const auto root = temporary.path / "map";
    auto store = open(root, true);
    const auto original = producer_asset();
    auto state = std::make_shared<map_state>(*original.snapshot);
    auto invalid = original;
    invalid.snapshot = state;
    auto expected = error_code::INVALID_DATA;
    const auto field = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9);
    CAPTURE(field);
    switch (field) {
    case 0:
        invalid.snapshot.reset();
        expected = error_code::INVALID_ARGUMENT;
        break;
    case 1:
        invalid.provenance.source_sha256 = "bad";
        break;
    case 2:
        invalid.provenance.decode_completion_sha256 = std::string(64, 'G');
        break;
    case 3:
        invalid.provenance.pose_completion_sha256.clear();
        break;
    case 4:
        state->revision = {0};
        break;
    case 5:
        state->calibrations.clear();
        break;
    case 6:
        state->observations.clear();
        break;
    case 7:
        state->poses.clear();
        break;
    case 8:
        state->world_from_submap.clear();
        break;
    case 9:
        state->scale = scale_status::METRIC_MEASURED;
        expected = error_code::UNSUPPORTED_OPERATION;
        break;
    default:
        FAIL("unknown required producer field");
    }
    const auto before = store->acquire_snapshot();
    const auto files = tree_identity(root);
    const auto rejected = store->import_pose_asset(invalid, {777});
    REQUIRE(!rejected);
    CHECK(rejected.error().code == expected);
    CHECK(store->acquire_snapshot() == before);
    CHECK(tree_identity(root) == files);
    REQUIRE(store->import_pose_asset(original, {777}));
    CHECK(store->acquire_snapshot()->observations.size() == 2);
}

TEST_CASE("canonical import accepts only a fresh initial solve without invalidated frames",
          "[canonical_admission][initial]") {
    directory temporary;
    const auto root = temporary.path / "map";
    auto store = open(root, true);
    const auto original = producer_asset();
    auto state = std::make_shared<map_state>(*original.snapshot);
    const auto field = GENERATE(0, 1, 2);
    if (field == 0)
        state->epoch = {1};
    else if (field == 1)
        state->revision = {2};
    else
        state->invalidated_frames.insert({1});
    const canonical_pose_asset advanced{state, original.provenance};
    const auto encoded = encode_pose_asset(advanced);
    REQUIRE(encoded); // Valid interchange can still be unsuitable for initial seeding.
    const auto before = store->acquire_snapshot();
    const auto files = tree_identity(root);
    const auto rejected = store->import_pose_asset(advanced, {777});
    REQUIRE(!rejected);
    CHECK(rejected.error().code == error_code::UNSUPPORTED_OPERATION);
    CHECK(store->acquire_snapshot() == before);
    CHECK(tree_identity(root) == files);
    const auto unchanged = encode_pose_asset(advanced);
    REQUIRE(unchanged);
    CHECK(*unchanged == *encoded);
    REQUIRE(store->import_pose_asset(original, {777}));
    CHECK(store->acquire_snapshot()->epoch.value == 0);
}

TEST_CASE("cancelled canonical memory and file imports preserve producer bytes and allow retry",
          "[canonical_admission][cancellation]") {
    directory temporary;
    const auto root = temporary.path / "map";
    auto store = open(root, true);
    const auto original = producer_asset();
    const auto source = temporary.path / "synthetic.weposes";
    const auto hash = write_pose_asset(source, original);
    REQUIRE(hash);
    const auto before = store->acquire_snapshot();
    const auto files = tree_identity(temporary.path);
    std::stop_source stopped;
    stopped.request_stop();
    const auto from_file = GENERATE(false, true);
    const auto rejected = from_file
                              ? store->import_pose_asset(source, *hash, {777}, stopped.get_token())
                              : store->import_pose_asset(original, {777}, stopped.get_token());
    REQUIRE(!rejected);
    CHECK(rejected.error().code == error_code::CANCELLED);
    CHECK(store->acquire_snapshot() == before);
    CHECK(tree_identity(temporary.path) == files);
    REQUIRE(store->import_pose_asset(original, {777}));
    store.reset();
    auto restored = open(root);
    REQUIRE(restored->import_pose_asset(source, *hash, {777}));
    CHECK(restored->acquire_snapshot()->revision.value == 1);
}

TEST_CASE("canonical import record budgets retain complete orphans without acknowledging failure",
          "[canonical_admission][limits]") {
    directory temporary;
    const auto root = temporary.path / "map";
    auto original = producer_asset();
    auto state = std::make_shared<map_state>(*original.snapshot);
    auto calibration = state->calibrations.at({7});
    calibration.id = {8};
    state->calibrations.emplace(calibration.id, calibration);
    state->world_from_submap.emplace(submap_id{56}, similarity_transform{});
    original.snapshot = state;
    persistent_map_config config{.directory = root, .create_if_missing = true};
    const auto field = GENERATE(0, 1, 2);
    if (field == 0)
        config.records.max_frames = 1;
    else if (field == 1)
        config.records.max_calibrations = 1;
    else
        config.records.max_submaps = 1;
    auto opened = map_store::open_persistent(config);
    REQUIRE(opened);
    auto store = std::move(*opened);
    const auto before = store->acquire_snapshot();
    const auto digest = manifest_digest(root);
    const auto bytes = encode_pose_asset(original);
    REQUIRE(bytes);
    const auto hash = sha256(*bytes);
    const auto rejected = store->import_pose_asset(original, {777});
    REQUIRE(!rejected);
    CHECK(rejected.error().code == error_code::RESOURCE_EXHAUSTED);
    CHECK(store->acquire_snapshot() == before);
    CHECK(manifest_digest(root) == digest);
    // Immutable dependencies may be installed before checkpoint admission fails.
    const auto orphan = root / "assets" / ("poses-" + hash + ".weposes");
    REQUIRE(read_pose_asset(orphan, hash));
    store.reset();
    auto restored = open(root);
    REQUIRE(restored->import_pose_asset(original, {777}));
    const auto installed = tree_identity(root);
    restored.reset();
    restored = open(root);
    REQUIRE(restored->import_pose_asset(original, {777}));
    CHECK(tree_identity(root) == installed);
    CHECK(restored->acquire_snapshot()->calibrations.size() == 2);
}

TEST_CASE(
    "canonical import refuses memory stores and missing operation IDs without consuming input",
    "[canonical_admission][operation]") {
    directory temporary;
    const auto original = producer_asset();
    const auto bytes = encode_pose_asset(original);
    REQUIRE(bytes);
    map_store memory;
    const auto before = memory.acquire_snapshot();
    const auto unsupported = memory.import_pose_asset(original, {777});
    REQUIRE(!unsupported);
    CHECK(unsupported.error().code == error_code::UNSUPPORTED_OPERATION);
    CHECK(memory.acquire_snapshot() == before);
    const auto root = temporary.path / "map";
    auto store = open(root, true);
    const auto initial = store->acquire_snapshot();
    const auto digest = manifest_digest(root);
    const auto rejected = store->import_pose_asset(original, {0});
    REQUIRE(!rejected);
    CHECK(rejected.error().code == error_code::INVALID_ARGUMENT);
    CHECK(store->acquire_snapshot() == initial);
    CHECK(manifest_digest(root) == digest);
    const auto unchanged = encode_pose_asset(original);
    REQUIRE(unchanged);
    CHECK(*unchanged == *bytes);
    REQUIRE(store->import_pose_asset(original, {777}));
}

TEST_CASE("occupied canonical dependency paths preserve caller entries until explicit repair",
          "[canonical_admission][assets]") {
    directory temporary;
    const auto root = temporary.path / "map";
    auto store = open(root, true);
    const auto original = producer_asset();
    const auto bytes = encode_pose_asset(original);
    REQUIRE(bytes);
    const auto hash = sha256(*bytes);
    const auto occupied = root / "assets" / ("poses-" + hash + ".weposes");
    const auto directory_entry = GENERATE(false, true);
    if (directory_entry)
        REQUIRE(std::filesystem::create_directory(occupied));
    else
        std::ofstream(occupied) << "caller data";
    const auto before = store->acquire_snapshot();
    const auto files = tree_identity(root);
    const auto rejected = store->import_pose_asset(original, {777});
    REQUIRE(!rejected);
    CHECK(rejected.error().code == error_code::INVALID_DATA);
    CHECK(store->acquire_snapshot() == before);
    CHECK(tree_identity(root) == files);
    std::filesystem::remove(occupied);
    REQUIRE(store->import_pose_asset(original, {777}));
    store.reset();
    auto restored = open(root);
    REQUIRE(restored->import_pose_asset(original, {777}));
}

TEST_CASE("canonical file import rejects unavailable incomplete and rehashed malformed sources",
          "[canonical_admission][file]") {
    directory temporary;
    const auto root = temporary.path / "map";
    auto store = open(root, true);
    const auto original = producer_asset();
    auto bytes = encode_pose_asset(original);
    REQUIRE(bytes);
    auto hash = sha256(*bytes);
    const auto source = temporary.path / "caller.weposes";
    const auto field = GENERATE(0, 1, 2, 3, 4, 5);
    CAPTURE(field);
    auto expected = error_code::INVALID_DATA;
    if (field == 1)
        REQUIRE(std::filesystem::create_directory(source));
    else if (field >= 2) {
        if (field == 2) {
            bytes->clear();
            expected = error_code::RESOURCE_EXHAUSTED;
        } else if (field == 3)
            bytes->resize(bytes->size() / 2);
        else if (field == 4)
            hash = std::string(64, '0');
        else {
            pose_wire::GetMutablepose_asset(bytes->data())
                ->mutable_provenance()
                ->mutable_source_sha256()
                ->Mutate(0, 'G');
            hash = sha256(*bytes);
        }
        std::ofstream output(source, std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes->data()),
                     static_cast<std::streamsize>(bytes->size()));
        REQUIRE(output);
    }
    const auto before = store->acquire_snapshot();
    const auto files = tree_identity(temporary.path);
    const auto rejected = store->import_pose_asset(source, hash, {777});
    REQUIRE(!rejected);
    CHECK(rejected.error().code == expected);
    CHECK(store->acquire_snapshot() == before);
    CHECK(tree_identity(temporary.path) == files);
    REQUIRE(store->import_pose_asset(original, {777}));
    store.reset();
    auto restored = open(root);
    REQUIRE(restored->import_pose_asset(original, {777}));
}
