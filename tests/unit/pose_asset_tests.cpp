#include "poses_generated.h"
#include "world_explorer/core/hash.hpp"
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
canonical_pose_asset fixture() {
    map_store store;
    map_transaction transaction;
    transaction.new_revision = {1};
    transaction.operation = {99};
    transaction.calibrations = {{{7},
                                 {711, 529, 319.25, 241.75},
                                 640,
                                 480,
                                 calibration_model::OPENCV,
                                 {.1, -.2, .003, -.004},
                                 {.5, .75, -3.25, 2.5}},
                                {{8}, {801, 603, 400.125, 299.875}, 800, 600}};
    transaction.observations = {{{9007199254740993ULL},
                                 {1},
                                 {7},
                                 {-7, 1001, 30000},
                                 std::chrono::nanoseconds(123),
                                 std::string(64, 'a')},
                                {{2},
                                 {1},
                                 {8},
                                 {11, 1001, 30000},
                                 std::chrono::nanoseconds(367033333),
                                 std::string(64, 'b')}};
    transaction.alignments = {{{55}, {1}, {{0, 1, 0, 0}, {10, -20, 30}, 2.5}}};
    const double h = std::sqrt(.5);
    transaction.poses = {{{9007199254740993ULL},
                          {55},
                          {{h, 0, h, 0}, {1, 2, 3}, 1},
                          std::chrono::nanoseconds(123),
                          {1},
                          tracking_state::TRACKING,
                          {}},
                         {{2},
                          {55},
                          {{.5, .5, .5, .5}, {-1, 3, -2}, 1},
                          std::chrono::nanoseconds(367033333),
                          {1},
                          tracking_state::TRACKING,
                          {}}};
    auto& covariance = transaction.poses[0].covariance.emplace();
    for (std::size_t i = 0; i < 6; ++i)
        covariance[i * 6 + i] = double(i + 1) * .1;
    REQUIRE(store.commit(transaction));
    return {
        store.acquire_snapshot(),
        {std::string(64, '1'), std::string(64, '2'), std::string(64, '3'), std::string(64, '4')}};
}
struct directory {
    std::filesystem::path path;
    directory() {
        static std::atomic<unsigned> count{};
        path = std::filesystem::temp_directory_path() /
               ("we_pose_asset_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
                std::to_string(count++));
        REQUIRE(std::filesystem::create_directory(path));
    }
    ~directory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};
} // namespace
TEST_CASE(
    "canonical pose asset preserves doubles identities calibration timestamps and world transforms",
    "[poses][serialization]") {
    auto asset = fixture();
    const auto encoded = encode_pose_asset(asset);
    REQUIRE(encoded);
    const auto loaded = decode_pose_asset(*encoded, sha256(*encoded));
    REQUIRE(loaded);
    const auto reencoded = encode_pose_asset(*loaded);
    REQUIRE(reencoded);
    CHECK(*reencoded == *encoded);
    std::vector<std::byte> unaligned(encoded->size() + 1);
    std::copy(encoded->begin(), encoded->end(), unaligned.begin() + 1);
    CHECK(decode_pose_asset(std::span(unaligned).subspan(1), sha256(*encoded)));
    CHECK(loaded->snapshot.get() != asset.snapshot.get());
    CHECK(loaded->provenance.journal_sha256 == asset.provenance.journal_sha256);
    const auto& map = *loaded->snapshot;
    CHECK(map.scale == scale_status::ARBITRARY);
    CHECK(map.observations.at({9007199254740993ULL}).timestamp.pts == -7);
    CHECK(map.observations.at({2}).timestamp.time_base_numerator == 1001);
    CHECK(map.poses.at({9007199254740993ULL}).covariance ==
          asset.snapshot->poses.at({9007199254740993ULL}).covariance);
    const auto inspected = inspect_pose_asset(*loaded);
    REQUIRE(inspected);
    const auto json = nlohmann::json::parse(*inspected);
    CHECK(json["provenance"]["gravity"] == "UNKNOWN");
    CHECK(json["calibrations"][0]["intrinsics"][2] == 319.25);
    CHECK(json["calibrations"][0]["distortion_k1_k2_p1_p2"][2] == .003);
    CHECK(json["calibrations"][0]["image_transform_sx_sy_ox_oy"][2] == -3.25);
    const auto& world = json["poses"][1]["world_from_camera"];
    CHECK(world["translation"] == nlohmann::json({12.5, -25., 22.5}));
    CHECK(std::abs(world["rotation_wxyz"][1].get<double>() - std::sqrt(.5)) < 1e-12);
    CHECK(std::abs(world["rotation_wxyz"][3].get<double>() - std::sqrt(.5)) < 1e-12);
    CHECK(world["scale"] == 2.5);
    asset.provenance.journal_sha256.reset();
    const auto legacy = encode_pose_asset(asset);
    REQUIRE(legacy);
    const auto legacy_loaded = decode_pose_asset(*legacy, sha256(*legacy));
    REQUIRE(legacy_loaded);
    CHECK_FALSE(legacy_loaded->provenance.journal_sha256);
}
TEST_CASE(
    "canonical pose reader rejects malformed structure and semantically invalid rehashed assets",
    "[poses][malformed]") {
    auto bytes = encode_pose_asset(fixture());
    REQUIRE(bytes);
    const auto mutation = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14);
    auto* root = pose_wire::GetMutablepose_asset(bytes->data());
    auto* pose = root->mutable_poses()->GetMutableObject(0);
    auto* camera = root->mutable_calibrations()->GetMutableObject(0);
    switch (mutation) {
    default:
        FAIL("Uncovered mutation");
        break;
    case 0:
        bytes->resize(bytes->size() / 2);
        break;
    case 1:
        (*bytes)[0] = std::byte{0xff};
        (*bytes)[1] = std::byte{0xff};
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
    const auto asset = fixture();
    const auto encoded = encode_pose_asset(asset);
    REQUIRE(encoded);
    CHECK_FALSE(decode_pose_asset(*encoded, std::string(64, '0')));
    CHECK_FALSE(decode_pose_asset(*encoded, ""));
    pose_asset_limits limits;
    limits.encoded_bytes = {1};
    CHECK_FALSE(encode_pose_asset(asset, limits));
    CHECK_FALSE(decode_pose_asset(*encoded, sha256(*encoded), limits));
    limits = {};
    limits.records.max_frames = 1;
    CHECK_FALSE(encode_pose_asset(asset, limits));
    CHECK_FALSE(decode_pose_asset(*encoded, sha256(*encoded), limits));
    std::stop_source stop;
    stop.request_stop();
    CHECK(encode_pose_asset(asset, {}, stop.get_token()).error().code == error_code::CANCELLED);
    CHECK(decode_pose_asset(*encoded, sha256(*encoded), {}, stop.get_token()).error().code ==
          error_code::CANCELLED);
    for (unsigned mode = 0; mode < 6; ++mode) {
        auto invalid = asset;
        auto map = std::make_shared<map_state>(*asset.snapshot);
        invalid.snapshot = map;
        auto& optional_covariance = map->poses.at({9007199254740993ULL}).covariance;
        if (!optional_covariance) {
            FAIL("Missing fixture covariance");
            return;
        }
        auto& covariance = *optional_covariance;
        if (mode == 0)
            covariance[0] = -1;
        if (mode == 1)
            covariance[1] = .01;
        if (mode == 2)
            covariance[1] = covariance[6] = 1; // symmetric, positive diagonal, indefinite
        if (mode == 3)
            covariance[0] = std::numeric_limits<double>::infinity();
        if (mode == 4)
            map->scale = scale_status::METRIC_MEASURED;
        if (mode == 5)
            invalid.provenance.journal_sha256 = "invented";
        CHECK_FALSE(encode_pose_asset(invalid));
    }
    auto map = std::make_shared<map_state>(*asset.snapshot);
    map->invalidated_frames = {{2}, {999}};
    map->revision = {7};
    map->epoch = {8};
    if (auto& covariance = map->poses.at({9007199254740993ULL}).covariance; covariance)
        covariance->fill(0); // valid singular uncertainty
    else
        FAIL("Missing fixture covariance");
    canonical_pose_asset corrected{map, asset.provenance};
    const auto revised = encode_pose_asset(corrected);
    REQUIRE(revised);
    const auto restored = decode_pose_asset(*revised, sha256(*revised));
    REQUIRE(restored);
    CHECK(restored->snapshot->invalidated_frames == map->invalidated_frames);
    CHECK(restored->snapshot->epoch.value == 8);
    CHECK(restored->snapshot->revision.value == 7);
    CHECK(restored->snapshot->poses.at({2}).revision.value == 1);
}
TEST_CASE("canonical pose file publication is immutable and concurrent writers preserve the winner",
          "[poses][publication]") {
    directory temporary;
    const auto path = temporary.path / "canonical.weposes";
    const auto asset = fixture();
    const auto hash = write_pose_asset(path, asset);
    REQUIRE(hash);
    const auto restored = read_pose_asset(path, *hash);
    REQUIRE(restored);
    CHECK_FALSE(write_pose_asset(path, asset));
    CHECK(read_pose_asset(path, *hash));
    const auto occupied = temporary.path / "caller.txt";
    std::ofstream(occupied) << "caller data";
    CHECK_FALSE(write_pose_asset(occupied, asset));
    std::ifstream existing(occupied);
    std::string text;
    std::getline(existing, text);
    CHECK(text == "caller data");
    const auto cancelled = temporary.path / "cancelled.weposes";
    std::stop_source stop;
    stop.request_stop();
    CHECK(write_pose_asset(cancelled, asset, {}, stop.get_token()).error().code ==
          error_code::CANCELLED);
    CHECK_FALSE(std::filesystem::exists(cancelled));
    const auto raced = temporary.path / "race.weposes";
    std::atomic<unsigned> successes{};
    const auto writer = [&] {
        if (write_pose_asset(raced, asset))
            ++successes;
    };
    {
        std::jthread first(writer), second(writer);
    }
    CHECK(successes == 1);
    CHECK(read_pose_asset(raced, *hash));
    CHECK_FALSE(write_pose_asset(temporary.path / "missing" / "poses.weposes", asset));
    for (const auto& entry : std::filesystem::directory_iterator(temporary.path)) {
        CHECK(!entry.path().filename().string().starts_with(".weposes-tmp-"));
        CHECK(!entry.path().filename().string().starts_with(".we-map-tmp-"));
    }
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    file.put('!');
    file.close();
    CHECK_FALSE(read_pose_asset(path, *hash));
}
