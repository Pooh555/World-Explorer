#include "poses_generated.h"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/map_store/pose_asset.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <nlohmann/json.hpp>

#include <atomic>
#include <fstream>
#include <limits>
#include <thread>

using namespace world_explorer;

namespace {

CanonicalPoseAsset fixture() {
    MapStore store;
    MapTransaction transaction;
    transaction.m_new_revision = { 1 };
    transaction.m_operation = { 99 };
    transaction.m_calibrations = { { { 7 },
                                   { 711, 529, 319.25, 241.75 },
                                   640,
                                   480,
                                   CalibrationModel::OPENCV,
                                   { .1, -.2, .003, -.004 },
                                   { .5, .75, -3.25, 2.5 } },
                                  { { 8 }, { 801, 603, 400.125, 299.875 }, 800, 600 } };
    transaction.m_observations = { { { 9007199254740993ULL },
                                   { 1 },
                                   { 7 },
                                   { -7, 1001, 30000 },
                                   std::chrono::nanoseconds(123),
                                   std::string(64, 'a') },
                                  { { 2 },
                                   { 1 },
                                   { 8 },
                                   { 11, 1001, 30000 },
                                   std::chrono::nanoseconds(367033333),
                                   std::string(64, 'b') } };
    transaction.m_alignments = { { { 55 }, { 1 }, { { 0, 1, 0, 0 }, { 10, -20, 30 }, 2.5 } } };
    const double K_H = std::sqrt(.5);
    transaction.m_poses = { { { 9007199254740993ULL },
                            { 55 },
                            { { K_H, 0, K_H, 0 }, { 1, 2, 3 }, 1 },
                            std::chrono::nanoseconds(123),
                            { 1 },
                            TrackingState::TRACKING,
                            {} },
                           { { 2 },
                            { 55 },
                            { { .5, .5, .5, .5 }, { -1, 3, -2 }, 1 },
                            std::chrono::nanoseconds(367033333),
                            { 1 },
                            TrackingState::TRACKING,
                            {} } };
    auto& covariance = transaction.m_poses[0].m_covariance.emplace();
    for (std::size_t i { 0uz }; i < 6uz; ++i) {
        covariance[i * 6 + i] = double(i + 1) * .1;
    }
    REQUIRE(store.commit(transaction));
    return {
        store.acquire_snapshot(),
        { std::string(64, '1'), std::string(64, '2'), std::string(64, '3'), std::string(64, '4') } };
}

struct Directory {
    std::filesystem::path m_path;

    Directory() {
        static std::atomic<unsigned> count {};
        m_path = std::filesystem::temp_directory_path() /
                 ("we_pose_asset_" +
                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                  "_" + std::to_string(count++));
        REQUIRE(std::filesystem::create_directory(m_path));
    }

    ~Directory() {
        std::error_code ignored;
        std::filesystem::remove_all(m_path, ignored);
    }
};

}  // namespace

TEST_CASE(
    "canonical pose asset preserves doubles identities calibration timestamps and world transforms",
    "[poses][serialization]") {
    auto asset = fixture();
    const auto K_ENCODED = encode_pose_asset(asset);
    REQUIRE(K_ENCODED);
    const auto K_LOADED = decode_pose_asset(*K_ENCODED, sha256(*K_ENCODED));
    REQUIRE(K_LOADED);
    const auto K_REENCODED = encode_pose_asset(*K_LOADED);
    REQUIRE(K_REENCODED);
    CHECK(*K_REENCODED == *K_ENCODED);
    std::vector<std::byte> unaligned(K_ENCODED->size() + 1);
    std::copy(K_ENCODED->begin(), K_ENCODED->end(), unaligned.begin() + 1);
    CHECK(decode_pose_asset(std::span(unaligned).subspan(1), sha256(*K_ENCODED)));
    CHECK(K_LOADED->m_snapshot.get() != asset.m_snapshot.get());
    CHECK(K_LOADED->m_provenance.m_journal_sha256 == asset.m_provenance.m_journal_sha256);
    const auto& map = *K_LOADED->m_snapshot;
    CHECK(map.m_scale == ScaleStatus::ARBITRARY);
    CHECK(map.m_observations.at({ 9007199254740993ULL }).m_timestamp.m_pts == -7);
    CHECK(map.m_observations.at({ 2 }).m_timestamp.m_time_base_numerator == 1001);
    CHECK(map.m_poses.at({ 9007199254740993ULL }).m_covariance ==
          asset.m_snapshot->m_poses.at({ 9007199254740993ULL }).m_covariance);
    const auto K_INSPECTED = inspect_pose_asset(*K_LOADED);
    REQUIRE(K_INSPECTED);
    const auto K_JSON = nlohmann::json::parse(*K_INSPECTED);
    CHECK(K_JSON["provenance"]["gravity"] == "UNKNOWN");
    CHECK(K_JSON["calibrations"][0]["intrinsics"][2] == 319.25);
    CHECK(K_JSON["calibrations"][0]["distortion_k1_k2_p1_p2"][2] == .003);
    CHECK(K_JSON["calibrations"][0]["image_transform_sx_sy_ox_oy"][2] == -3.25);
    const auto& world = K_JSON["poses"][1]["world_from_camera"];
    CHECK(world["translation"] == nlohmann::json({ 12.5, -25., 22.5 }));
    CHECK(std::abs(world["rotation_wxyz"][1].get<double>() - std::sqrt(.5)) < 1e-12);
    CHECK(std::abs(world["rotation_wxyz"][3].get<double>() - std::sqrt(.5)) < 1e-12);
    CHECK(world["scale"] == 2.5);
    asset.m_provenance.m_journal_sha256.reset();
    const auto K_LEGACY = encode_pose_asset(asset);
    REQUIRE(K_LEGACY);
    const auto K_LEGACY_LOADED = decode_pose_asset(*K_LEGACY, sha256(*K_LEGACY));
    REQUIRE(K_LEGACY_LOADED);
    CHECK_FALSE(K_LEGACY_LOADED->m_provenance.m_journal_sha256);
}

TEST_CASE(
    "canonical pose reader rejects malformed structure and semantically invalid rehashed assets",
    "[poses][malformed]") {
    auto bytes = encode_pose_asset(fixture());
    REQUIRE(bytes);
    const auto K_MUTATION = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14);
    auto* root = pose_wire::GetMutablepose_asset(bytes->data());
    auto* pose = root->mutable_poses()->GetMutableObject(0);
    auto* camera = root->mutable_calibrations()->GetMutableObject(0);
    switch (K_MUTATION) {
    default:
        FAIL("Uncovered mutation");
        break;
    case 0:
        bytes->resize(bytes->size() / 2);
        break;
    case 1:
        (*bytes)[0] = std::byte { 0xff };
        (*bytes)[1] = std::byte { 0xff };
        break;
    case 2:
        REQUIRE(root->mutate_schema_version(2));
        break;
    case 3:
        REQUIRE(root->mutate_coordinates(static_cast<pose_wire::coordinate_profile>(255)));
        break;
    case 4:
        REQUIRE(camera->mutate_model(static_cast<pose_wire::camera_model>(255)));
        break;
    case 5:
        camera->mutable_intrinsics()->mutate_a(std::numeric_limits<double>::quiet_NaN());
        break;
    case 6:
        REQUIRE(pose->mutate_submap(99));
        break;
    case 7:
        REQUIRE(pose->mutate_revision(99));
        break;
    case 8:
        REQUIRE(pose->mutate_tracking(static_cast<pose_wire::tracking_state>(255)));
        break;
    case 9:
        pose->mutable_world_from_camera()->mutate_qw(1);
        break;
    case 10:
        REQUIRE(pose->mutate_frame(root->poses()->Get(1)->frame()));
        break;
    case 11:
        REQUIRE(root->mutable_observations()->GetMutableObject(0)->mutate_capture_nanoseconds(7));
        break;
    case 12:
        REQUIRE(root->mutable_provenance()->mutate_scale(
            static_cast<pose_wire::scale_provenance>(255)));
        break;
    case 13:
        root->mutable_poses()->GetMutableObject(1)->mutable_covariance()->Mutate(0, -1);
        break;
    case 14:
        pose->mutable_world_from_camera()->mutate_tx(1e200);
        break;
    }
    CHECK_FALSE(decode_pose_asset(*bytes, sha256(*bytes)));
}

TEST_CASE("canonical pose validation bounds covariance provenance and cancellation",
          "[poses][limits]") {
    const auto K_ASSET = fixture();
    const auto K_ENCODED = encode_pose_asset(K_ASSET);
    REQUIRE(K_ENCODED);
    CHECK_FALSE(decode_pose_asset(*K_ENCODED, std::string(64, '0')));
    CHECK_FALSE(decode_pose_asset(*K_ENCODED, ""));
    PoseAssetLimits limits;
    limits.m_encoded_bytes = { 1 };
    CHECK_FALSE(encode_pose_asset(K_ASSET, limits));
    CHECK_FALSE(decode_pose_asset(*K_ENCODED, sha256(*K_ENCODED), limits));
    limits = {};
    limits.m_records.m_max_frames = 1;
    CHECK_FALSE(encode_pose_asset(K_ASSET, limits));
    CHECK_FALSE(decode_pose_asset(*K_ENCODED, sha256(*K_ENCODED), limits));
    std::stop_source stop;
    stop.request_stop();
    CHECK(encode_pose_asset(K_ASSET, {}, stop.get_token()).error().m_code == ErrorCode::CANCELLED);
    CHECK(decode_pose_asset(*K_ENCODED, sha256(*K_ENCODED), {}, stop.get_token()).error().m_code ==
          ErrorCode::CANCELLED);
    for (unsigned mode { 0 }; mode < 6; ++mode) {
        auto invalid = K_ASSET;
        auto map = std::make_shared<MapState>(*K_ASSET.m_snapshot);
        invalid.m_snapshot = map;
        auto& optional_covariance = map->m_poses.at({ 9007199254740993ULL }).m_covariance;
        if (!optional_covariance) {
            FAIL("Missing fixture covariance");
            return;
        }
        auto& covariance = *optional_covariance;
        if (mode == 0) {
            covariance[0] = -1;
        }
        if (mode == 1) {
            covariance[1] = .01;
        }
        if (mode == 2) {
            covariance[1] = covariance[6] = 1;  // symmetric, positive diagonal, indefinite
        }
        if (mode == 3) {
            covariance[0] = std::numeric_limits<double>::infinity();
        }
        if (mode == 4) {
            map->m_scale = ScaleStatus::METRIC_MEASURED;
        }
        if (mode == 5) {
            invalid.m_provenance.m_journal_sha256 = "invented";
        }
        CHECK_FALSE(encode_pose_asset(invalid));
    }
    auto map = std::make_shared<MapState>(*K_ASSET.m_snapshot);
    map->m_invalidated_frames = { { 2 }, { 999 } };
    map->m_revision = { 7 };
    map->m_epoch = { 8 };
    if (auto& covariance = map->m_poses.at({ 9007199254740993ULL }).m_covariance; covariance) {
        covariance->fill(0);  // valid singular uncertainty
    } else {
        FAIL("Missing fixture covariance");
    }
    CanonicalPoseAsset corrected { map, K_ASSET.m_provenance };
    const auto K_REVISED = encode_pose_asset(corrected);
    REQUIRE(K_REVISED);
    const auto K_RESTORED = decode_pose_asset(*K_REVISED, sha256(*K_REVISED));
    REQUIRE(K_RESTORED);
    CHECK(K_RESTORED->m_snapshot->m_invalidated_frames == map->m_invalidated_frames);
    CHECK(K_RESTORED->m_snapshot->m_epoch.m_value == 8);
    CHECK(K_RESTORED->m_snapshot->m_revision.m_value == 7);
    CHECK(K_RESTORED->m_snapshot->m_poses.at({ 2 }).m_revision.m_value == 1);
}

TEST_CASE("canonical pose file publication is immutable and concurrent writers preserve the winner",
          "[poses][publication]") {
    Directory temporary;
    const auto K_PATH = temporary.m_path / "canonical.weposes";
    const auto K_ASSET = fixture();
    const auto K_HASH = write_pose_asset(K_PATH, K_ASSET);
    REQUIRE(K_HASH);
    const auto K_RESTORED = read_pose_asset(K_PATH, *K_HASH);
    REQUIRE(K_RESTORED);
    CHECK_FALSE(write_pose_asset(K_PATH, K_ASSET));
    CHECK(read_pose_asset(K_PATH, *K_HASH));
    const auto K_OCCUPIED = temporary.m_path / "caller.txt";
    std::ofstream(K_OCCUPIED) << "caller data";
    CHECK_FALSE(write_pose_asset(K_OCCUPIED, K_ASSET));
    std::ifstream existing(K_OCCUPIED);
    std::string text;
    std::getline(existing, text);
    CHECK(text == "caller data");
    const auto K_CANCELLED = temporary.m_path / "cancelled.weposes";
    std::stop_source stop;
    stop.request_stop();
    CHECK(write_pose_asset(K_CANCELLED, K_ASSET, {}, stop.get_token()).error().m_code ==
          ErrorCode::CANCELLED);
    CHECK_FALSE(std::filesystem::exists(K_CANCELLED));
    const auto K_RACED = temporary.m_path / "race.weposes";
    std::atomic<unsigned> successes {};
    const auto K_WRITER = [&] {
        if (write_pose_asset(K_RACED, K_ASSET)) {
            ++successes;
        }
    };
    { std::jthread first(K_WRITER), second(K_WRITER); }
    CHECK(successes == 1);
    CHECK(read_pose_asset(K_RACED, *K_HASH));
    CHECK_FALSE(write_pose_asset(temporary.m_path / "missing" / "poses.weposes", K_ASSET));
    for (const auto& entry : std::filesystem::directory_iterator(temporary.m_path)) {
        CHECK(!entry.path().filename().string().starts_with(".weposes-tmp-"));
        CHECK(!entry.path().filename().string().starts_with(".we-map-tmp-"));
    }
    std::fstream file(K_PATH, std::ios::binary | std::ios::in | std::ios::out);
    file.put('!');
    file.close();
    CHECK_FALSE(read_pose_asset(K_PATH, *K_HASH));
}

TEST_CASE("canonical byte and record limits reject invalid configurations before reading files",
          "[canonical_admission][limits]") {
    Directory temporary;
    const auto K_ASSET = fixture();
    const auto K_SOURCE = temporary.m_path / "synthetic.weposes";
    const auto K_HASH = write_pose_asset(K_SOURCE, K_ASSET);
    REQUIRE(K_HASH);
    const auto K_BYTES = encode_pose_asset(K_ASSET);
    REQUIRE(K_BYTES);
    PoseAssetLimits limits;
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
    CAPTURE(K_FIELD);
    switch (K_FIELD) {
    case 0:
        limits.m_encoded_bytes.m_bytes = 0;
        break;
    case 1:
        limits.m_encoded_bytes.m_bytes = 256uz * 1024 * 1024 + 1;
        break;
    case 2:
        limits.m_records.m_max_frames = 0;
        break;
    case 3:
        limits.m_records.m_max_frames = 1000001;
        break;
    case 4:
        limits.m_records.m_max_calibrations = 0;
        break;
    case 5:
        limits.m_records.m_max_calibrations = 4097;
        break;
    case 6:
        limits.m_records.m_max_submaps = 0;
        break;
    case 7:
        limits.m_records.m_max_submaps = 4097;
        break;
    default:
        FAIL("unknown canonical limit");
    }
    const auto K_ENCODED = encode_pose_asset(K_ASSET, limits);
    REQUIRE(!K_ENCODED);
    CHECK(K_ENCODED.error().m_code == ErrorCode::INVALID_ARGUMENT);
    const auto K_DECODED = decode_pose_asset(*K_BYTES, *K_HASH, limits);
    REQUIRE(!K_DECODED);
    CHECK(K_DECODED.error().m_code == ErrorCode::INVALID_ARGUMENT);
    const auto K_READ = read_pose_asset(K_SOURCE, *K_HASH, limits);
    REQUIRE(!K_READ);
    CHECK(K_READ.error().m_code == ErrorCode::INVALID_ARGUMENT);
    const auto K_INSPECTED = inspect_pose_asset(K_ASSET, limits);
    REQUIRE(!K_INSPECTED);
    CHECK(K_INSPECTED.error().m_code == ErrorCode::INVALID_ARGUMENT);
    const auto K_PRESERVED = sha256_file(K_SOURCE, K_BYTES->size());
    REQUIRE(K_PRESERVED);
    CHECK(*K_PRESERVED == *K_HASH);
    REQUIRE(read_pose_asset(K_SOURCE, *K_HASH));
}

TEST_CASE("canonical record admission bounds each collection independently",
          "[canonical_admission][limits]") {
    auto asset = fixture();
    auto state = std::make_shared<MapState>(*asset.m_snapshot);
    asset.m_snapshot = state;
    state->m_world_from_submap.emplace(SubmapId { 56 }, SimilarityTransform {});
    state->m_invalidated_frames = { { 1 }, { 2 } };
    const auto K_FIELD = GENERATE(0, 1, 2, 3, 4);
    CAPTURE(K_FIELD);
    if (K_FIELD == 2 || K_FIELD == 4) {
        state->m_observations.erase({ 9007199254740993ULL });
        state->m_poses.erase({ 9007199254740993ULL });
    }
    auto bytes = encode_pose_asset(asset);
    REQUIRE(bytes);
    if (K_FIELD == 2) {
        // Keep two poses but one observation so the independent pose budget is reached.
        state->m_poses.emplace(FrameId { 9007199254740993ULL },
                               fixture().m_snapshot->m_poses.at({ 9007199254740993ULL }));
        auto full = fixture();
        bytes = encode_pose_asset(full);
        REQUIRE(bytes);
        auto* observations = pose_wire::GetMutablepose_asset(bytes->data())->mutable_observations();
        flatbuffers::WriteScalar(reinterpret_cast<std::uint8_t*>(observations), std::uint32_t { 1 });
    }
    PoseAssetLimits limits;
    if (K_FIELD == 0) {
        limits.m_records.m_max_calibrations = 1;
    } else if (K_FIELD == 3) {
        limits.m_records.m_max_submaps = 1;
    } else {
        limits.m_records.m_max_frames = 1;
    }
    const auto K_ENCODED = encode_pose_asset(asset, limits);
    REQUIRE(!K_ENCODED);
    CHECK(K_ENCODED.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
    const auto K_DECODED = decode_pose_asset(*bytes, sha256(*bytes), limits);
    REQUIRE(!K_DECODED);
    CHECK(K_DECODED.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
    if (K_FIELD == 2) {
        const auto K_UNBOUNDED = encode_pose_asset(asset);
        REQUIRE(!K_UNBOUNDED);
        CHECK(K_UNBOUNDED.error().m_code == ErrorCode::INVALID_DATA);
    } else {
        const auto K_UNBOUNDED = encode_pose_asset(asset);
        REQUIRE(K_UNBOUNDED);
        CHECK(*K_UNBOUNDED == *bytes);
    }
}

TEST_CASE("canonical encoding has exact byte admission and rejects incomplete publications",
          "[canonical_admission][bytes]") {
    Directory temporary;
    const auto K_ASSET = fixture();
    const auto K_BYTES = encode_pose_asset(K_ASSET);
    REQUIRE(K_BYTES);
    const auto K_HASH = sha256(*K_BYTES);
    // Sweep output budgets without depending on FlatBuffers' internal record layout.
    for (std::size_t budget { 1uz }; budget < K_BYTES->size(); budget += 64uz) {
        CAPTURE(budget);
        const auto K_REJECTED = encode_pose_asset(K_ASSET, { { budget }, {} });
        REQUIRE(!K_REJECTED);
        CHECK(K_REJECTED.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
    }
    const PoseAssetLimits K_EXACT { { K_BYTES->size() }, {} };
    const auto K_ENCODED = encode_pose_asset(K_ASSET, K_EXACT);
    REQUIRE(K_ENCODED);
    CHECK(*K_ENCODED == *K_BYTES);
    REQUIRE(decode_pose_asset(*K_BYTES, K_HASH, K_EXACT));
    const auto K_EMPTY = decode_pose_asset({}, K_HASH);
    REQUIRE(!K_EMPTY);
    CHECK(K_EMPTY.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
    const PoseAssetLimits K_SHORT_BUDGET { { K_BYTES->size() - 1 }, {} };
    const auto K_REJECTED = encode_pose_asset(K_ASSET, K_SHORT_BUDGET);
    REQUIRE(!K_REJECTED);
    CHECK(K_REJECTED.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
    const auto K_DESTINATION = temporary.m_path / "never-published.weposes";
    const auto K_WRITTEN = write_pose_asset(K_DESTINATION, K_ASSET, K_SHORT_BUDGET);
    REQUIRE(!K_WRITTEN);
    CHECK(K_WRITTEN.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
    CHECK(std::filesystem::is_empty(temporary.m_path));
    const auto K_PUBLISHED = write_pose_asset(K_DESTINATION, K_ASSET, K_EXACT);
    REQUIRE(K_PUBLISHED);
    CHECK(*K_PUBLISHED == K_HASH);
    const auto K_READ = read_pose_asset(K_DESTINATION, K_HASH, K_SHORT_BUDGET);
    REQUIRE(!K_READ);
    CHECK(K_READ.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
    REQUIRE(read_pose_asset(K_DESTINATION, K_HASH, K_EXACT));
    const auto K_PRESERVED = sha256_file(K_DESTINATION, K_BYTES->size());
    REQUIRE(K_PRESERVED);
    CHECK(*K_PRESERVED == *K_PUBLISHED);
}

TEST_CASE("rehashed canonical provenance rejects each short hash and invalid hexadecimal value",
          "[canonical_admission][provenance]") {
    const auto K_ASSET = fixture();
    auto bytes = encode_pose_asset(K_ASSET);
    REQUIRE(bytes);
    auto* provenance = pose_wire::GetMutablepose_asset(bytes->data())->mutable_provenance();
    const auto K_FIELD = GENERATE(0, 1, 2, 3);
    const auto K_SHORT_HASH = GENERATE(false, true);
    flatbuffers::String* hash {};
    if (K_FIELD == 0) {
        hash = provenance->mutable_source_sha256();
    } else if (K_FIELD == 1) {
        hash = provenance->mutable_decode_completion_sha256();
    } else if (K_FIELD == 2) {
        hash = provenance->mutable_pose_completion_sha256();
    } else {
        hash = provenance->mutable_journal_sha256();
    }
    REQUIRE(hash);
    if (K_SHORT_HASH) {
        hash->Mutate(63, '\0');
        flatbuffers::WriteScalar(reinterpret_cast<std::uint8_t*>(hash), std::uint32_t { 63 });
    } else {
        hash->Mutate(0, 'G');
    }
    const auto K_SUPPLIED = *bytes;
    const auto K_REJECTED = decode_pose_asset(*bytes, sha256(*bytes));
    REQUIRE(!K_REJECTED);
    CHECK(K_REJECTED.error().m_code == ErrorCode::INVALID_DATA);
    CHECK(*bytes == K_SUPPLIED);
    REQUIRE(encode_pose_asset(K_ASSET));
}

TEST_CASE("canonical inspection rejects invalid input and reports invalidations without a journal",
          "[canonical_admission][inspection]") {
    auto asset = fixture();
    auto state = std::make_shared<MapState>(*asset.m_snapshot);
    state->m_invalidated_frames = { { 2 } };
    asset.m_snapshot = state;
    asset.m_provenance.m_journal_sha256.reset();
    const auto K_TEXT = inspect_pose_asset(asset);
    REQUIRE(K_TEXT);
    const auto K_JSON = nlohmann::json::parse(*K_TEXT);
    CHECK(K_JSON["provenance"]["journal_sha256"].is_null());
    CHECK(K_JSON["invalidated_frames"] == nlohmann::json::array({ 2 }));
    const auto K_REJECTED = inspect_pose_asset(asset, { { K_TEXT->size() - 1 }, {} });
    REQUIRE(!K_REJECTED);
    CHECK(K_REJECTED.error().m_code == ErrorCode::RESOURCE_EXHAUSTED);
    const auto K_EXACT = inspect_pose_asset(asset, { { K_TEXT->size() }, {} });
    REQUIRE(K_EXACT);
    CHECK(*K_EXACT == *K_TEXT);
    asset.m_snapshot.reset();
    const auto K_MISSING = inspect_pose_asset(asset);
    REQUIRE(!K_MISSING);
    CHECK(K_MISSING.error().m_code == ErrorCode::INVALID_ARGUMENT);
}
