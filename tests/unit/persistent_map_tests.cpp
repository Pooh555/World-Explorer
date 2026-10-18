#include "checkpoint_generated.h"
#include "world_explorer/core/file_lock.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/map_store/map_store.hpp"
#include "world_explorer/map_store/pose_asset.hpp"

#include <atomic>
#include <bit>
#include <fstream>
#include <limits>
#include <thread>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <nlohmann/json.hpp>
using namespace world_explorer;

namespace {

struct Directory {
    std::filesystem::path m_path;

    Directory() {
        static std::atomic<unsigned> count {};
        m_path = std::filesystem::temp_directory_path() /
                 ("we_persistent_map_" +
                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                  "_" + std::to_string(count++));
        REQUIRE(std::filesystem::create_directory(m_path));
    }

    ~Directory() {
        std::error_code ignored;
        std::filesystem::remove_all(m_path, ignored);
    }
};

MapTransaction first_transaction() {
    MapTransaction tx;
    tx.m_operation = { 100 };
    tx.m_new_revision = { 1 };
    tx.m_calibrations = { { { 7 },
                          { 711, 529, 319.25, 241.75 },
                          640,
                          480,
                          CalibrationModel::OPENCV,
                          { .1, -.2, .003, -.004 },
                          { .5, .75, -3.25, 2.5 } } };
    tx.m_observations = {
        { { 1 }, { 8 }, { 7 }, { -7, 1001, 30000 }, std::chrono::nanoseconds { 123 }, std::string(64, 'a') },
        { { 2 },
         { 8 },
         { 7 },
         { 11, 1001, 30000 },
         std::chrono::nanoseconds { 367033333 },
         std::string(64, 'b') } };
    tx.m_alignments = { { { 55 }, { 1 }, { { 0, 1, 0, 0 }, { 10, -20, 30 }, 2.5 } } };
    tx.m_poses = { { { 1 },
                   { 55 },
                   { { .5, .5, .5, .5 }, { 1, 2, 3 }, 1 },
                   std::chrono::nanoseconds { 123 },
                   { 1 },
                   TrackingState::TRACKING,
                   {} },
                  { { 2 },
                   { 55 },
                   { { 0, 1, 0, 0 }, { -1, 3, -2 }, 1 },
                   std::chrono::nanoseconds { 367033333 },
                   { 1 },
                   TrackingState::TRACKING,
                   {} } };
    return tx;
}

std::unique_ptr<MapStore>
open(const std::filesystem::path& root, bool create = false, MapStoreLimits limits = {}) {
    auto store = MapStore::open_persistent({root, limits, {256uz * 1024 * 1024}, create});
    REQUIRE(store);
    return std::move(*store);
}

std::string manifest_digest(const std::filesystem::path& root) {
    const auto K_HASH = sha256_file(root / "manifest.json", 65536);
    REQUIRE(K_HASH);
    return *K_HASH;
}

nlohmann::json manifest(const std::filesystem::path& root) {
    return nlohmann::json::parse(std::ifstream(root / "manifest.json"));
}

std::vector<std::byte> checkpoint_bytes(const std::filesystem::path& root,
                                        const nlohmann::json& metadata) {
    const auto K_PATH =
        root / "assets" / ("checkpoint-" + metadata["sha256"].get<std::string>() + ".fb");
    std::ifstream input(K_PATH, std::ios::binary);
    std::vector<std::byte> bytes(metadata["bytes"].get<std::size_t>());
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(input.gcount() == static_cast<std::streamsize>(bytes.size()));
    return bytes;
}

void rehash_checkpoint(const std::filesystem::path& root,
                       std::span<const std::byte> bytes,
                       nlohmann::json metadata) {
    const auto K_HASH = sha256(bytes);
    std::ofstream output(root / "assets" / ("checkpoint-" + K_HASH + ".fb"), std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    output.close();
    metadata["sha256"] = K_HASH;
    metadata["bytes"] = bytes.size();
    std::ofstream(root / "manifest.json") << metadata.dump();
}

template <class ValueType>
void shorten_vector(flatbuffers::Vector<ValueType>* values, flatbuffers::uoffset_t count) {
    REQUIRE(values);
    REQUIRE(count <= values->size());
    flatbuffers::WriteScalar(reinterpret_cast<std::uint8_t*>(values), count);
}

void restore_manifest(const std::filesystem::path& root, const nlohmann::json& value) {
    std::ofstream output(root / "manifest.json");
    output << value.dump();
    REQUIRE(output);
}

CanonicalPoseAsset producer_asset() {
    MapStore memory;
    REQUIRE(memory.commit(first_transaction()));
    return {memory.acquire_snapshot(),
            {std::string(64, 'a'), std::string(64, 'b'), std::string(64, 'c'), {}}};
}

struct HeldWriterLock {
    std::filesystem::path m_path;
};

std::map<std::string, std::string> tree_identity(const std::filesystem::path& root,
                                                 const HeldWriterLock& writer_lock) {
    std::map<std::string, std::string> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        const auto K_NAME = entry.path().lexically_relative(root).generic_string();
        CAPTURE(K_NAME);
        const auto K_STATUS = entry.symlink_status();
        if (entry.path() == writer_lock.m_path) {
            // Windows denies another read handle while the store owns this lock.
            // Keep its regular entry in the inventory without opening its contents.
            REQUIRE(std::filesystem::is_regular_file(K_STATUS));
            files.emplace(K_NAME, "exclusive-writer-lock");
        } else if (std::filesystem::is_directory(K_STATUS)) files.emplace(K_NAME, "directory");
        else {
            REQUIRE(std::filesystem::is_regular_file(K_STATUS));
            const auto K_DIGEST = sha256_file(entry.path(), 256uz * 1024 * 1024);
            INFO((K_DIGEST ? "SHA-256 available" : K_DIGEST.error().m_message));
            REQUIRE(K_DIGEST);
            files.emplace(K_NAME, *K_DIGEST);
        }
    }
    return files;
}

MapTransaction extension_transaction() {
    auto tx = first_transaction();
    tx.m_operation = { 101 };
    tx.m_base_revision = { 1 };
    tx.m_new_revision = { 2 };
    tx.m_calibrations[0].m_id = { 8 };
    tx.m_observations.resize(1);
    tx.m_observations[0].m_id = { 3 };
    tx.m_observations[0].m_calibration = { 8 };
    tx.m_alignments[0].m_submap = { 56 };
    tx.m_alignments[0].m_revision = { 2 };
    tx.m_poses.resize(1);
    tx.m_poses[0].m_frame = { 3 };
    tx.m_poses[0].m_submap = { 56 };
    tx.m_poses[0].m_revision = { 2 };
    return tx;
}

void reject_transaction_and_retry(const MapTransaction& rejected,
                                  ErrorCode expected,
                                  const MapTransaction& corrected,
                                  MapStoreLimits limits = {}) {
    Directory temporary;
    const auto K_ROOT = temporary.m_path / "map";
    auto store = open(K_ROOT, true, limits);
    const auto K_FIRST = first_transaction();
    REQUIRE(store->commit(K_FIRST));
    const auto K_SAVED = store->acquire_snapshot();
    const CanonicalPoseAsset K_PRODUCER { K_SAVED, producer_asset().m_provenance };
    const auto K_BYTES = encode_pose_asset(K_PRODUCER);
    REQUIRE(K_BYTES);
    // This producer file is outside the store and must also survive failed admission.
    REQUIRE(write_pose_asset(temporary.m_path / "producer.weposes", K_PRODUCER));
    const auto K_FILES = tree_identity(temporary.m_path, {K_ROOT / "writer.lock"});
    const auto K_RESULT = store->commit(rejected);
    INFO((K_RESULT ? "unexpected admission" : K_RESULT.error().m_message));
    REQUIRE(!K_RESULT);
    CHECK(K_RESULT.error().m_code == expected);
    CHECK(store->acquire_snapshot() == K_SAVED);
    CHECK(tree_identity(temporary.m_path, {K_ROOT / "writer.lock"}) == K_FILES);
    const auto K_OLD_RETRY = store->commit(K_FIRST);
    REQUIRE(K_OLD_RETRY);
    CHECK(K_OLD_RETRY->m_value == 1);
    CHECK(tree_identity(temporary.m_path, {K_ROOT / "writer.lock"}) == K_FILES);
    // Failed admission must not reserve the operation ID in the live writer.
    REQUIRE(store->commit(corrected));
    CHECK(store->acquire_snapshot()->m_revision.m_value == 2);
    const auto K_UNCHANGED = encode_pose_asset(K_PRODUCER);
    REQUIRE(K_UNCHANGED);
    CHECK(*K_UNCHANGED == *K_BYTES);
    const auto K_COMMITTED = tree_identity(temporary.m_path, {K_ROOT / "writer.lock"});
    store.reset();
    store = open(K_ROOT, false, limits);
    const auto K_RETRY = store->commit(corrected);
    REQUIRE(K_RETRY);
    CHECK(K_RETRY->m_value == 2);
    REQUIRE(store->commit(K_FIRST));
    CHECK(tree_identity(temporary.m_path, {K_ROOT / "writer.lock"}) == K_COMMITTED);
}

}  // namespace

TEST_CASE("transaction revision and identity rejection preserves durable receipts",
          "[map_transaction_admission][envelope]") {
    const auto K_CORRECTED = extension_transaction();
    auto rejected = K_CORRECTED;
    auto code = ErrorCode::INVALID_ARGUMENT;
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5);
    CAPTURE(K_FIELD);
    switch (K_FIELD) {
    case 0:
        rejected.m_operation = {};
        break;
    case 1:
        rejected.m_epoch = { 1 };
        code = ErrorCode::WRONG_EPOCH;
        break;
    case 2:
        rejected.m_base_revision = {};
        code = ErrorCode::WRONG_BASE;
        break;
    case 3:
        rejected.m_new_revision = {};
        break;
    case 4:
        rejected.m_new_revision = { 1 };
        break;
    case 5:
        rejected.m_new_revision = { 3 };
        break;
    default:
        FAIL("unknown transaction envelope mutation");
    }
    reject_transaction_and_retry(rejected, code, K_CORRECTED);
    CHECK(rejected.m_operation.m_value == (K_FIELD == 0 ? 0 : 101));
}

TEST_CASE("transaction array budgets independently reject poses deletions and other records",
          "[map_transaction_admission][limits]") {
    MapStoreLimits limits { .m_max_frames = 2, .m_max_calibrations = 1, .m_max_submaps = 1 };
    MapTransaction corrected;
    corrected.m_operation = { 101 };
    corrected.m_base_revision = { 1 };
    corrected.m_new_revision = { 2 };
    auto rejected = corrected;
    const auto K_PRODUCER = first_transaction();
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4);
    CAPTURE(K_FIELD);
    switch (K_FIELD) {
    case 0:
        rejected.m_observations.assign(3, K_PRODUCER.m_observations[0]);
        break;
    case 1:
        rejected.m_poses.assign(3, K_PRODUCER.m_poses[0]);
        break;
    case 2:
        rejected.m_deleted_frames.assign(3, FrameId { 1 });
        break;
    case 3:
        rejected.m_calibrations.assign(2, K_PRODUCER.m_calibrations[0]);
        break;
    case 4:
        rejected.m_alignments.assign(2, K_PRODUCER.m_alignments[0]);
        break;
    default:
        FAIL("unknown transaction array budget");
    }
    reject_transaction_and_retry(rejected, ErrorCode::RESOURCE_EXHAUSTED, corrected, limits);
    CHECK(rejected.m_observations.size() + rejected.m_poses.size() +
              rejected.m_deleted_frames.size() + rejected.m_calibrations.size() +
              rejected.m_alignments.size() ==
          (K_FIELD < 3 ? 3 : 2));
}

TEST_CASE("transaction calibration admission rejects invalid and reused identities atomically",
          "[map_transaction_admission][calibration]") {
    const auto K_CORRECTED = extension_transaction();
    auto rejected = K_CORRECTED;
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5);
    CAPTURE(K_FIELD);
    switch (K_FIELD) {
    case 0:
        rejected.m_calibrations[0].m_id = {};
        break;
    case 1:
        rejected.m_calibrations[0].m_camera.m_fx = 0;
        break;
    case 2:
        rejected.m_calibrations[0].m_id = { 7 };
        break;
    case 3:
        rejected.m_calibrations.push_back(rejected.m_calibrations[0]);
        break;
    case 4:
        rejected.m_calibrations[0].m_distortion[3] = std::numeric_limits<double>::infinity();
        break;
    case 5:
        rejected.m_calibrations[0].m_model = std::bit_cast<CalibrationModel>(std::uint8_t { 255 });
        break;
    default:
        FAIL("unknown transaction calibration mutation");
    }
    reject_transaction_and_retry(rejected, ErrorCode::INVALID_DATA, K_CORRECTED);
    CHECK(rejected.m_calibrations.size() == (K_FIELD == 3 ? 2 : 1));
}

TEST_CASE("transaction observations require provenance identities and valid timestamps",
          "[map_transaction_admission][observations]") {
    const auto K_CORRECTED = extension_transaction();
    auto rejected = K_CORRECTED;
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12);
    CAPTURE(K_FIELD);
    switch (K_FIELD) {
    case 0:
        rejected.m_observations[0].m_image_sha256.pop_back();
        break;
    case 1:
        rejected.m_observations[0].m_image_sha256[0] = 'G';
        break;
    case 2:
        rejected.m_observations[0].m_image_sha256[63] = '\0';
        break;
    case 3:
        rejected.m_observations[0].m_id = {};
        break;
    case 4:
        rejected.m_observations[0].m_capture = {};
        break;
    case 5:
        rejected.m_observations[0].m_calibration = { 999 };
        break;
    case 6:
        rejected.m_observations[0].m_timestamp.m_time_base_numerator = 0;
        break;
    case 7:
        rejected.m_observations[0].m_timestamp.m_time_base_numerator = -1;
        break;
    case 8:
        rejected.m_observations[0].m_timestamp.m_time_base_denominator = 0;
        break;
    case 9:
        rejected.m_observations[0].m_timestamp.m_time_base_denominator = -1;
        break;
    case 10:
        rejected.m_observations[0].m_capture_time = std::chrono::nanoseconds { -1 };
        break;
    case 11:
        rejected.m_observations[0].m_id = { 1 };
        break;
    case 12:
        rejected.m_observations.push_back(rejected.m_observations[0]);
        break;
    default:
        FAIL("unknown transaction observation mutation");
    }
    reject_transaction_and_retry(rejected, ErrorCode::INVALID_DATA, K_CORRECTED);
    CHECK(rejected.m_observations[0].m_image_sha256.size() == (K_FIELD == 0 ? 63 : 64));
    if (K_FIELD == 1) CHECK(rejected.m_observations[0].m_image_sha256[0] == 'G');
    if (K_FIELD == 2) CHECK(rejected.m_observations[0].m_image_sha256[63] == '\0');
}

TEST_CASE("transaction alignments reject invalid dependencies and duplicate changes",
          "[map_transaction_admission][alignment]") {
    const auto K_CORRECTED = extension_transaction();
    auto rejected = K_CORRECTED;
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4);
    CAPTURE(K_FIELD);
    switch (K_FIELD) {
    case 0:
        rejected.m_alignments[0].m_submap = {};
        break;
    case 1:
        rejected.m_alignments[0].m_revision = { 1 };
        break;
    case 2:
        rejected.m_alignments[0].m_world_from_submap.m_scale = 0;
        break;
    case 3:
        rejected.m_alignments[0].m_world_from_submap.m_rotation = { 1, 1, 0, 0 };
        break;
    case 4:
        rejected.m_alignments.push_back(rejected.m_alignments[0]);
        break;
    default:
        FAIL("unknown transaction alignment mutation");
    }
    reject_transaction_and_retry(rejected, ErrorCode::INVALID_DATA, K_CORRECTED);
    CHECK(rejected.m_alignments.size() == (K_FIELD == 4 ? 2 : 1));
}

TEST_CASE("transaction poses reject inconsistent dependencies covariance and world overflow",
          "[map_transaction_admission][poses]") {
    auto corrected = extension_transaction();
    auto& covariance = corrected.m_poses[0].m_covariance.emplace();
    for (std::size_t axis { 0uz }; axis < 6uz; ++axis)
        covariance[axis * 6 + axis] = static_cast<double>(axis + 1);
    auto rejected = corrected;
    auto rejected_covariance = covariance;
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13);
    CAPTURE(K_FIELD);
    switch (K_FIELD) {
    case 0:
        rejected.m_poses[0].m_frame = { 999 };
        break;
    case 1:
        rejected.m_poses[0].m_submap = { 999 };
        break;
    case 2:
        rejected.m_poses[0].m_submap_from_camera.m_scale = 0;
        break;
    case 3:
        rejected.m_poses[0].m_submap_from_camera.m_scale = 2;
        break;
    case 4:
        rejected.m_poses[0].m_revision = { 1 };
        break;
    case 5:
        rejected.m_poses.push_back(rejected.m_poses[0]);
        break;
    case 6:
        rejected.m_poses[0].m_capture_time += std::chrono::nanoseconds { 1 };
        break;
    case 7:
        rejected_covariance[35] = std::numeric_limits<double>::quiet_NaN();
        break;
    case 8:
        rejected_covariance[35] = std::numeric_limits<double>::infinity();
        break;
    case 9:
        rejected_covariance[0] = -1;
        break;
    case 10:
        rejected_covariance[1] = .5;
        break;
    case 11:
        rejected_covariance[1] = 10;
        rejected_covariance[6] = 10;
        break;
    case 12:
        rejected.m_poses[0].m_tracking = std::bit_cast<TrackingState>(std::uint8_t { 255 });
        break;
    case 13:
        rejected.m_alignments[0].m_world_from_submap.m_scale = std::numeric_limits<double>::max();
        break;
    default:
        FAIL("unknown transaction pose mutation");
    }
    rejected.m_poses[0].m_covariance = rejected_covariance;
    reject_transaction_and_retry(rejected, ErrorCode::INVALID_DATA, corrected);
    if (const auto& retained = rejected.m_poses[0].m_covariance; retained) {
        if (K_FIELD == 7) CHECK(std::isnan((*retained)[35]));
        if (K_FIELD == 8) CHECK(std::isinf((*retained)[35]));
    } else FAIL("rejected producer covariance was consumed");
    CHECK(covariance[35] == 6);
}

TEST_CASE("transaction deletions cannot remove missing observations or change a frame twice",
          "[map_transaction_admission][deletion]") {
    MapTransaction corrected;
    corrected.m_operation = { 101 };
    corrected.m_base_revision = { 1 };
    corrected.m_new_revision = { 2 };
    corrected.m_deleted_frames = { { 1 } };
    auto rejected = corrected;
    const auto K_FIELD = GENERATE(0, 1, 2, 3);
    CAPTURE(K_FIELD);
    if (K_FIELD == 0) rejected.m_deleted_frames = { { 1 }, { 999 } };
    else if (K_FIELD == 1) rejected.m_deleted_frames.push_back({1});
    else if (K_FIELD == 2) {
        rejected.m_poses = { first_transaction().m_poses[0] };
        rejected.m_poses[0].m_revision = { 2 };
    } else rejected.m_deleted_frames.push_back({0});
    reject_transaction_and_retry(rejected, ErrorCode::NOT_FOUND, corrected);
    CHECK(rejected.m_deleted_frames.front() == FrameId { 1 });
}

TEST_CASE("aggregate record budgets reject small extensions while admitting exact boundaries",
          "[map_transaction_admission][limits]") {
    const MapStoreLimits K_LIMITS { .m_max_frames = 2, .m_max_calibrations = 1, .m_max_submaps = 1 };
    MapTransaction corrected;
    corrected.m_operation = { 101 };
    corrected.m_base_revision = { 1 };
    corrected.m_new_revision = { 2 };
    auto rejected = corrected;
    const auto K_EXTENSION = extension_transaction();
    const auto K_FIELD = GENERATE(0, 1, 2);
    CAPTURE(K_FIELD);
    if (K_FIELD == 0) {
        rejected.m_observations = K_EXTENSION.m_observations;
        rejected.m_observations[0].m_calibration = { 7 };
        corrected.m_observations = rejected.m_observations;
        corrected.m_deleted_frames = { { 1 } };
    } else if (K_FIELD == 1) rejected.m_calibrations = K_EXTENSION.m_calibrations;
    else rejected.m_alignments = K_EXTENSION.m_alignments;
    reject_transaction_and_retry(rejected, ErrorCode::RESOURCE_EXHAUSTED, corrected, K_LIMITS);
}

TEST_CASE(
    "alignment correction invalidates only dependent poses and each revision replaces the set",
    "[map_transaction_admission][invalidation]") {
    Directory temporary;
    const auto K_ROOT = temporary.m_path / "map";
    auto store = open(K_ROOT, true);
    const auto K_FIRST = first_transaction();
    const auto K_EXTENSION = extension_transaction();
    REQUIRE(store->commit(K_FIRST));
    REQUIRE(store->commit(K_EXTENSION));
    const auto K_BEFORE = store->acquire_snapshot();
    CHECK(K_BEFORE->m_invalidated_frames.empty());
    MapTransaction correction;
    correction.m_operation = { 102 };
    correction.m_base_revision = { 2 };
    correction.m_new_revision = { 3 };
    correction.m_alignments = { { { 55 }, { 3 }, { {}, { 1, 2, 3 }, 1 } } };
    REQUIRE(store->commit(correction));
    const auto K_CHANGED = store->acquire_snapshot();
    CHECK(K_CHANGED->m_invalidated_frames == std::set<FrameId> {{1}, {2}});
    CHECK(K_CHANGED->m_poses.at({3}).m_submap == SubmapId { 56 });
    CHECK(K_BEFORE->m_world_from_submap.at({55}).m_translation == Vec3d { 10, -20, 30 });
    MapTransaction deletion;
    deletion.m_operation = { 103 };
    deletion.m_base_revision = { 3 };
    deletion.m_new_revision = { 4 };
    deletion.m_deleted_frames = { { 3 } };
    REQUIRE(store->commit(deletion));
    CHECK(store->acquire_snapshot()->m_invalidated_frames == std::set<FrameId> {{3}});
    CHECK(!store->acquire_snapshot()->m_poses.contains({3}));
    CHECK(K_CHANGED->m_poses.contains({3}));
    const auto K_FILES = tree_identity(K_ROOT, {K_ROOT / "writer.lock"});
    store.reset();
    store = open(K_ROOT);
    REQUIRE(store->commit(correction));
    REQUIRE(store->commit(deletion));
    CHECK(store->acquire_snapshot()->m_revision.m_value == 4);
    CHECK(store->acquire_snapshot()->m_invalidated_frames == std::set<FrameId> {{3}});
    CHECK(tree_identity(K_ROOT, {K_ROOT / "writer.lock"}) == K_FILES);
}

TEST_CASE("saturated durable map revisions cannot wrap to revision zero",
          "[map_transaction_admission][revision]") {
    Directory temporary;
    const auto K_ROOT = temporary.m_path / "map";
    auto store = open(K_ROOT, true);
    REQUIRE(store->commit(first_transaction()));
    const auto K_ORIGINAL = manifest(K_ROOT);
    store.reset();
    // A rehashed synthetic checkpoint reaches the otherwise impractical public boundary.
    auto bytes = checkpoint_bytes(K_ROOT, K_ORIGINAL);
    auto* checkpoint = map_wire::GetMutablecheckpoint(bytes.data());
    constexpr auto K_MAX_REVISION { std::numeric_limits<std::uint64_t>::max() };
    REQUIRE(checkpoint->mutate_revision(K_MAX_REVISION));
    REQUIRE(checkpoint->mutable_receipts()->GetMutableObject(0)->mutate_revision(K_MAX_REVISION));
    auto saturated = K_ORIGINAL;
    saturated["revision"] = K_MAX_REVISION;
    rehash_checkpoint(K_ROOT, bytes, saturated);
    store = open(K_ROOT);
    const auto K_SAVED = store->acquire_snapshot();
    CHECK(K_SAVED->m_revision.m_value == K_MAX_REVISION);
    const auto K_FILES = tree_identity(K_ROOT, {K_ROOT / "writer.lock"});
    MapTransaction wrapped;
    wrapped.m_operation = { 101 };
    wrapped.m_base_revision = { K_MAX_REVISION };
    const auto K_REJECTED = store->commit(wrapped);
    REQUIRE(!K_REJECTED);
    CHECK(K_REJECTED.error().m_code == ErrorCode::INVALID_ARGUMENT);
    CHECK(store->acquire_snapshot() == K_SAVED);
    CHECK(tree_identity(K_ROOT, {K_ROOT / "writer.lock"}) == K_FILES);
    REQUIRE(store->reset({1}));
    wrapped.m_epoch = { 1 };
    wrapped.m_base_revision = {};
    wrapped.m_new_revision = { 1 };
    REQUIRE(store->commit(wrapped));
    store.reset();
    store = open(K_ROOT);
    REQUIRE(store->commit(wrapped));
    CHECK(store->acquire_snapshot()->m_epoch.m_value == 1);
    CHECK(store->acquire_snapshot()->m_revision.m_value == 1);
    CHECK(K_SAVED->m_revision.m_value == K_MAX_REVISION);
}

TEST_CASE("failed durable reset preserves epoch records and receipts until explicit repair",
          "[map_transaction_admission][reset]") {
    Directory temporary;
    const auto K_ROOT = temporary.m_path / "map";
    auto store = open(K_ROOT, true);
    const auto K_FIRST = first_transaction();
    REQUIRE(store->commit(K_FIRST));
    const auto K_SAVED = store->acquire_snapshot();
    const auto K_OBSTRUCTION = K_ROOT / "assets" / "caller-directory";
    REQUIRE(std::filesystem::create_directory(K_OBSTRUCTION));
    std::ofstream(K_OBSTRUCTION / "caller.txt") << "caller data";
    const auto K_FILES = tree_identity(K_ROOT, {K_ROOT / "writer.lock"});
    const auto K_REJECTED = store->reset({1});
    REQUIRE(!K_REJECTED);
    CHECK(K_REJECTED.error().m_code == ErrorCode::INVALID_DATA);
    CHECK(store->acquire_snapshot() == K_SAVED);
    REQUIRE(store->commit(K_FIRST));
    CHECK(tree_identity(K_ROOT, {K_ROOT / "writer.lock"}) == K_FILES);
    REQUIRE(std::filesystem::remove(K_OBSTRUCTION / "caller.txt"));
    REQUIRE(std::filesystem::remove(K_OBSTRUCTION));
    REQUIRE(store->reset({1}));
    store.reset();
    store = open(K_ROOT);
    CHECK(store->acquire_snapshot()->m_epoch.m_value == 1);
    CHECK(store->acquire_snapshot()->m_revision.m_value == 0);
    CHECK(store->acquire_snapshot()->m_observations.empty());
    CHECK(K_SAVED->m_observations.size() == 2);
}

TEST_CASE("map preservation observer inventories the held lock and hashes unrelated lock names",
          "[canonical_admission][observation]") {
    Directory temporary;
    const auto K_ROOT = temporary.m_path / "map";
    auto store = open(K_ROOT, true);
    const auto K_LOCK_PATH = K_ROOT / "writer.lock";
    const auto K_CALLER = temporary.m_path / "writer.lock";
    std::ofstream(K_CALLER) << "caller data";
    const auto K_OBSERVED = tree_identity(temporary.m_path, {K_LOCK_PATH});
    CHECK(K_OBSERVED.at("map/writer.lock") == "exclusive-writer-lock");
    const auto K_CALLER_HASH = sha256_file(K_CALLER, 65536);
    REQUIRE(K_CALLER_HASH);
    CHECK(K_OBSERVED.at("writer.lock") == *K_CALLER_HASH);
    const auto K_STILL_OWNED = FileLock::acquire(K_LOCK_PATH);
    REQUIRE(!K_STILL_OWNED);
    CHECK(K_STILL_OWNED.error().m_code == ErrorCode::WOULD_BLOCK);
    const auto K_BEFORE = store->acquire_snapshot();
    std::ofstream(K_CALLER) << "changed caller data";
    const auto K_CHANGED = tree_identity(temporary.m_path, {K_LOCK_PATH});
    CHECK(K_CHANGED.at("writer.lock") != K_OBSERVED.at("writer.lock"));
    CHECK(K_CHANGED.at("map/writer.lock") == K_OBSERVED.at("map/writer.lock"));
    CHECK(K_CHANGED.at("map/manifest.json") == K_OBSERVED.at("map/manifest.json"));
    CHECK(store->acquire_snapshot() == K_BEFORE);
    store.reset();
    const auto K_RELEASED = tree_identity(temporary.m_path, {});
    const auto K_LOCK_HASH = sha256_file(K_LOCK_PATH, 65536);
    REQUIRE(K_LOCK_HASH);
    CHECK(K_RELEASED.at("map/writer.lock") == *K_LOCK_HASH);
}

TEST_CASE("persistent maps reload snapshots and operation receipts without losing correction "
          "invalidation",
          "[map][restart]") {
    Directory temporary;
    const auto K_ROOT = temporary.m_path / "map";
    auto store = open(K_ROOT, true);
    const auto K_FIRST = first_transaction();
    REQUIRE(store->commit(K_FIRST));
    const auto K_ORIGINAL = store->acquire_snapshot();
    auto correction = MapTransaction {};
    correction.m_operation = { 101 };
    correction.m_base_revision = { 1 };
    correction.m_new_revision = { 2 };
    auto pose = K_FIRST.m_poses[0];
    pose.m_revision = { 2 };
    pose.m_submap_from_camera.m_translation = { 5, -2, 8 };
    correction.m_poses = { pose };
    REQUIRE(store->commit(correction));
    const auto K_CORRECTED = store->acquire_snapshot();
    const auto K_HASH = manifest_digest(K_ROOT);
    store.reset();
    store = open(K_ROOT);
    CHECK(store->acquire_snapshot()->m_revision.m_value == 2);
    CHECK(store->acquire_snapshot()->m_invalidated_frames == std::set<FrameId> {{1}});
    CHECK(store->acquire_snapshot()->m_poses.at({1}).m_submap_from_camera.m_translation ==
          Vec3d { 5, -2, 8 });
    CHECK(store->acquire_snapshot()->m_calibrations.at({7}).m_image_transform[2] == -3.25);
    CHECK(store->acquire_snapshot()->m_observations.at({1}).m_timestamp.m_pts == -7);
    REQUIRE(store->commit(K_FIRST));
    REQUIRE(store->commit(correction));  // old base is a recognized retry
    CHECK(manifest_digest(K_ROOT) == K_HASH);
    auto collision = K_FIRST;
    collision.m_calibrations[0].m_distortion[0] = .11;
    const auto K_FAILED = store->commit(collision);
    REQUIRE(!(K_FAILED));
    CHECK(K_FAILED.error().m_code == ErrorCode::DUPLICATE_OPERATION);
    CHECK(manifest_digest(K_ROOT) == K_HASH);
    CHECK(K_ORIGINAL->m_poses.at({1}).m_submap_from_camera.m_translation.m_x == 1);
    CHECK(K_CORRECTED->m_poses.at({1}).m_submap_from_camera.m_translation.m_x == 5);
    MapTransaction deletion;
    deletion.m_operation = { 102 };
    deletion.m_base_revision = { 2 };
    deletion.m_new_revision = { 3 };
    deletion.m_deleted_frames = { { 1 } };
    REQUIRE(store->commit(deletion));
    store.reset();
    store = open(K_ROOT);
    CHECK(!(store->acquire_snapshot()->m_observations.contains({1})));
    CHECK(!(store->acquire_snapshot()->m_poses.contains({1})));
    CHECK(store->acquire_snapshot()->m_invalidated_frames.contains({1}));
    CHECK(K_CORRECTED->m_observations.size() == 2);
    REQUIRE(store->commit(deletion));
}

TEST_CASE("persistent map writer ownership prevents racing processes and serializes thread commits",
          "[map][ownership]") {
    Directory temporary;
    const auto K_ROOT = temporary.m_path / "map";
    auto store = open(K_ROOT, true);
    const auto K_SECOND = MapStore::open_persistent({.m_directory = K_ROOT});
    REQUIRE(!(K_SECOND));
    CHECK(K_SECOND.error().m_code == ErrorCode::WOULD_BLOCK);
    const auto K_TX = first_transaction();
    std::atomic<unsigned> accepted {};
    const auto K_PRODUCER = [&] {
        if (store->commit(K_TX)) ++accepted;
    };
    { std::jthread first(K_PRODUCER), second_writer(K_PRODUCER); }
    CHECK(accepted == 2);
    CHECK(store->acquire_snapshot()->m_revision.m_value == 1);
    store.reset();
    auto reopened = open(K_ROOT);
    CHECK(reopened->acquire_snapshot()->m_revision.m_value == 1);
}

TEST_CASE("persistent map cancellation and budget failure preserve the prior manifest and RAM",
          "[map][failure]") {
    Directory temporary;
    const auto K_ROOT = temporary.m_path / "map";
    const auto K_DISK_LIMIT = GENERATE(false, true);
    auto store = MapStore::open_persistent({K_ROOT,
                                            {},
                                            {K_DISK_LIMIT ? 256uz * 1024 * 1024 : 512},
                                            true,
                                            {K_DISK_LIMIT ? 512 : 8uz * 1024 * 1024 * 1024}});
    REQUIRE(store);
    const auto K_INITIAL = (*store)->acquire_snapshot();
    const auto K_HASH = manifest_digest(K_ROOT);
    const auto K_TX = first_transaction();
    const auto K_FAILED = (*store)->commit(K_TX);
    REQUIRE(!(K_FAILED));
    CHECK(K_FAILED.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
    CHECK((*store)->acquire_snapshot() == K_INITIAL);
    CHECK(manifest_digest(K_ROOT) == K_HASH);
    std::stop_source stop;
    stop.request_stop();
    CHECK((*store)->commit(K_TX, stop.get_token()).error().m_code == ErrorCode::CANCELLED);
    CHECK((*store)->reset({1}, stop.get_token()).error().m_code == ErrorCode::CANCELLED);
    CHECK((*store)->acquire_snapshot() == K_INITIAL);
    CHECK(manifest_digest(K_ROOT) == K_HASH);
    CHECK(!(MapStore::open_persistent({.m_directory = K_ROOT}, stop.get_token())));
    store->reset();
    auto recovered = open(K_ROOT);
    CHECK(recovered->acquire_snapshot()->m_revision.m_value == 0);
    std::ofstream(K_ROOT / "assets" / "checkpoint-orphan.fb") << "interrupted unreferenced asset";
    std::filesystem::create_directory(K_ROOT / ".we-map-tmp-interrupted");
    recovered.reset();
    auto with_orphans = open(K_ROOT);
    CHECK(with_orphans->acquire_snapshot()->m_revision.m_value == 0);
    CHECK(std::filesystem::exists(K_ROOT / "assets" / "checkpoint-orphan.fb"));
}

TEST_CASE(
    "persistent map reset reloads a fresh epoch and does not silently truncate the retry window",
    "[map][epoch]") {
    Directory temporary;
    const auto K_ROOT = temporary.m_path / "map";
    auto store = open(K_ROOT, true, {.m_remembered_operations = 2});
    const auto K_FIRST = first_transaction();
    REQUIRE(store->commit(K_FIRST));
    for (std::uint64_t revision { 2 }; revision <= 3; ++revision) {
        MapTransaction tx;
        tx.m_operation = { 100 + revision };
        tx.m_base_revision = { revision - 1 };
        tx.m_new_revision = { revision };
        REQUIRE(store->commit(tx));
    }
    store.reset();
    const auto K_NARROW = MapStore::open_persistent(
        {.m_directory = K_ROOT, .m_records = { .m_remembered_operations = 1 }});
    REQUIRE(!(K_NARROW));
    CHECK(K_NARROW.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
    store = open(K_ROOT, false, {.m_remembered_operations = 2});
    CHECK(store->commit(K_FIRST).error().m_code ==
          ErrorCode::WRONG_BASE);  // expired receipt requires resync
    auto wrong = MapTransaction {};
    wrong.m_operation = { 1000 };
    wrong.m_epoch = { 1 };
    wrong.m_base_revision = { 3 };
    wrong.m_new_revision = { 4 };
    CHECK(store->commit(wrong).error().m_code == ErrorCode::WRONG_EPOCH);
    REQUIRE(store->reset({8}));
    store.reset();
    store = open(K_ROOT);
    CHECK(store->acquire_snapshot()->m_epoch.m_value == 8);
    CHECK(store->acquire_snapshot()->m_revision.m_value == 0);
    CHECK(store->acquire_snapshot()->m_observations.empty());
    CHECK(store->commit(K_FIRST).error().m_code == ErrorCode::WRONG_EPOCH);
    CHECK(store->reset({8}).error().m_code == ErrorCode::WRONG_EPOCH);
    MapTransaction fresh;
    fresh.m_operation = { 100 };
    fresh.m_epoch = { 8 };
    fresh.m_new_revision = { 1 };
    REQUIRE(store->commit(fresh));
    store.reset();
    store = open(K_ROOT);
    REQUIRE(store->commit(fresh));
}

TEST_CASE("persistent recovery rejects corrupt rehashed checkpoints instead of falling back",
          "[map][corruption]") {
    Directory temporary;
    const auto K_ROOT = temporary.m_path / "map";
    auto store = open(K_ROOT, true);
    REQUIRE(store->commit(first_transaction()));
    MapTransaction second;
    second.m_operation = { 101 };
    second.m_base_revision = { 1 };
    second.m_new_revision = { 2 };
    REQUIRE(store->commit(second));
    store.reset();
    auto metadata = manifest(K_ROOT);
    auto bytes = checkpoint_bytes(K_ROOT, metadata);
    auto* checkpoint = map_wire::GetMutablecheckpoint(bytes.data());
    const auto K_MODE = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8);
    switch (K_MODE) {
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
    rehash_checkpoint(K_ROOT, bytes, metadata);
    const auto K_RESULT = MapStore::open_persistent({.m_directory = K_ROOT});
    CHECK(!(K_RESULT));
    CHECK(std::filesystem::exists(K_ROOT / "assets" /
                                  ("checkpoint-" + metadata["sha256"].get<std::string>() + ".fb")));
}

TEST_CASE("persistent initialization and manifest tampering preserve caller data and demand resync",
          "[map][manifest]") {
    Directory temporary;
    std::ofstream(temporary.m_path / "caller.txt") << "caller data";
    CHECK(!(
        MapStore::open_persistent({.m_directory = temporary.m_path, .m_create_if_missing = true})));
    std::ifstream caller(temporary.m_path / "caller.txt");
    std::string text;
    std::getline(caller, text);
    CHECK(text == "caller data");
    const auto K_ROOT = temporary.m_path / "map";
    auto store = open(K_ROOT, true);
    REQUIRE(store->commit(first_transaction()));
    const auto K_SNAPSHOT = store->acquire_snapshot();
    auto metadata = manifest(K_ROOT);
    metadata["revision"] = 99;
    std::ofstream(K_ROOT / "manifest.json") << metadata.dump();
    const auto K_CHANGED = manifest_digest(K_ROOT);
    MapTransaction tx;
    tx.m_operation = { 101 };
    tx.m_base_revision = { 1 };
    tx.m_new_revision = { 2 };
    CHECK(store->commit(tx).error().m_code == ErrorCode::RESYNC_REQUIRED);
    CHECK(store->acquire_snapshot() == K_SNAPSHOT);
    CHECK(manifest_digest(K_ROOT) == K_CHANGED);
    store.reset();
    CHECK(!(MapStore::open_persistent({.m_directory = K_ROOT})));
    std::filesystem::remove(K_ROOT / "manifest.json");
    CHECK(!(MapStore::open_persistent({.m_directory = K_ROOT, .m_create_if_missing = true})));
    CHECK(!(std::filesystem::exists(K_ROOT / "manifest.json")));
}

TEST_CASE("persistent canonical import keeps its checked source dependency after the external "
          "asset disappears",
          "[map][provenance]") {
    Directory temporary;
    MapStore memory;
    const auto K_TX = first_transaction();
    REQUIRE(memory.commit(K_TX));
    const auto K_SOURCE = temporary.m_path / "source.weposes";
    const auto K_HASH =
        write_pose_asset(K_SOURCE,
                         {memory.acquire_snapshot(),
                          {std::string(64, 'a'), std::string(64, 'b'), std::string(64, 'c'), {}}});
    REQUIRE(K_HASH);
    const auto K_ROOT = temporary.m_path / "map";
    auto store = open(K_ROOT, true);
    REQUIRE(store->import_pose_asset(K_SOURCE, *K_HASH, {777}));
    const auto K_SAVED = store->acquire_snapshot();
    CHECK(K_SAVED->m_initial_pose_asset_sha256 == *K_HASH);
    const auto K_DEPENDENCY = K_ROOT / "assets" / ("poses-" + *K_HASH + ".weposes");
    std::filesystem::remove(K_SOURCE);
    store.reset();
    store = open(K_ROOT);
    CHECK(store->acquire_snapshot()->m_initial_pose_asset_sha256 == *K_HASH);
    REQUIRE(store->import_pose_asset(K_DEPENDENCY, *K_HASH, {777}));
    const auto K_DIGEST = manifest_digest(K_ROOT);
    CHECK(!(store->import_pose_asset(K_DEPENDENCY, *K_HASH, {778})));
    CHECK(manifest_digest(K_ROOT) == K_DIGEST);
    store.reset();
    std::fstream changed(K_DEPENDENCY, std::ios::binary | std::ios::in | std::ios::out);
    changed.put('!');
    changed.close();
    CHECK(!(MapStore::open_persistent({.m_directory = K_ROOT})));
    CHECK(K_SAVED->m_poses.size() == 2);
}

TEST_CASE("a full retained asset index rejects another checkpoint while the committed map stays "
          "recoverable",
          "[map][limits]") {
    Directory temporary;
    const auto K_ROOT = temporary.m_path / "map";
    auto store = open(K_ROOT, true);
    const auto K_DIGEST = manifest_digest(K_ROOT);
    for (unsigned index { 0 }; index < 4095; ++index)
        std::ofstream(K_ROOT / "assets" / ("orphan-" + std::to_string(index))) << "x";
    const auto K_RESULT = store->commit(first_transaction());
    REQUIRE(!K_RESULT);
    CHECK(K_RESULT.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
    CHECK(manifest_digest(K_ROOT) == K_DIGEST);
    CHECK(store->acquire_snapshot()->m_revision.m_value == 0);
    store.reset();
    store = open(K_ROOT);
    CHECK(store->acquire_snapshot()->m_revision.m_value == 0);
}

TEST_CASE("invalid persistent configuration fails before creating files or acquiring ownership",
          "[map_admission][configuration]") {
    Directory temporary;
    const auto K_ROOT = temporary.m_path / "must-remain-missing";
    PersistentMapConfig config { .m_directory = K_ROOT, .m_create_if_missing = true };
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13);
    CAPTURE(K_FIELD);
    switch (K_FIELD) {
    case 0:
        config.m_directory.clear();
        break;
    case 1: {
        auto spelling = K_ROOT.native();
        spelling.push_back(std::filesystem::path::value_type {});
        spelling += std::filesystem::path("suffix").native();
        config.m_directory = spelling;
        break;
    }
    case 2:
        config.m_checkpoint_bytes.m_bytes = 0;
        break;
    case 3:
        config.m_checkpoint_bytes.m_bytes = 256uz * 1024 * 1024 + 1;
        break;
    case 4:
        config.m_records.m_max_frames = 0;
        break;
    case 5:
        config.m_records.m_max_frames = 1000001;
        break;
    case 6:
        config.m_records.m_max_calibrations = 0;
        break;
    case 7:
        config.m_records.m_max_calibrations = 4097;
        break;
    case 8:
        config.m_records.m_max_submaps = 0;
        break;
    case 9:
        config.m_records.m_max_submaps = 4097;
        break;
    case 10:
        config.m_records.m_remembered_operations = 0;
        break;
    case 11:
        config.m_records.m_remembered_operations = 65537;
        break;
    case 12:
        config.m_retained_asset_bytes.m_bytes = 0;
        break;
    case 13:
        config.m_retained_asset_bytes.m_bytes = 256uz * 1024 * 1024 * 1024 + 1;
        break;
    default:
        FAIL("unknown persistent configuration mutation");
    }
    const auto K_RESULT = MapStore::open_persistent(config);
    REQUIRE(!K_RESULT);
    CHECK(K_RESULT.error().m_code == ErrorCode::INVALID_ARGUMENT);
    CHECK(!std::filesystem::exists(K_ROOT));
    CHECK(std::filesystem::is_empty(temporary.m_path));
    auto valid = open(K_ROOT, true);
    REQUIRE(valid->commit(first_transaction()));
}

TEST_CASE("manifest schema and byte admission reject tampering without resetting a committed map",
          "[map_admission][manifest]") {
    Directory temporary;
    const auto K_ROOT = temporary.m_path / "map";
    auto store = open(K_ROOT, true);
    const auto K_TX = first_transaction();
    REQUIRE(store->commit(K_TX));
    const auto K_SAVED = store->acquire_snapshot();
    const auto K_ORIGINAL = manifest(K_ROOT);
    store.reset();
    auto value = K_ORIGINAL;
    auto expected = ErrorCode::INVALID_DATA;
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    CAPTURE(K_FIELD);
    switch (K_FIELD) {
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
        expected = ErrorCode::RESOURCE_EXHAUSTED;
        break;
    case 12:
        value["bytes"] = 256uz * 1024 * 1024 + 1;
        expected = ErrorCode::RESOURCE_EXHAUSTED;
        break;
    case 13:
        value["bytes"] = K_ORIGINAL["bytes"].get<std::uint64_t>() + 1;
        break;
    case 14:
        value["sha256"] = std::string(64, '0');
        break;
    case 15:
        value["sha256"] = "nested";
        for (unsigned depth { 0 }; depth < 10; ++depth)
            value["sha256"] = nlohmann::json::array({value["sha256"]});
        break;
    default:
        FAIL("unknown manifest mutation");
    }
    restore_manifest(K_ROOT, value);
    const auto K_CHANGED_HASH = manifest_digest(K_ROOT);
    const auto K_REJECTED =
        MapStore::open_persistent({.m_directory = K_ROOT, .m_create_if_missing = true});
    REQUIRE(!K_REJECTED);
    CHECK(K_REJECTED.error().m_code == expected);
    CHECK(manifest_digest(K_ROOT) == K_CHANGED_HASH);
    CHECK(K_SAVED->m_revision.m_value == 1);
    const auto K_ORIGINAL_CHECKPOINT =
        K_ROOT / "assets" / ("checkpoint-" + K_ORIGINAL["sha256"].get<std::string>() + ".fb");
    const auto K_ORIGINAL_HASH = sha256_file(K_ORIGINAL_CHECKPOINT, 256uz * 1024 * 1024);
    REQUIRE(K_ORIGINAL_HASH);
    CHECK(*K_ORIGINAL_HASH == K_ORIGINAL["sha256"].get<std::string>());
    restore_manifest(K_ROOT, K_ORIGINAL);
    auto restored = open(K_ROOT);
    REQUIRE(restored->commit(K_TX));
    CHECK(restored->acquire_snapshot()->m_revision.m_value == 1);
}

TEST_CASE("rehashed checkpoint receipts reject gaps malformed digests and missing committed tails",
          "[map_admission][receipt]") {
    Directory temporary;
    const auto K_ROOT = temporary.m_path / "map";
    auto store = open(K_ROOT, true);
    REQUIRE(store->commit(first_transaction()));
    for (std::uint64_t revision { 2 }; revision <= 3; ++revision) {
        MapTransaction transaction;
        transaction.m_base_revision = { revision - 1 };
        transaction.m_new_revision = { revision };
        transaction.m_operation = { 100 + revision };
        REQUIRE(store->commit(transaction));
    }
    const auto K_SAVED = store->acquire_snapshot();
    const auto K_ORIGINAL = manifest(K_ROOT);
    store.reset();
    auto bytes = checkpoint_bytes(K_ROOT, K_ORIGINAL);
    auto* checkpoint = map_wire::GetMutablecheckpoint(bytes.data());
    auto* receipts = checkpoint->mutable_receipts();
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6);
    CAPTURE(K_FIELD);
    switch (K_FIELD) {
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
    rehash_checkpoint(K_ROOT, bytes, K_ORIGINAL);
    const auto K_CHANGED_HASH = manifest_digest(K_ROOT);
    const auto K_REJECTED =
        MapStore::open_persistent({.m_directory = K_ROOT, .m_create_if_missing = true});
    REQUIRE(!K_REJECTED);
    CHECK(K_REJECTED.error().m_code == ErrorCode::INVALID_DATA);
    CHECK(manifest_digest(K_ROOT) == K_CHANGED_HASH);
    CHECK(K_SAVED->m_revision.m_value == 3);
    restore_manifest(K_ROOT, K_ORIGINAL);
    auto restored = open(K_ROOT);
    MapTransaction retry;
    retry.m_base_revision = { 2 };
    retry.m_new_revision = { 3 };
    retry.m_operation = { 103 };
    REQUIRE(restored->commit(retry));
    CHECK(restored->acquire_snapshot()->m_revision.m_value == 3);
}

TEST_CASE("revision-zero checkpoints cannot retain an initial canonical dependency",
          "[map_admission][dependency]") {
    Directory temporary;
    MapStore memory;
    REQUIRE(memory.commit(first_transaction()));
    const auto K_SOURCE = temporary.m_path / "synthetic.weposes";
    const auto K_HASH =
        write_pose_asset(K_SOURCE,
                         {memory.acquire_snapshot(),
                          {std::string(64, 'a'), std::string(64, 'b'), std::string(64, 'c'), {}}});
    REQUIRE(K_HASH);
    const auto K_ROOT = temporary.m_path / "map";
    auto store = open(K_ROOT, true);
    REQUIRE(store->import_pose_asset(K_SOURCE, *K_HASH, {777}));
    const auto K_SAVED = store->acquire_snapshot();
    const auto K_ORIGINAL = manifest(K_ROOT);
    store.reset();
    auto bytes = checkpoint_bytes(K_ROOT, K_ORIGINAL);
    auto* checkpoint = map_wire::GetMutablecheckpoint(bytes.data());
    auto metadata = K_ORIGINAL;
    const auto K_FIELD = GENERATE(0, 1, 2);
    switch (K_FIELD) {
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
        std::filesystem::rename(K_ROOT / "assets" / ("poses-" + *K_HASH + ".weposes"),
                                K_ROOT / "caller-dependency.weposes");
        break;
    default:
        FAIL("unknown canonical dependency mutation");
    }
    rehash_checkpoint(K_ROOT, bytes, metadata);
    const auto K_CHANGED_HASH = manifest_digest(K_ROOT);
    const auto K_REJECTED = MapStore::open_persistent({.m_directory = K_ROOT});
    REQUIRE(!K_REJECTED);
    CHECK(K_REJECTED.error().m_code == ErrorCode::INVALID_DATA);
    CHECK(manifest_digest(K_ROOT) == K_CHANGED_HASH);
    CHECK(K_SAVED->m_initial_pose_asset_sha256 == *K_HASH);
    if (K_FIELD == 2)
        std::filesystem::rename(K_ROOT / "caller-dependency.weposes",
                                K_ROOT / "assets" / ("poses-" + *K_HASH + ".weposes"));
    restore_manifest(K_ROOT, K_ORIGINAL);
    auto restored = open(K_ROOT);
    REQUIRE(restored->import_pose_asset(K_SOURCE, *K_HASH, {777}));
    CHECK(restored->acquire_snapshot()->m_revision.m_value == 1);
}

TEST_CASE("rehashed checkpoint records reject duplicate identities and invalid pose dependencies",
          "[map_admission][records]") {
    Directory temporary;
    const auto K_ROOT = temporary.m_path / "map";
    auto store = open(K_ROOT, true);
    auto first = first_transaction();
    auto calibration = first.m_calibrations[0];
    calibration.m_id = { 8 };
    first.m_calibrations.push_back(calibration);
    first.m_alignments.push_back({{56}, {1}, {}});
    auto& covariance = first.m_poses[0].m_covariance.emplace();
    for (std::size_t axis { 0uz }; axis < 6uz; ++axis) covariance[axis * 6 + axis] = 1;
    REQUIRE(store->commit(first));
    MapTransaction second;
    second.m_base_revision = { 1 };
    second.m_new_revision = { 2 };
    second.m_operation = { 101 };
    second.m_poses = first.m_poses;
    for (auto& pose : second.m_poses) {
        pose.m_revision = { 2 };
        pose.m_submap_from_camera.m_translation.m_x += 1;
    }
    REQUIRE(store->commit(second));
    const auto K_SAVED = store->acquire_snapshot();
    const auto K_ORIGINAL = manifest(K_ROOT);
    store.reset();
    auto bytes = checkpoint_bytes(K_ROOT, K_ORIGINAL);
    auto* checkpoint = map_wire::GetMutablecheckpoint(bytes.data());
    auto* observation = checkpoint->mutable_observations()->GetMutableObject(0);
    auto* pose = checkpoint->mutable_poses()->GetMutableObject(0);
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
                                  25);
    CAPTURE(K_FIELD);
    switch (K_FIELD) {
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
            std::uint32_t { 63 });
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
        for (std::size_t index { 0uz }; index < 36uz; ++index)
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
    rehash_checkpoint(K_ROOT, bytes, K_ORIGINAL);
    const auto K_CHANGED_HASH = manifest_digest(K_ROOT);
    const auto K_REJECTED = MapStore::open_persistent({.m_directory = K_ROOT});
    REQUIRE(!K_REJECTED);
    CHECK(K_REJECTED.error().m_code == ErrorCode::INVALID_DATA);
    CHECK(manifest_digest(K_ROOT) == K_CHANGED_HASH);
    CHECK(K_SAVED->m_revision.m_value == 2);
    CHECK(K_SAVED->m_invalidated_frames.size() == 2);
    restore_manifest(K_ROOT, K_ORIGINAL);
    auto restored = open(K_ROOT);
    REQUIRE(restored->commit(second));
    CHECK(restored->acquire_snapshot()->m_revision.m_value == 2);
}

TEST_CASE("persistent opening refuses missing or occupied roots without inventing a blank map",
          "[map_admission][ownership]") {
    Directory temporary;
    const auto K_ROOT = temporary.m_path / "map";
    const auto K_MODE = GENERATE(0, 1, 2, 3, 4);
    PersistentMapConfig config { .m_directory = K_ROOT };
    auto expected = ErrorCode::NOT_FOUND;
    switch (K_MODE) {
    case 0:
        break;
    case 1:
        std::ofstream(K_ROOT) << "caller data";
        expected = ErrorCode::INVALID_DATA;
        break;
    case 2:
        REQUIRE(std::filesystem::create_directory(K_ROOT));
        break;
    case 3:
        REQUIRE(std::filesystem::create_directory(K_ROOT));
        std::ofstream(K_ROOT / "assets") << "caller assets file";
        config.m_create_if_missing = true;
        expected = ErrorCode::INVALID_DATA;
        break;
    case 4:
        config.m_create_if_missing = true;
        config.m_checkpoint_bytes.m_bytes = 1;
        expected = ErrorCode::RESOURCE_EXHAUSTED;
        break;
    default:
        FAIL("unknown root admission mode");
    }
    const auto K_RESULT = MapStore::open_persistent(config);
    REQUIRE(!K_RESULT);
    CHECK(K_RESULT.error().m_code == expected);
    if (K_MODE == 0) CHECK(!std::filesystem::exists(K_ROOT));
    else if (K_MODE == 1) {
        std::ifstream input(K_ROOT);
        std::string text;
        std::getline(input, text);
        CHECK(text == "caller data");
    } else {
        CHECK(!std::filesystem::exists(K_ROOT / "manifest.json"));
        if (K_MODE == 3) {
            std::ifstream input(K_ROOT / "assets");
            std::string text;
            std::getline(input, text);
            CHECK(text == "caller assets file");
        } else {
            auto restored = open(K_ROOT, true);
            REQUIRE(restored->commit(first_transaction()));
        }
    }
}

TEST_CASE("retained asset admission failures preserve committed RAM manifest and retry receipts",
          "[map_admission][assets]") {
    Directory temporary;
    const auto K_ROOT = temporary.m_path / "map";
    auto store = open(K_ROOT, true);
    const auto K_FIRST = first_transaction();
    REQUIRE(store->commit(K_FIRST));
    const auto K_SAVED = store->acquire_snapshot();
    const auto K_ORIGINAL_HASH = manifest_digest(K_ROOT);
    const auto K_MODE = GENERATE(0, 1, 2);
    const auto K_OBSTRUCTION = K_ROOT / "assets" / "caller-directory";
    if (K_MODE == 0) REQUIRE(std::filesystem::create_directory(K_OBSTRUCTION));
    else if (K_MODE == 1) {
        store.reset();
        const auto K_REJECTED =
            MapStore::open_persistent({.m_directory = K_ROOT, .m_retained_asset_bytes = { 1 }});
        REQUIRE(!K_REJECTED);
        CHECK(K_REJECTED.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
    } else {
        store.reset();
        std::filesystem::rename(K_ROOT / "assets", K_ROOT / "caller-assets");
        const auto K_REJECTED = MapStore::open_persistent({.m_directory = K_ROOT});
        REQUIRE(!K_REJECTED);
        CHECK(K_REJECTED.error().m_code == ErrorCode::INVALID_DATA);
    }
    if (K_MODE == 0) {
        MapTransaction next;
        next.m_base_revision = { 1 };
        next.m_new_revision = { 2 };
        next.m_operation = { 101 };
        const auto K_REJECTED = store->commit(next);
        REQUIRE(!K_REJECTED);
        CHECK(K_REJECTED.error().m_code == ErrorCode::INVALID_DATA);
        CHECK(store->acquire_snapshot() == K_SAVED);
        CHECK(manifest_digest(K_ROOT) == K_ORIGINAL_HASH);
        REQUIRE(store->commit(K_FIRST));
        std::filesystem::remove(K_OBSTRUCTION);
        REQUIRE(store->commit(next));
        CHECK(store->acquire_snapshot()->m_revision.m_value == 2);
        store.reset();
    } else {
        CHECK(manifest_digest(K_ROOT) == K_ORIGINAL_HASH);
        if (K_MODE == 2) std::filesystem::rename(K_ROOT / "caller-assets", K_ROOT / "assets");
    }
    auto restored = open(K_ROOT);
    REQUIRE(restored->commit(K_FIRST));
    CHECK(K_SAVED->m_revision.m_value == 1);
}

TEST_CASE("record budgets reject retained checkpoints without discarding records",
          "[map_admission][limits]") {
    Directory temporary;
    const auto K_ROOT = temporary.m_path / "map";
    auto store = open(K_ROOT, true);
    auto first = first_transaction();
    auto calibration = first.m_calibrations[0];
    calibration.m_id = { 8 };
    first.m_calibrations.push_back(calibration);
    first.m_alignments.push_back({{56}, {1}, {}});
    REQUIRE(store->commit(first));
    const auto K_ORIGINAL = manifest(K_ROOT);
    const auto K_ORIGINAL_HASH = manifest_digest(K_ROOT);
    store.reset();
    PersistentMapConfig config { .m_directory = K_ROOT };
    const auto K_MODE = GENERATE(0, 1, 2);
    switch (K_MODE) {
    case 0:
        config.m_records.m_max_calibrations = 1;
        break;
    case 1:
        config.m_records.m_max_frames = 1;
        break;
    case 2:
        config.m_records.m_max_submaps = 1;
        break;
    default:
        FAIL("unknown record budget");
    }
    const auto K_REJECTED = MapStore::open_persistent(config);
    REQUIRE(!K_REJECTED);
    CHECK(K_REJECTED.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
    CHECK(manifest_digest(K_ROOT) == K_ORIGINAL_HASH);
    CHECK(manifest(K_ROOT) == K_ORIGINAL);
    auto restored = open(K_ROOT);
    REQUIRE(restored->commit(first));
    CHECK(restored->acquire_snapshot()->m_observations.size() == 2);
}

TEST_CASE("pose and invalidation counts have independent recovery budgets",
          "[map_admission][limits]") {
    Directory temporary;
    const auto K_ROOT = temporary.m_path / "map";
    auto store = open(K_ROOT, true);
    REQUIRE(store->commit(first_transaction()));
    const auto K_MODE = GENERATE(0, 1);
    MapTransaction deletion;
    deletion.m_base_revision = { 1 };
    deletion.m_new_revision = { 2 };
    deletion.m_operation = { 101 };
    deletion.m_deleted_frames = { { 1 }, { 2 } };
    if (K_MODE == 1) REQUIRE(store->commit(deletion));
    const auto K_SAVED = store->acquire_snapshot();
    const auto K_ORIGINAL = manifest(K_ROOT);
    store.reset();
    if (K_MODE == 0) {
        auto bytes = checkpoint_bytes(K_ROOT, K_ORIGINAL);
        shorten_vector(map_wire::GetMutablecheckpoint(bytes.data())->mutable_observations(), 0);
        rehash_checkpoint(K_ROOT, bytes, K_ORIGINAL);
    }
    const auto K_CHANGED_HASH = manifest_digest(K_ROOT);
    const auto K_REJECTED =
        MapStore::open_persistent({.m_directory = K_ROOT, .m_records = { .m_max_frames = 1 }});
    REQUIRE(!K_REJECTED);
    CHECK(K_REJECTED.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
    CHECK(manifest_digest(K_ROOT) == K_CHANGED_HASH);
    restore_manifest(K_ROOT, K_ORIGINAL);
    auto restored = open(K_ROOT);
    if (K_MODE == 0) CHECK(restored->acquire_snapshot()->m_poses.size() == 2);
    else {
        REQUIRE(restored->commit(deletion));
        CHECK(restored->acquire_snapshot()->m_invalidated_frames.size() == 2);
        CHECK(K_SAVED->m_observations.empty());
    }
}

TEST_CASE("antipodal baked world quaternions preserve valid canonical poses on recovery",
          "[map_admission][coordinates]") {
    Directory temporary;
    const auto K_ROOT = temporary.m_path / "map";
    auto store = open(K_ROOT, true);
    const auto K_FIRST = first_transaction();
    REQUIRE(store->commit(K_FIRST));
    const auto K_SAVED = store->acquire_snapshot();
    const auto K_ORIGINAL = manifest(K_ROOT);
    store.reset();
    auto bytes = checkpoint_bytes(K_ROOT, K_ORIGINAL);
    auto* world = map_wire::GetMutablecheckpoint(bytes.data())
                      ->mutable_poses()
                      ->GetMutableObject(0)
                      ->mutable_world_from_camera();
    world->mutate_qw(-world->qw());
    world->mutate_qx(-world->qx());
    world->mutate_qy(-world->qy());
    world->mutate_qz(-world->qz());
    rehash_checkpoint(K_ROOT, bytes, K_ORIGINAL);
    auto restored = open(K_ROOT);
    REQUIRE(restored->commit(K_FIRST));
    CHECK(restored->acquire_snapshot()->m_poses.at({1}).m_submap_from_camera.m_rotation.m_w ==
          K_SAVED->m_poses.at({1}).m_submap_from_camera.m_rotation.m_w);
    CHECK(restored->acquire_snapshot()->m_scale == ScaleStatus::ARBITRARY);
}

TEST_CASE("canonical producer identity and transform failures leave the persistent map untouched",
          "[canonical_admission][producer]") {
    Directory temporary;
    const auto K_ROOT = temporary.m_path / "map";
    auto store = open(K_ROOT, true);
    const auto K_ORIGINAL = producer_asset();
    const auto K_ORIGINAL_BYTES = encode_pose_asset(K_ORIGINAL);
    REQUIRE(K_ORIGINAL_BYTES);
    auto state = std::make_shared<MapState>(*K_ORIGINAL.m_snapshot);
    auto invalid = K_ORIGINAL;
    invalid.m_snapshot = state;
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
    CAPTURE(K_FIELD);
    switch (K_FIELD) {
    case 0:
        state->m_calibrations.at({7}).m_id = { 8 };
        break;
    case 1:
        state->m_observations.at({1}).m_id = { 42 };
        break;
    case 2: {
        auto observation = state->m_observations.extract({1});
        observation.key() = { 0 };
        observation.mapped().m_id = { 0 };
        state->m_observations.insert(std::move(observation));
        break;
    }
    case 3:
        state->m_poses.at({1}).m_frame = { 42 };
        break;
    case 4:
        state->m_poses.at({1}).m_submap = { 56 };
        break;
    case 5:
        state->m_poses.at({1}).m_submap_from_camera.m_rotation = { 0, 0, 0, 0 };
        break;
    case 6:
        state->m_poses.at({1}).m_submap_from_camera.m_translation.m_x =
            std::numeric_limits<double>::infinity();
        break;
    case 7:
        state->m_world_from_submap.at({55}) = { {}, {}, std::numeric_limits<double>::max() };
        state->m_poses.at({1}).m_submap_from_camera = { {}, { 2, 0, 0 }, 1 };
        break;
    case 8:
        state->m_initial_pose_asset_sha256 = std::string(64, 'G');
        break;
    case 9:
        state->m_poses.at({1}).m_submap_from_camera.m_scale = 2;
        break;
    case 10:
        state->m_observations.erase({1});
        break;
    default:
        FAIL("unknown producer mutation");
    }
    const auto K_BEFORE = store->acquire_snapshot();
    const auto K_FILES = tree_identity(K_ROOT, {K_ROOT / "writer.lock"});
    const auto K_REJECTED = store->import_pose_asset(invalid, {777});
    REQUIRE(!K_REJECTED);
    CHECK(K_REJECTED.error().m_code == ErrorCode::INVALID_DATA);
    CHECK(invalid.m_snapshot == state);
    CHECK(state->m_calibrations.size() == 1);
    CHECK(state->m_poses.size() == 2);
    CHECK(store->acquire_snapshot() == K_BEFORE);
    CHECK(tree_identity(K_ROOT, {K_ROOT / "writer.lock"}) == K_FILES);
    const auto K_UNCHANGED = encode_pose_asset(K_ORIGINAL);
    REQUIRE(K_UNCHANGED);
    CHECK(*K_UNCHANGED == *K_ORIGINAL_BYTES);
    // A rejected operation must not reserve its ID or poison the later authentic retry.
    REQUIRE(store->import_pose_asset(K_ORIGINAL, {777}));
    const auto K_INSTALLED = tree_identity(K_ROOT, {K_ROOT / "writer.lock"});
    const auto K_COMMITTED = store->acquire_snapshot();
    const auto K_REJECTED_RETRY = store->import_pose_asset(invalid, {777});
    REQUIRE(!K_REJECTED_RETRY);
    CHECK(K_REJECTED_RETRY.error().m_code == ErrorCode::INVALID_DATA);
    CHECK(store->acquire_snapshot() == K_COMMITTED);
    CHECK(tree_identity(K_ROOT, {K_ROOT / "writer.lock"}) == K_INSTALLED);
    store.reset();
    auto restored = open(K_ROOT);
    REQUIRE(restored->import_pose_asset(K_ORIGINAL, {777}));
    CHECK(restored->acquire_snapshot()->m_revision.m_value == 1);
    CHECK(tree_identity(K_ROOT, {K_ROOT / "writer.lock"}) == K_INSTALLED);
}

TEST_CASE("canonical producer provenance and required records fail before dependency publication",
          "[canonical_admission][producer]") {
    Directory temporary;
    const auto K_ROOT = temporary.m_path / "map";
    auto store = open(K_ROOT, true);
    const auto K_ORIGINAL = producer_asset();
    auto state = std::make_shared<MapState>(*K_ORIGINAL.m_snapshot);
    auto invalid = K_ORIGINAL;
    invalid.m_snapshot = state;
    auto expected = ErrorCode::INVALID_DATA;
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9);
    CAPTURE(K_FIELD);
    switch (K_FIELD) {
    case 0:
        invalid.m_snapshot.reset();
        expected = ErrorCode::INVALID_ARGUMENT;
        break;
    case 1:
        invalid.m_provenance.m_source_sha256 = "bad";
        break;
    case 2:
        invalid.m_provenance.m_decode_completion_sha256 = std::string(64, 'G');
        break;
    case 3:
        invalid.m_provenance.m_pose_completion_sha256.clear();
        break;
    case 4:
        state->m_revision = { 0 };
        break;
    case 5:
        state->m_calibrations.clear();
        break;
    case 6:
        state->m_observations.clear();
        break;
    case 7:
        state->m_poses.clear();
        break;
    case 8:
        state->m_world_from_submap.clear();
        break;
    case 9:
        state->m_scale = ScaleStatus::METRIC_MEASURED;
        expected = ErrorCode::UNSUPPORTED_OPERATION;
        break;
    default:
        FAIL("unknown required producer field");
    }
    const auto K_BEFORE = store->acquire_snapshot();
    const auto K_FILES = tree_identity(K_ROOT, {K_ROOT / "writer.lock"});
    const auto K_REJECTED = store->import_pose_asset(invalid, {777});
    REQUIRE(!K_REJECTED);
    CHECK(K_REJECTED.error().m_code == expected);
    CHECK(store->acquire_snapshot() == K_BEFORE);
    CHECK(tree_identity(K_ROOT, {K_ROOT / "writer.lock"}) == K_FILES);
    REQUIRE(store->import_pose_asset(K_ORIGINAL, {777}));
    CHECK(store->acquire_snapshot()->m_observations.size() == 2);
}

TEST_CASE("canonical import accepts only a fresh initial solve without invalidated frames",
          "[canonical_admission][initial]") {
    Directory temporary;
    const auto K_ROOT = temporary.m_path / "map";
    auto store = open(K_ROOT, true);
    const auto K_ORIGINAL = producer_asset();
    auto state = std::make_shared<MapState>(*K_ORIGINAL.m_snapshot);
    const auto K_FIELD = GENERATE(0, 1, 2);
    if (K_FIELD == 0) state->m_epoch = { 1 };
    else if (K_FIELD == 1) state->m_revision = { 2 };
    else state->m_invalidated_frames.insert({1});
    const CanonicalPoseAsset K_ADVANCED { state, K_ORIGINAL.m_provenance };
    const auto K_ENCODED = encode_pose_asset(K_ADVANCED);
    REQUIRE(K_ENCODED);  // Valid interchange can still be unsuitable for initial seeding.
    const auto K_BEFORE = store->acquire_snapshot();
    const auto K_FILES = tree_identity(K_ROOT, {K_ROOT / "writer.lock"});
    const auto K_REJECTED = store->import_pose_asset(K_ADVANCED, {777});
    REQUIRE(!K_REJECTED);
    CHECK(K_REJECTED.error().m_code == ErrorCode::UNSUPPORTED_OPERATION);
    CHECK(store->acquire_snapshot() == K_BEFORE);
    CHECK(tree_identity(K_ROOT, {K_ROOT / "writer.lock"}) == K_FILES);
    const auto K_UNCHANGED = encode_pose_asset(K_ADVANCED);
    REQUIRE(K_UNCHANGED);
    CHECK(*K_UNCHANGED == *K_ENCODED);
    REQUIRE(store->import_pose_asset(K_ORIGINAL, {777}));
    CHECK(store->acquire_snapshot()->m_epoch.m_value == 0);
}

TEST_CASE("cancelled canonical memory and file imports preserve producer bytes and allow retry",
          "[canonical_admission][cancellation]") {
    Directory temporary;
    const auto K_ROOT = temporary.m_path / "map";
    auto store = open(K_ROOT, true);
    const auto K_ORIGINAL = producer_asset();
    const auto K_SOURCE = temporary.m_path / "synthetic.weposes";
    const auto K_HASH = write_pose_asset(K_SOURCE, K_ORIGINAL);
    REQUIRE(K_HASH);
    const auto K_BEFORE = store->acquire_snapshot();
    const auto K_FILES = tree_identity(temporary.m_path, {K_ROOT / "writer.lock"});
    std::stop_source stopped;
    stopped.request_stop();
    const auto K_FROM_FILE = GENERATE(false, true);
    const auto K_REJECTED =
        K_FROM_FILE ? store->import_pose_asset(K_SOURCE, *K_HASH, {777}, stopped.get_token())
                    : store->import_pose_asset(K_ORIGINAL, {777}, stopped.get_token());
    REQUIRE(!K_REJECTED);
    CHECK(K_REJECTED.error().m_code == ErrorCode::CANCELLED);
    CHECK(store->acquire_snapshot() == K_BEFORE);
    CHECK(tree_identity(temporary.m_path, {K_ROOT / "writer.lock"}) == K_FILES);
    REQUIRE(store->import_pose_asset(K_ORIGINAL, {777}));
    store.reset();
    auto restored = open(K_ROOT);
    REQUIRE(restored->import_pose_asset(K_SOURCE, *K_HASH, {777}));
    CHECK(restored->acquire_snapshot()->m_revision.m_value == 1);
}

TEST_CASE("canonical import record budgets retain complete orphans without acknowledging failure",
          "[canonical_admission][limits]") {
    Directory temporary;
    const auto K_ROOT = temporary.m_path / "map";
    auto original = producer_asset();
    auto state = std::make_shared<MapState>(*original.m_snapshot);
    auto calibration = state->m_calibrations.at({7});
    calibration.m_id = { 8 };
    state->m_calibrations.emplace(calibration.m_id, calibration);
    state->m_world_from_submap.emplace(SubmapId { 56 }, SimilarityTransform {});
    original.m_snapshot = state;
    PersistentMapConfig config { .m_directory = K_ROOT, .m_create_if_missing = true };
    const auto K_FIELD = GENERATE(0, 1, 2);
    if (K_FIELD == 0) config.m_records.m_max_frames = 1;
    else if (K_FIELD == 1) config.m_records.m_max_calibrations = 1;
    else config.m_records.m_max_submaps = 1;
    auto opened = MapStore::open_persistent(config);
    REQUIRE(opened);
    auto store = std::move(*opened);
    const auto K_BEFORE = store->acquire_snapshot();
    const auto K_DIGEST = manifest_digest(K_ROOT);
    const auto K_BYTES = encode_pose_asset(original);
    REQUIRE(K_BYTES);
    const auto K_HASH = sha256(*K_BYTES);
    const auto K_REJECTED = store->import_pose_asset(original, {777});
    REQUIRE(!K_REJECTED);
    CHECK(K_REJECTED.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
    CHECK(store->acquire_snapshot() == K_BEFORE);
    CHECK(manifest_digest(K_ROOT) == K_DIGEST);
    // Immutable dependencies may be installed before checkpoint admission fails.
    const auto K_ORPHAN = K_ROOT / "assets" / ("poses-" + K_HASH + ".weposes");
    REQUIRE(read_pose_asset(K_ORPHAN, K_HASH));
    store.reset();
    auto restored = open(K_ROOT);
    REQUIRE(restored->import_pose_asset(original, {777}));
    const auto K_INSTALLED = tree_identity(K_ROOT, {K_ROOT / "writer.lock"});
    restored.reset();
    restored = open(K_ROOT);
    REQUIRE(restored->import_pose_asset(original, {777}));
    CHECK(tree_identity(K_ROOT, {K_ROOT / "writer.lock"}) == K_INSTALLED);
    CHECK(restored->acquire_snapshot()->m_calibrations.size() == 2);
}

TEST_CASE(
    "canonical import refuses memory stores and missing operation IDs without consuming input",
    "[canonical_admission][operation]") {
    Directory temporary;
    const auto K_ORIGINAL = producer_asset();
    const auto K_BYTES = encode_pose_asset(K_ORIGINAL);
    REQUIRE(K_BYTES);
    MapStore memory;
    const auto K_BEFORE = memory.acquire_snapshot();
    const auto K_UNSUPPORTED = memory.import_pose_asset(K_ORIGINAL, {777});
    REQUIRE(!K_UNSUPPORTED);
    CHECK(K_UNSUPPORTED.error().m_code == ErrorCode::UNSUPPORTED_OPERATION);
    CHECK(memory.acquire_snapshot() == K_BEFORE);
    const auto K_ROOT = temporary.m_path / "map";
    auto store = open(K_ROOT, true);
    const auto K_INITIAL = store->acquire_snapshot();
    const auto K_DIGEST = manifest_digest(K_ROOT);
    const auto K_REJECTED = store->import_pose_asset(K_ORIGINAL, {0});
    REQUIRE(!K_REJECTED);
    CHECK(K_REJECTED.error().m_code == ErrorCode::INVALID_ARGUMENT);
    CHECK(store->acquire_snapshot() == K_INITIAL);
    CHECK(manifest_digest(K_ROOT) == K_DIGEST);
    const auto K_UNCHANGED = encode_pose_asset(K_ORIGINAL);
    REQUIRE(K_UNCHANGED);
    CHECK(*K_UNCHANGED == *K_BYTES);
    REQUIRE(store->import_pose_asset(K_ORIGINAL, {777}));
}

TEST_CASE("occupied canonical dependency paths preserve caller entries until explicit repair",
          "[canonical_admission][assets]") {
    Directory temporary;
    const auto K_ROOT = temporary.m_path / "map";
    auto store = open(K_ROOT, true);
    const auto K_ORIGINAL = producer_asset();
    const auto K_BYTES = encode_pose_asset(K_ORIGINAL);
    REQUIRE(K_BYTES);
    const auto K_HASH = sha256(*K_BYTES);
    const auto K_OCCUPIED = K_ROOT / "assets" / ("poses-" + K_HASH + ".weposes");
    const auto K_DIRECTORY_ENTRY = GENERATE(false, true);
    if (K_DIRECTORY_ENTRY) REQUIRE(std::filesystem::create_directory(K_OCCUPIED));
    else std::ofstream(K_OCCUPIED) << "caller data";
    const auto K_BEFORE = store->acquire_snapshot();
    const auto K_FILES = tree_identity(K_ROOT, {K_ROOT / "writer.lock"});
    const auto K_REJECTED = store->import_pose_asset(K_ORIGINAL, {777});
    REQUIRE(!K_REJECTED);
    CHECK(K_REJECTED.error().m_code == ErrorCode::INVALID_DATA);
    CHECK(store->acquire_snapshot() == K_BEFORE);
    CHECK(tree_identity(K_ROOT, {K_ROOT / "writer.lock"}) == K_FILES);
    std::filesystem::remove(K_OCCUPIED);
    REQUIRE(store->import_pose_asset(K_ORIGINAL, {777}));
    store.reset();
    auto restored = open(K_ROOT);
    REQUIRE(restored->import_pose_asset(K_ORIGINAL, {777}));
}

TEST_CASE("canonical file import rejects unavailable incomplete and rehashed malformed sources",
          "[canonical_admission][file]") {
    Directory temporary;
    const auto K_ROOT = temporary.m_path / "map";
    auto store = open(K_ROOT, true);
    const auto K_ORIGINAL = producer_asset();
    auto bytes = encode_pose_asset(K_ORIGINAL);
    REQUIRE(bytes);
    auto hash = sha256(*bytes);
    const auto K_SOURCE = temporary.m_path / "caller.weposes";
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5);
    CAPTURE(K_FIELD);
    auto expected = ErrorCode::INVALID_DATA;
    if (K_FIELD == 1) REQUIRE(std::filesystem::create_directory(K_SOURCE));
    else if (K_FIELD >= 2) {
        if (K_FIELD == 2) {
            bytes->clear();
            expected = ErrorCode::RESOURCE_EXHAUSTED;
        } else if (K_FIELD == 3) bytes->resize(bytes->size() / 2);
        else if (K_FIELD == 4) hash = std::string(64, '0');
        else {
            pose_wire::GetMutablepose_asset(bytes->data())
                ->mutable_provenance()
                ->mutable_source_sha256()
                ->Mutate(0, 'G');
            hash = sha256(*bytes);
        }
        std::ofstream output(K_SOURCE, std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes->data()),
                     static_cast<std::streamsize>(bytes->size()));
        REQUIRE(output);
    }
    const auto K_BEFORE = store->acquire_snapshot();
    const auto K_FILES = tree_identity(temporary.m_path, {K_ROOT / "writer.lock"});
    const auto K_REJECTED = store->import_pose_asset(K_SOURCE, hash, {777});
    REQUIRE(!K_REJECTED);
    CHECK(K_REJECTED.error().m_code == expected);
    CHECK(store->acquire_snapshot() == K_BEFORE);
    CHECK(tree_identity(temporary.m_path, {K_ROOT / "writer.lock"}) == K_FILES);
    REQUIRE(store->import_pose_asset(K_ORIGINAL, {777}));
    store.reset();
    auto restored = open(K_ROOT);
    REQUIRE(restored->import_pose_asset(K_ORIGINAL, {777}));
}
