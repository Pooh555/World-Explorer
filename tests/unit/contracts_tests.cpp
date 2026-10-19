#include "world_explorer/core/hash.hpp"
#include "world_explorer/map_store/map_store.hpp"
#include "world_explorer/scene_store/bundle.hpp"
#include "world_explorer/scene_store/scene_store.hpp"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <limits>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <nlohmann/json.hpp>

using namespace world_explorer;

namespace {

std::shared_ptr<SceneChunk> fixture_chunk(ChunkId id = { 1 }, std::uint64_t generation = 1) {
    auto chunk = std::make_shared<SceneChunk>();
    chunk->m_id = id;
    chunk->m_generation = generation;
    chunk->m_bounds = { { -5, -5, -5 }, { 5, 5, 5 } };
    chunk->m_pose_revision = { 1 };
    chunk->m_geometry_valid = true;
    Gaussian splat;
    splat.m_mean = { 1, 2, 3 };
    splat.m_scale = { 0.3F, 0.6F, 0.9F };
    splat.m_opacity = 0.7F;
    splat.m_sh[0] = 0.2F;
    chunk->m_splats.push_back(splat);
    return chunk;
}

SceneTransaction create_transaction() {
    return {
        {42}, {0}, {0}, {1}, {100}, {{SceneOperationKind::CREATE, {1}, fixture_chunk(), {}, {}}}};
}

struct TemporaryDirectory {
    std::filesystem::path m_path;

    TemporaryDirectory() {
        static std::atomic<std::uint64_t> count {};
        m_path = std::filesystem::temp_directory_path() /
                 ("world_explorer_contracts_" +
                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                  "_" + std::to_string(count++));
        std::filesystem::create_directories(m_path);
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(m_path, ignored);
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

std::vector<std::byte> read_bytes(const std::filesystem::path& path) {
    std::vector<std::byte> bytes(static_cast<std::size_t>(std::filesystem::file_size(path)));
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.read(reinterpret_cast<char*>(bytes.data()),
                       static_cast<std::streamsize>(bytes.size())));
    return bytes;
}

void write_bytes(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    REQUIRE(output);
}

// Schema-1 fixtures mutate fixed little-endian wire offsets independently of the reader.
void put_integer(std::span<std::byte> bytes, std::uint64_t value) {
    REQUIRE(bytes.size() <= sizeof(value));
    for (std::size_t index { 0uz }; index < bytes.size(); ++index)
        bytes[index] = static_cast<std::byte>((value >> (8U * index)) & 255U);
}

}  // namespace

TEST_CASE("asymmetric optical projection and world pose round trip", "[contracts][coordinates]") {
    PinholeCamera camera { 711, 529, 319.25, 241.75 };
    const Vec3d K_ORIGINAL { 0.71, -0.22, 3.4 };
    auto pixel = project(camera, K_ORIGINAL);
    REQUIRE(pixel);
    auto recovered = unproject(camera, *pixel, K_ORIGINAL.m_z);
    REQUIRE(recovered);
    CHECK(std::abs(recovered->m_x - K_ORIGINAL.m_x) < 1e-12);
    CHECK(std::abs(recovered->m_y - K_ORIGINAL.m_y) < 1e-12);
    const SimilarityTransform K_WORLD_FROM_CAMERA {{0, 1, 0, 0}, {2, -3, 7}, 2.5};
    const auto K_ROUND_TRIP = inverse_transform_point(
        K_WORLD_FROM_CAMERA, transform_point(K_WORLD_FROM_CAMERA, K_ORIGINAL));
    CHECK(std::abs(K_ROUND_TRIP.m_x - K_ORIGINAL.m_x) < 1e-12);
    CHECK(std::abs(K_ROUND_TRIP.m_y - K_ORIGINAL.m_y) < 1e-12);
    CHECK(std::abs(K_ROUND_TRIP.m_z - K_ORIGINAL.m_z) < 1e-12);
    CHECK_FALSE(project(camera, {0, 0, -1}));
    CHECK_FALSE(unproject({0, 1, 0, 0}, {1, 1}, 2));
}

TEST_CASE("scene transactions replay replace delete correction and epoch resync atomically",
          "[contracts][replay]") {
    SceneStore store({42});
    auto create = create_transaction();
    REQUIRE(store.commit(create));
    const auto K_OLD = store.acquire_snapshot();
    REQUIRE(store.commit(create));
    CHECK(store.acquire_snapshot()->m_revision.m_value == 1);
    auto caller_alias = std::const_pointer_cast<SceneChunk>(create.m_changes[0].m_chunk);
    caller_alias->m_splats[0].m_mean[0] = 4;
    CHECK(K_OLD->m_chunks.at({1})->m_splats[0].m_mean[0] == 1);
    CHECK_FALSE(store.commit(create));  // reused ID with modified payload
    auto replacement = fixture_chunk({1}, 2);
    replacement->m_splats[0].m_opacity = 0.4F;
    SceneTransaction replace {
        {42}, {0}, {1}, {2}, {101}, {{SceneOperationKind::REPLACE, {1}, replacement, {}, {}}}};
    REQUIRE(store.commit(replace));
    CHECK(K_OLD->m_chunks.at({1})->m_splats[0].m_opacity == 0.7F);
    auto wrong_base = replace;
    wrong_base.m_operation = { 102 };
    wrong_base.m_base_revision = { 0 };
    auto rejected = store.commit(wrong_base);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().m_code == ErrorCode::WRONG_BASE);
    auto wrong_epoch = replace;
    wrong_epoch.m_operation = { 103 };
    wrong_epoch.m_epoch = { 1 };
    rejected = store.commit(wrong_epoch);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().m_code == ErrorCode::WRONG_EPOCH);
    SceneTransaction correction {
        {42},
        {0},
        {2},
        {3},
        {104},
        {{SceneOperationKind::TRANSFORM, {1}, nullptr, {{0, 1, 0, 0}, {10, 2, 3}, 2}, {3}}}};
    REQUIRE(store.commit(correction));
    const auto K_CORRECTED = store.acquire_snapshot();
    CHECK_FALSE(K_CORRECTED->m_chunks.at({1})->m_geometry_valid);
    CHECK(K_CORRECTED->m_chunks.at({1})->m_pose_revision.m_value == 3);
    CHECK(K_CORRECTED->m_chunks.at({1})->m_world_from_chunk.m_translation.m_x == 10);
    auto replay = store.changes_since({1}, {100000});
    REQUIRE(replay);
    CHECK(replay->m_base_revision.m_value == 1);
    CHECK(replay->m_new_revision.m_value == 3);
    CHECK_FALSE(store.changes_since({1}, {1}));
    SceneTransaction deletion {
        {42}, {0}, {3}, {4}, {105}, {{SceneOperationKind::ERASE, {1}, nullptr, {}, {}}}};
    REQUIRE(store.commit(deletion));
    CHECK(store.acquire_snapshot()->m_chunks.empty());
    CHECK(K_CORRECTED->m_chunks.size() == 1);
    auto reset = std::make_shared<SceneState>(*K_OLD);
    reset->m_epoch = { 1 };
    REQUIRE(store.reset(reset));
    CHECK_FALSE(store.commit(deletion));
    CHECK(store.acquire_snapshot()->m_epoch.m_value == 1);
}

TEST_CASE("invalid transactions leave all state untouched and replay window is bounded",
          "[contracts][budgets]") {
    SceneStoreLimits limits;
    limits.m_journal_entries = 1;
    limits.m_remembered_operations = 2;
    SceneStore store({42}, limits);
    REQUIRE(store.commit(create_transaction()));
    auto invalid = fixture_chunk({2});
    invalid->m_splats[0].m_scale[0] = -1;
    SceneTransaction transaction {{42},
                                  {0},
                                  {1},
                                  {2},
                                  {102},
                                  {{SceneOperationKind::ERASE, {1}, nullptr, {}, {}},
                                   {SceneOperationKind::CREATE, {2}, invalid, {}, {}}}};
    REQUIRE_FALSE(store.commit(transaction));
    CHECK(store.acquire_snapshot()->m_chunks.contains({1}));
    transaction.m_changes.pop_back();
    REQUIRE(store.commit(transaction));
    auto missed = store.changes_since({0}, {100000});
    REQUIRE_FALSE(missed);
    CHECK(missed.error().m_code == ErrorCode::RESYNC_REQUIRED);
    CHECK_FALSE(store.changes_since({3}, {100000}));
    auto bad = fixture_chunk();
    bad->m_splats[0].m_rotation = { 0, 0, 0, 0 };
    CHECK_FALSE(validate_chunk(*bad));
    bad = fixture_chunk();
    bad->m_splats[0].m_mean[0] = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(validate_chunk(*bad));
    limits.m_scene.m_max_splats = 0;
    CHECK_FALSE(validate_chunk(*fixture_chunk(), limits.m_scene));
}

TEST_CASE("concurrent readers retain immutable scene revisions", "[contracts][threads]") {
    SceneStore store({42});
    REQUIRE(store.commit(create_transaction()));
    std::atomic<bool> valid_snapshots { true };
    std::jthread reader([&] {
        for (int i { 0 }; i < 1000; ++i) {
            const auto K_SNAPSHOT = store.acquire_snapshot();
            if (K_SNAPSHOT->m_chunks.size() != 1 || K_SNAPSHOT->m_revision.m_value == 0)
                valid_snapshots = false;
        }
    });
    for (std::uint64_t i { 2 }; i < 30; ++i) {
        SceneTransaction change {
            {42},
            {0},
            {i - 1},
            {i},
            {100 + i},
            {{SceneOperationKind::REPLACE, {1}, fixture_chunk({1}, i), {}, {}}}};
        REQUIRE(store.commit(change));
    }
    reader.join();
    CHECK(valid_snapshots.load());
}

TEST_CASE("map corrections invalidate dependencies and reject stale submissions",
          "[contracts][map]") {
    MapStore store;
    MapTransaction first;
    first.m_operation = { 1 };
    first.m_new_revision = { 1 };
    first.m_calibrations.push_back({{1}, {711, 529, 319.25, 241.75}, 640, 480});
    first.m_observations.push_back(
        {{1}, {1}, {1}, {3, 1, 30}, std::chrono::nanoseconds { 100 }, std::string(64, 'a')});
    first.m_alignments.push_back({{1}, {1}, {}});
    PoseEstimate pose;
    pose.m_frame = { 1 };
    pose.m_submap = { 1 };
    pose.m_revision = { 1 };
    pose.m_capture_time = std::chrono::nanoseconds { 100 };
    pose.m_tracking = TrackingState::TRACKING;
    first.m_poses.push_back(pose);
    REQUIRE(store.commit(first));
    REQUIRE(store.commit(first));
    const auto K_OLD = store.acquire_snapshot();
    MapTransaction correction;
    correction.m_operation = { 2 };
    correction.m_base_revision = { 1 };
    correction.m_new_revision = { 2 };
    pose.m_revision = { 2 };
    pose.m_submap_from_camera.m_translation = { 2, 3, 4 };
    correction.m_poses.push_back(pose);
    REQUIRE(store.commit(correction));
    CHECK(store.acquire_snapshot()->m_invalidated_frames.contains({1}));
    CHECK(K_OLD->m_poses.at({1}).m_submap_from_camera.m_translation.m_x == 0);
    correction.m_operation = { 3 };
    CHECK_FALSE(store.commit(correction));
    MapTransaction alignment;
    alignment.m_operation = { 4 };
    alignment.m_base_revision = { 2 };
    alignment.m_new_revision = { 3 };
    alignment.m_alignments.push_back({{1}, {3}, {{0, 1, 0, 0}, {4, 5, 6}, 2}});
    REQUIRE(store.commit(alignment));
    CHECK(store.acquire_snapshot()->m_invalidated_frames.contains({1}));
    REQUIRE(store.reset({1}));
    CHECK(store.acquire_snapshot()->m_poses.empty());
    CHECK_FALSE(store.commit(first));
}

TEST_CASE("invalid memory map retry windows reject admission without retaining an operation",
          "[map_transaction_admission][limits]") {
    const auto K_RECEIPTS = GENERATE(0uz, 65537uz);
    MapStore store({.m_remembered_operations = K_RECEIPTS});
    MapTransaction transaction;
    transaction.m_operation = { 1 };
    transaction.m_new_revision = { 1 };
    const auto K_BEFORE = store.acquire_snapshot();
    const auto K_REJECTED = store.commit(transaction);
    REQUIRE(!K_REJECTED);
    CHECK(K_REJECTED.error().m_code == ErrorCode::INVALID_ARGUMENT);
    CHECK(store.acquire_snapshot() == K_BEFORE);
    CHECK(K_BEFORE->m_revision.m_value == 0);
    CHECK(transaction.m_operation.m_value == 1);
    CHECK(transaction.m_new_revision.m_value == 1);
    // Configuration is immutable; a valid store accepts this same caller-owned input.
    MapStore repaired({.m_remembered_operations = 1});
    REQUIRE(repaired.commit(transaction));
    REQUIRE(repaired.commit(transaction));
    CHECK(repaired.acquire_snapshot()->m_revision.m_value == 1);
}

TEST_CASE("SHA-256 matches an independent standard test vector", "[contracts][hash]") {
    const std::string K_TEXT = "abc";
    CHECK(sha256(std::as_bytes(std::span(K_TEXT.data(), K_TEXT.size()))) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("SHA-256 builder preserves a digest across small updates and buffer boundaries",
          "[contracts][hash]") {
    std::vector<std::byte> bytes(65537);
    for (std::size_t index { 0uz }; index < bytes.size(); ++index)
        bytes[index] = static_cast<std::byte>(index % 251);
    const auto K_EXPECTED = sha256(bytes);
    Sha256Builder streamed;
    for (std::size_t offset { 0uz }; offset < bytes.size();) {
        const auto K_COUNT = std::min<std::size_t>(offset % 19 + 1, bytes.size() - offset);
        streamed.update(std::span(bytes).subspan(offset, K_COUNT));
        offset += K_COUNT;
    }
    CHECK(streamed.finish() == K_EXPECTED);
    CHECK_THROWS(streamed.update(bytes));
    CHECK_THROWS(streamed.finish());
    Sha256Builder direct;
    direct.update(bytes);
    CHECK(direct.finish() == K_EXPECTED);
}

TEST_CASE("bundle round trip enforces checksums paths conventions and allocation bounds",
          "[contracts][bundle]") {
    TemporaryDirectory directory;
    const auto K_BUNDLE_PATH =
        directory.m_path / GENERATE(std::filesystem::path("scene.wexplore"),
                                    std::filesystem::path(u8"room \u00e9\u623f.wexplore"));
    SceneStore store({42});
    REQUIRE(store.commit(create_transaction()));
    auto snapshot = std::make_shared<SceneState>(*store.acquire_snapshot());
    const std::string K_UTF8_LABEL = "room \xc3\xa9\xe6\x88\xbf";
    snapshot->m_provenance["label"] = K_UTF8_LABEL;
    REQUIRE(write_bundle(K_BUNDLE_PATH, snapshot));
    auto loaded = load_bundle(K_BUNDLE_PATH);
    REQUIRE(loaded);
    CHECK((*loaded)->m_chunks.at({1})->m_splats[0].m_mean[1] == 2);
    CHECK((*loaded)->m_provenance.at("label") == K_UTF8_LABEL);
    auto manifest = read_json(K_BUNDLE_PATH / "manifest.json");
    const auto K_ORIGINAL = manifest;
    manifest["index"] = "../outside.bin";
    write_json(K_BUNDLE_PATH / "manifest.json", manifest);
    CHECK_FALSE(load_bundle(K_BUNDLE_PATH));
    manifest = K_ORIGINAL;
    manifest["coordinates"]["quaternion_order"] = "xyzw";
    write_json(K_BUNDLE_PATH / "manifest.json", manifest);
    CHECK_FALSE(load_bundle(K_BUNDLE_PATH));
    for (const auto& malformed : {std::string {},
                                  std::string("{"),
                                  std::string("{\"label\":\"\xff\"}"),
                                  std::string("{}\0trailing", 11)}) {
        std::ofstream input(K_BUNDLE_PATH / "manifest.json", std::ios::binary);
        input.write(malformed.data(), static_cast<std::streamsize>(malformed.size()));
        input.close();
        CHECK_FALSE(load_bundle(K_BUNDLE_PATH));
    }
    write_json(K_BUNDLE_PATH / "manifest.json", K_ORIGINAL);
    BundleLimits limits;
    limits.m_scene.m_max_splats = 0;
    CHECK_FALSE(load_bundle(K_BUNDLE_PATH, limits));
    limits = {};
    limits.m_max_manifest_bytes = 8;
    CHECK_FALSE(load_bundle(K_BUNDLE_PATH, limits));
    const auto K_INDEX = K_BUNDLE_PATH / K_ORIGINAL.at("index").get<std::string>();
    std::ofstream corrupted(K_INDEX, std::ios::binary | std::ios::app);
    corrupted.put('x');
    corrupted.close();
    CHECK_FALSE(load_bundle(K_BUNDLE_PATH));
}

TEST_CASE("failed publication preserves previous valid bundle", "[contracts][bundle]") {
    TemporaryDirectory directory;
    SceneStore store({42});
    REQUIRE(store.commit(create_transaction()));
    REQUIRE(write_bundle(directory.m_path, store.acquire_snapshot()));
    auto invalid = std::make_shared<SceneState>(*store.acquire_snapshot());
    auto chunk = fixture_chunk();
    chunk->m_splats[0].m_scale[0] = 0;
    invalid->m_chunks[{1}] = chunk;
    REQUIRE_FALSE(write_bundle(directory.m_path, invalid));
    auto loaded = load_bundle(directory.m_path);
    REQUIRE(loaded);
    CHECK((*loaded)->m_chunks.at({1})->m_splats[0].m_scale[0] == 0.3F);
}

TEST_CASE(
    "gsplat interchange decoder converts logits scales quaternions and rejects ambiguous input",
    "[contracts][ply]") {
    TemporaryDirectory directory;
    const auto K_PATH = directory.m_path / "gsplat.ply";
    const std::string K_HEADER =
        "ply\nformat ascii 1.0\ncomment exporter gsplat-1.5.3\ncomment color_domain srgb\nelement "
        "vertex 1\nproperty float x\nproperty float y\nproperty float z\nproperty float "
        "f_dc_0\nproperty float f_dc_1\nproperty float f_dc_2\nproperty float opacity\nproperty "
        "float scale_0\nproperty float scale_1\nproperty float scale_2\nproperty float "
        "rot_0\nproperty float rot_1\nproperty float rot_2\nproperty float rot_3\nend_header\n";
    {
        std::ofstream output(K_PATH);
        output << K_HEADER << "1 2 3 0.2 0.3 0.4 0 0 0 0 2 0 0 0\n";
    }
    auto loaded = load_gsplat_ply(K_PATH, {42});
    REQUIRE(loaded);
    const auto& chunk = *(*loaded)->m_chunks.at({1});
    CHECK(chunk.m_splats[0].m_opacity == 0.5F);
    CHECK(chunk.m_splats[0].m_scale[0] == 1);
    CHECK(chunk.m_splats[0].m_rotation[0] == 1);
    const auto K_TRANSFORMED = transform_point(chunk.m_world_from_chunk, {1, 2, 3});
    CHECK(K_TRANSFORMED.m_y == -2);
    CHECK(K_TRANSFORMED.m_z == -3);
    {
        std::ofstream output(K_PATH);
        output << K_HEADER << "1 2 3 0.2 0.3 0.4 0 1000 0 0 1 0 0 0\n";
    }
    CHECK_FALSE(load_gsplat_ply(K_PATH, {42}));
    {
        std::ofstream output(K_PATH);
        output << "ply\nformat ascii 1.0\nelement vertex 1\nend_header\n";
    }
    CHECK_FALSE(load_gsplat_ply(K_PATH, {42}));
}

TEST_CASE("checksummed hostile bundle indexes never bypass package validation",
          "[validation][bundle]") {
    TemporaryDirectory directory;
    SceneStore store({42});
    REQUIRE(store.commit(create_transaction()));
    REQUIRE(write_bundle(directory.m_path, store.acquire_snapshot()));
    auto manifest = read_json(directory.m_path / "manifest.json");
    const auto K_INDEX_PATH = directory.m_path / manifest.at("index").get<std::string>();
    auto bytes = read_bytes(K_INDEX_PATH);
    BundleLimits limits;
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11);
    CAPTURE(K_FIELD);
    switch (K_FIELD) {
    case 0:
        bytes[0] = std::byte { 0 };
        break;
    case 1:
        put_integer(std::span(bytes).subspan(8, 8), 2);
        limits.m_scene.m_max_chunks = 1;
        break;
    case 2:
        put_integer(std::span(bytes).subspan(24, 8), 0);
        break;
    case 3:
        bytes[32] = std::byte { 'z' };
        break;
    case 4:
        put_integer(std::span(bytes).subspan(96, 4), std::numeric_limits<std::uint32_t>::max());
        break;
    case 5:
        bytes.push_back(std::byte { 0 });
        break;
    case 6:
        put_integer(std::span(bytes).subspan(16, 8), 2);
        break;
    case 7:
        bytes.resize(5);
        break;
    case 8: {
        const auto K_ENTRY = std::vector<std::byte>(bytes.begin() + 16, bytes.end());
        bytes.insert(bytes.end(), K_ENTRY.begin(), K_ENTRY.end());
        put_integer(std::span(bytes).subspan(8, 8), 2);
        break;
    }
    case 9:
        bytes.resize(40);
        break;
    case 10:
        bytes[100] = std::byte { 'z' };
        break;
    case 11:
        put_integer(std::span(bytes).subspan(96, 4), 0);
        bytes.resize(100);
        break;
    default:
        FAIL("unknown validation mutation");
    }
    write_bytes(K_INDEX_PATH, bytes);
    manifest["index_bytes"] = bytes.size();
    manifest["index_sha256"] = sha256(bytes);
    write_json(directory.m_path / "manifest.json", manifest);
    const auto K_REJECTED = load_bundle(directory.m_path, limits);
    REQUIRE_FALSE(K_REJECTED);
    CHECK(K_REJECTED.error().m_code == ErrorCode::INVALID_DATA);
    CHECK(store.acquire_snapshot()->m_revision.m_value == 1);
}

TEST_CASE("checksummed hostile chunk payloads reject counts enums and trailing bytes",
          "[validation][bundle]") {
    TemporaryDirectory directory;
    SceneStore store({42});
    REQUIRE(store.commit(create_transaction()));
    REQUIRE(write_bundle(directory.m_path, store.acquire_snapshot()));
    auto manifest = read_json(directory.m_path / "manifest.json");
    const auto K_INDEX_PATH = directory.m_path / manifest.at("index").get<std::string>();
    auto index = read_bytes(K_INDEX_PATH);
    const auto K_CHUNK_NAME =
        std::string(reinterpret_cast<const char*>(index.data() + 100), index.size() - 100);
    const auto K_CHUNK_PATH = directory.m_path / K_CHUNK_NAME;
    auto bytes = read_bytes(K_CHUNK_PATH);
    BundleLimits limits;
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
    CAPTURE(K_FIELD);
    switch (K_FIELD) {
    case 0:
        put_integer(std::span(bytes).subspan(32, 8), 2);
        break;
    case 1:
        put_integer(std::span(bytes).subspan(152, 8), 11);
        limits.m_scene.m_max_splats = 10;
        break;
    case 2:
        put_integer(std::span(bytes).subspan(160, 8), 11);
        limits.m_scene.m_max_source_frames = 10;
        break;
    case 3:
        put_integer(std::span(bytes).subspan(160, 8), 1000);
        break;
    case 4:
        bytes.resize(5);
        break;
    case 5:
        bytes.push_back(std::byte { 0 });
        break;
    case 6:
        put_integer(std::span(bytes).subspan(bytes.size() - 4, 4), 4);
        break;
    case 7:
        put_integer(std::span(bytes).subspan(8, 8), 0);
        break;
    default:
        FAIL("unknown validation mutation");
    }
    write_bytes(K_CHUNK_PATH, bytes);
    put_integer(std::span(index).subspan(24, 8), bytes.size());
    const auto K_CHUNK_HASH = sha256(bytes);
    for (std::size_t offset { 0uz }; offset < K_CHUNK_HASH.size(); ++offset)
        index[32 + offset] = static_cast<std::byte>(K_CHUNK_HASH[offset]);
    write_bytes(K_INDEX_PATH, index);
    manifest["index_sha256"] = sha256(index);
    write_json(directory.m_path / "manifest.json", manifest);
    const auto K_REJECTED = load_bundle(directory.m_path, limits);
    REQUIRE_FALSE(K_REJECTED);
    CHECK(K_REJECTED.error().m_code == ErrorCode::INVALID_DATA);
}

TEST_CASE("bundle manifest validation preserves scale and enforces declared identities",
          "[validation][bundle]") {
    TemporaryDirectory directory;
    SceneStore store({42});
    REQUIRE(store.commit(create_transaction()));
    auto measured = std::make_shared<SceneState>(*store.acquire_snapshot());
    measured->m_scale = ScaleStatus::METRIC_MEASURED;
    REQUIRE(write_bundle(directory.m_path, measured));
    auto round_trip = load_bundle(directory.m_path);
    REQUIRE(round_trip);
    CHECK((*round_trip)->m_scale == ScaleStatus::METRIC_MEASURED);
    auto manifest = read_json(directory.m_path / "manifest.json");
    BundleLimits limits;
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9);
    CAPTURE(K_FIELD);
    switch (K_FIELD) {
    case 0:
        manifest["schema_version"] = 3;
        break;
    case 1:
        manifest["scene_id"] = -1;
        break;
    case 2:
        manifest["coordinates"]["scale_status"] = "INFERRED";
        break;
    case 3:
        manifest["index_sha256"] = "invalid";
        break;
    case 4:
        manifest["index_bytes"] = 0;
        break;
    case 5:
        manifest["index_sha256"] = std::string(64, '0');
        break;
    case 6:
        manifest["provenance_sha256"] = "invalid";
        break;
    case 7:
        manifest["provenance_sha256"] = std::string(64, '0');
        break;
    case 8:
        limits.m_scene.m_max_chunks = std::numeric_limits<std::size_t>::max();
        break;
    case 9:
        manifest["scene_id"] = 0;
        break;
    default:
        FAIL("unknown validation mutation");
    }
    write_json(directory.m_path / "manifest.json", manifest);
    const auto K_REJECTED = load_bundle(directory.m_path, limits);
    REQUIRE_FALSE(K_REJECTED);
    CHECK(K_REJECTED.error().m_code ==
          (K_FIELD == 0 ? ErrorCode::UNSUPPORTED_OPERATION : ErrorCode::INVALID_DATA));
    measured->m_scale = static_cast<ScaleStatus>(255);
    CHECK_FALSE(write_bundle(directory.m_path, measured));
}
