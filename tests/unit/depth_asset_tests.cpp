#include "world_explorer/application/depth_assets.hpp"
#include "world_explorer/core/asset_files.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/map_store/pose_asset.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <fstream>
#include <iterator>
#include <limits>

using namespace world_explorer;

namespace {

DepthImageAsset fixture() {
    DepthImageAsset asset;
    asset.m_metadata.m_dependency = {
        { 7 }, { 9 }, { 9007199254740993ULL }, { 3 }, std::string(64, 'a'), std::string(64, 'b') };
    asset.m_metadata.m_width = 3;
    asset.m_metadata.m_height = 2;
    asset.m_metadata.m_camera = { 2, 4, 1, .5 };
    asset.m_metadata.m_calibrated_image_sha256 = std::string(64, 'c');
    asset.m_metadata.m_recipe_sha256 = std::string(64, 'd');
    asset.m_axial_depth = { 2, 0, 4, 1, 8, 0 };
    asset.m_valid = { 1, 0, 1, 1, 1, 0 };
    asset.m_confidence = { .5, 0, 1, 0, .25, 0 };
    asset.m_support_count = { 2, 0, 4, 1, 3, 0 };
    return asset;
}

struct Directory {
    std::filesystem::path m_path;

    Directory() {
        static std::atomic<unsigned> sequence {};
        m_path = std::filesystem::temp_directory_path() /
                 ("we_depth_" +
                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                  "_" + std::to_string(sequence++));
        REQUIRE(std::filesystem::create_directory(m_path));
    }

    ~Directory() {
        std::error_code ignored;
        std::filesystem::remove_all(m_path, ignored);
    }
};

// Fixed wire offsets are independent of the production encoder's implementation.
void put_u32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t i { 0uz }; i < 4uz; ++i) {
        bytes.at(offset + i) = static_cast<std::byte>(value & 255U);
        value >>= 8U;
    }
}

}  // namespace

TEST_CASE("depth planes preserve explicit unknowns camera units and wide frame identities",
          "[depth]") {
    const auto K_ASSET = fixture();
    const auto K_ENCODED = encode_depth_asset(K_ASSET);
    REQUIRE(K_ENCODED);
    CHECK(K_ENCODED->size() == 356 + 6 * 13);
    const auto K_DECODED = decode_depth_asset(*K_ENCODED, sha256(*K_ENCODED));
    REQUIRE(K_DECODED);
    CHECK(K_DECODED->m_metadata.m_dependency.m_frame.m_value == 9007199254740993ULL);
    CHECK(K_DECODED->m_metadata.m_camera.m_cy == .5);
    CHECK(K_DECODED->m_axial_depth == K_ASSET.m_axial_depth);
    CHECK(K_DECODED->m_valid == K_ASSET.m_valid);
    CHECK(K_DECODED->m_confidence == K_ASSET.m_confidence);
    CHECK(K_DECODED->m_support_count == K_ASSET.m_support_count);
    CHECK((K_DECODED->m_standard_deviation).has_value() == false);
    CHECK(admit_depth_asset(*K_DECODED, K_ASSET.m_metadata));
    CHECK(*encode_depth_asset(*K_DECODED) == *K_ENCODED);
    std::vector<std::byte> unaligned(K_ENCODED->size() + 1);
    std::copy(K_ENCODED->begin(), K_ENCODED->end(), unaligned.begin() + 1);
    CHECK(decode_depth_asset(std::span(unaligned).subspan(1), sha256(*K_ENCODED)));
    // Off-axis axial Z=4 is (2,-.5,4), whose range is greater than 4.
    const auto K_POINT =
        unproject(K_DECODED->m_metadata.m_camera, { 2, 0 }, K_DECODED->m_axial_depth[2]);
    REQUIRE(K_POINT);
    CHECK(*K_POINT == Vec3d { 2, -.5, 4 });
    CHECK(std::sqrt(dot(*K_POINT, *K_POINT)) > 4);
    CHECK((*project(K_DECODED->m_metadata.m_camera, *K_POINT))[0] == 2);
}

TEST_CASE("depth wire planes have independent little endian scalar oracles", "[depth]") {
    auto asset = fixture();
    asset.m_standard_deviation = std::vector<float> { .25, 0, .5, 0, 1, 0 };
    const auto K_BYTES = encode_depth_asset(asset);
    REQUIRE(K_BYTES);
    CHECK(K_BYTES->size() == 356 + 6 * 17);
    // First depth=2: IEEE 0x40000000; validity begins after six floats.
    CHECK(K_BYTES->at(356) == std::byte { 0 });
    CHECK(K_BYTES->at(359) == std::byte { 0x40 });
    CHECK(K_BYTES->at(380) == std::byte { 1 });
    CHECK(K_BYTES->at(381) == std::byte { 0 });
    // Confidence=.5 at 386; support=2 at 410; sigma=.25 at 434.
    CHECK(K_BYTES->at(389) == std::byte { 0x3f });
    CHECK(K_BYTES->at(410) == std::byte { 2 });
    CHECK(K_BYTES->at(437) == std::byte { 0x3e });
    const auto K_DECODED = decode_depth_asset(*K_BYTES, sha256(*K_BYTES));
    REQUIRE(K_DECODED);
    CHECK(K_DECODED->m_standard_deviation == asset.m_standard_deviation);
    CHECK(K_DECODED->m_metadata.m_producer == DepthProducerKind::SYNTHETIC_DIAGNOSTIC);
}

TEST_CASE(
    "depth rejects rehashed unsupported semantics dimensions and identities before allocation",
    "[depth]") {
    const auto K_OFFSET = GENERATE(8uz, 12, 16, 20, 24, 28, 32, 100);
    const auto K_VALUE = GENERATE(0U, 0xffffffffU);
    const auto K_ORIGINAL = encode_depth_asset(fixture());
    REQUIRE(K_ORIGINAL);
    auto bytes = *K_ORIGINAL;
    put_u32(bytes, K_OFFSET, K_VALUE);
    const auto K_DECODED = decode_depth_asset(bytes, sha256(bytes));
    // Scale=0 is its existing arbitrary-unit encoding; uncertainty=0 is absent.
    if ((K_OFFSET == 20 || K_OFFSET == 32) && K_VALUE == 0) {
        CHECK(K_DECODED);
    } else {
        CHECK((K_DECODED).has_value() == false);
    }
    CHECK(*encode_depth_asset(fixture()) == *K_ORIGINAL);
}

TEST_CASE("depth rejects rehashed malformed samples in every serialized plane", "[depth]") {
    const auto K_OFFSET = GENERATE(356uz, 386, 410, 434);
    auto asset = fixture();
    asset.m_standard_deviation = std::vector<float> { .25, 0, .5, .125, 1, 0 };
    const auto K_ORIGINAL = encode_depth_asset(asset);
    REQUIRE(K_ORIGINAL);
    auto bytes = *K_ORIGINAL;
    put_u32(bytes, K_OFFSET, 0x7fc00000U);
    CHECK((decode_depth_asset(bytes, sha256(bytes))).has_value() == false);
    bytes = *K_ORIGINAL;
    bytes.at(380) = std::byte { 2 };
    CHECK((decode_depth_asset(bytes, sha256(bytes))).has_value() == false);
}

TEST_CASE("depth validates sample support masks confidence and optional uncertainty", "[depth]") {
    auto asset = fixture();
    const auto K_ORIGINAL = *encode_depth_asset(asset);
    const auto K_MUTATION = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11);
    switch (K_MUTATION) {
    case 0:
        asset.m_axial_depth[0] = 0;
        break;
    case 1:
        asset.m_axial_depth[1] = 1;
        break;
    case 2:
        asset.m_axial_depth[1] = -0.0F;
        break;
    case 3:
        asset.m_confidence[0] = 1.01F;
        break;
    case 4:
        asset.m_confidence[1] = .1F;
        break;
    case 5:
        asset.m_support_count[0] = 0;
        break;
    case 6:
        asset.m_support_count[1] = 1;
        break;
    case 7:
        asset.m_support_count[0] = 1000001;
        break;
    case 8:
        asset.m_confidence.pop_back();
        break;
    case 9:
        asset.m_standard_deviation = std::vector<float>(5);
        break;
    case 10:
        asset.m_standard_deviation = std::vector<float> { -1, 0, .5, 0, 1, 0 };
        break;
    case 11:
        asset.m_standard_deviation = std::vector<float> { 1, 1, 1, 1, 1, 1 };
        break;
    default:
        FAIL("Unknown mutation");
    }
    const auto K_REJECTED = encode_depth_asset(asset);
    REQUIRE((K_REJECTED).has_value() == false);
    CHECK(K_REJECTED.error().m_code == ErrorCode::INVALID_DATA);
    CHECK(*encode_depth_asset(fixture()) == K_ORIGINAL);
}

TEST_CASE("depth enforces independent exact pixel payload and encoded byte budgets", "[depth]") {
    const auto K_ASSET = fixture();
    DepthAssetLimits exact;
    exact.m_max_dimension = 3;
    exact.m_max_pixels = 6;
    exact.m_payload_bytes.m_bytes = 78;
    exact.m_encoded_bytes.m_bytes = 434;
    const auto K_ENCODED = encode_depth_asset(K_ASSET, exact);
    REQUIRE(K_ENCODED);
    REQUIRE(decode_depth_asset(*K_ENCODED, sha256(*K_ENCODED), exact));
    const auto K_LIMIT = GENERATE(0, 1, 2, 3, 4);
    switch (K_LIMIT) {
    case 0:
        --exact.m_max_pixels;
        break;
    case 1:
        --exact.m_max_dimension;
        break;
    case 2:
        --exact.m_payload_bytes.m_bytes;
        break;
    case 3:
        --exact.m_encoded_bytes.m_bytes;
        break;
    case 4:
        exact.m_max_support_count = 3;
        break;
    default:
        FAIL("Unknown budget");
    }
    CHECK((encode_depth_asset(K_ASSET, exact)).has_value() == false);
    CHECK((decode_depth_asset(*K_ENCODED, sha256(*K_ENCODED), exact)).has_value() == false);
}

TEST_CASE("depth checksum version truncation and trailing data fail without caller mutation",
          "[depth]") {
    const auto K_ORIGINAL = *encode_depth_asset(fixture());
    for (std::size_t length { 0uz }; length < K_ORIGINAL.size(); ++length) {
        const auto K_BYTES = std::span(K_ORIGINAL).first(length);
        CHECK((decode_depth_asset(K_BYTES, sha256(K_BYTES))).has_value() == false);
    }
    auto appended = K_ORIGINAL;
    appended.push_back(std::byte { 0 });
    CHECK((decode_depth_asset(appended, sha256(appended))).has_value() == false);
    CHECK((decode_depth_asset(K_ORIGINAL, std::string(64, 'f'))).has_value() == false);
    CHECK((decode_depth_asset(K_ORIGINAL, "INVALID")).has_value() == false);
    CHECK(*encode_depth_asset(fixture()) == K_ORIGINAL);
}

TEST_CASE("depth wrong epoch revision and dependency require resynchronization", "[depth]") {
    const auto K_ASSET = fixture();
    const auto K_ORIGINAL = *encode_depth_asset(K_ASSET);
    auto expected = K_ASSET.m_metadata;
    const auto K_MUTATION = GENERATE(0, 1, 2);
    const auto K_CODE = K_MUTATION == 0   ? ErrorCode::WRONG_EPOCH
                        : K_MUTATION == 1 ? ErrorCode::WRONG_BASE
                                          : ErrorCode::RESYNC_REQUIRED;
    if (K_MUTATION == 0) {
        ++expected.m_dependency.m_epoch.m_value;
    }
    if (K_MUTATION == 1) {
        ++expected.m_dependency.m_pose_revision.m_value;
    }
    if (K_MUTATION == 2) {
        expected.m_dependency.m_pose_asset_sha256 = std::string(64, 'e');
    }
    const auto K_REJECTED = admit_depth_asset(K_ASSET, expected);
    REQUIRE((K_REJECTED).has_value() == false);
    CHECK(K_REJECTED.error().m_code == K_CODE);
    CHECK(*encode_depth_asset(K_ASSET) == K_ORIGINAL);
}

TEST_CASE("depth admission binds the full calibrated domain recipe and source observation",
          "[depth]") {
    const auto K_ASSET = fixture();
    auto expected = K_ASSET.m_metadata;
    const auto K_MUTATION = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
    switch (K_MUTATION) {
    case 0:
        ++expected.m_dependency.m_frame.m_value;
        break;
    case 1:
        ++expected.m_dependency.m_calibration.m_value;
        break;
    case 2:
        expected.m_dependency.m_observation_image_sha256 = std::string(64, 'e');
        break;
    case 3:
        expected.m_calibrated_image_sha256 = std::string(64, 'e');
        break;
    case 4:
        expected.m_recipe_sha256 = std::string(64, 'e');
        break;
    case 5:
        expected.m_camera.m_cx += .5;
        break;
    case 6:
        expected.m_width = 2;
        break;
    case 7:
        expected.m_producer = DepthProducerKind::MULTIVIEW_STEREO;
        break;
    default:
        FAIL("Unknown metadata change");
    }
    CHECK((admit_depth_asset(K_ASSET, expected)).has_value() == false);
}

TEST_CASE("depth metric range and invalid limit claims remain unsupported", "[depth]") {
    auto asset = fixture();
    SECTION("ray range") {
        asset.m_metadata.m_convention = DepthConvention::RAY_RANGE;
    }
    SECTION("measured units") {
        asset.m_metadata.m_scale = ScaleStatus::METRIC_MEASURED;
    }
    const auto K_CHECKED = encode_depth_asset(asset);
    REQUIRE((K_CHECKED).has_value() == false);
    CHECK(K_CHECKED.error().m_code == ErrorCode::UNSUPPORTED_OPERATION);
}

TEST_CASE("depth file publication preserves existing entries and admission failures", "[depth]") {
    Directory root;
    const auto K_ASSET = fixture();
    const auto K_PATH = root.m_path / "image.wedepth";
    const auto K_DIGEST = write_depth_asset(K_PATH, K_ASSET);
    REQUIRE(K_DIGEST);
    const auto K_LOADED = read_depth_asset(K_PATH, *K_DIGEST, K_ASSET.m_metadata);
    REQUIRE(K_LOADED);
    const auto K_ORIGINAL = *asset_files::read_bytes(K_PATH, 1024, {});
    auto changed = K_ASSET;
    changed.m_axial_depth[0] = 3;
    CHECK((write_depth_asset(K_PATH, changed)).has_value() == false);
    auto expected = K_ASSET.m_metadata;
    ++expected.m_dependency.m_pose_revision.m_value;
    CHECK((read_depth_asset(K_PATH, *K_DIGEST, expected)).has_value() == false);
    CHECK(*asset_files::read_bytes(K_PATH, 1024, {}) == K_ORIGINAL);
    const auto K_INSPECTED = inspect_depth_file(K_PATH, *K_DIGEST);
    REQUIRE(K_INSPECTED);
    const auto K_SUMMARY = nlohmann::json::parse(*K_INSPECTED);
    CHECK(K_SUMMARY["producer"] == "SYNTHETIC_DIAGNOSTIC");
    CHECK(K_SUMMARY["frame"].get<std::uint64_t>() == 9007199254740993ULL);
    CHECK(K_SUMMARY["valid_pixels"] == 4);
    CHECK(K_SUMMARY["unknown_pixels"] == 2);
    CHECK(K_SUMMARY["uncertainty"] == "UNAVAILABLE");
    CHECK((write_depth_asset(root.m_path, K_ASSET)).has_value() == false);
    CHECK((write_depth_asset(root.m_path / "missing/image.wedepth", K_ASSET)).has_value() == false);
    CHECK(std::distance(std::filesystem::directory_iterator(root.m_path),
                        std::filesystem::directory_iterator {}) == 1);
}

TEST_CASE("depth pre cancellation preserves files planes and publication state", "[depth]") {
    Directory root;
    const auto K_ASSET = fixture();
    const auto K_BYTES = *encode_depth_asset(K_ASSET);
    std::stop_source source;
    source.request_stop();
    const auto K_TOKEN = source.get_token();
    const auto K_PATH = root.m_path / "cancelled.wedepth";
    const auto K_REQUIRE_CANCELLED = [](const auto& result) {
        REQUIRE(result.has_value() == false);
        CHECK(result.error().m_code == ErrorCode::CANCELLED);
    };
    K_REQUIRE_CANCELLED(encode_depth_asset(K_ASSET, {}, K_TOKEN));
    K_REQUIRE_CANCELLED(decode_depth_asset(K_BYTES, sha256(K_BYTES), {}, K_TOKEN));
    K_REQUIRE_CANCELLED(admit_depth_asset(K_ASSET, K_ASSET.m_metadata, {}, K_TOKEN));
    K_REQUIRE_CANCELLED(write_depth_asset(K_PATH, K_ASSET, {}, K_TOKEN));
    K_REQUIRE_CANCELLED(read_depth_asset(K_PATH, sha256(K_BYTES), K_ASSET.m_metadata, {}, K_TOKEN));
    K_REQUIRE_CANCELLED(inspect_depth_file(K_PATH, sha256(K_BYTES), {}, K_TOKEN));
    K_REQUIRE_CANCELLED(load_pose_bound_depth({}, {}, K_TOKEN));
    CHECK(std::filesystem::is_empty(root.m_path));
    CHECK(*encode_depth_asset(K_ASSET) == K_BYTES);
}

TEST_CASE("depth scalar wire tags never alias through narrowing", "[depth]") {
    const auto K_OFFSET = GENERATE(12uz, 16, 20);
    const auto K_ORIGINAL = *encode_depth_asset(fixture());
    auto bytes = K_ORIGINAL;
    put_u32(bytes, K_OFFSET, K_OFFSET == 20 ? 256 : 257);
    CHECK(decode_depth_asset(bytes, sha256(bytes)).has_value() == false);
}

TEST_CASE("depth admission verifies the independent canonical pose file", "[depth]") {
    Directory root;
    auto depth = fixture();
    MapStore store;
    MapTransaction transaction;
    transaction.m_new_revision = { 1 };
    transaction.m_operation = { 1 };
    const auto K_FRAME = depth.m_metadata.m_dependency.m_frame;
    transaction.m_calibrations = { { { 3 }, { 2, 4, 1, .5 }, 3, 2 } };
    transaction.m_observations = {
        { K_FRAME, { 1 }, { 3 }, { 0, 1, 30 }, std::chrono::nanoseconds { 0 }, std::string(64, 'b') } };
    transaction.m_alignments = { { { 1 }, { 1 }, {} } };
    transaction.m_poses = {
        { K_FRAME, { 1 }, {}, std::chrono::nanoseconds { 0 }, { 1 }, TrackingState::TRACKING, {} } };
    REQUIRE(store.commit(transaction));
    CanonicalPoseAsset poses {
        store.acquire_snapshot(),
        { std::string(64, '1'), std::string(64, '2'), std::string(64, '3'), {} } };
    const auto K_MUTATION = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
    if (K_MUTATION >= 7) {
        auto map = std::make_shared<MapState>(*poses.m_snapshot);
        if (K_MUTATION == 7) {
            map->m_invalidated_frames.insert(K_FRAME);
        }
        if (K_MUTATION == 8) {
            map->m_poses.at(K_FRAME).m_tracking = TrackingState::LOST;
        }
        if (K_MUTATION == 9) {
            map->m_poses.at(K_FRAME).m_tracking = TrackingState::INITIALIZING;
        }
        if (K_MUTATION == 10) {
            map->m_poses.at(K_FRAME).m_tracking = TrackingState::FINISHED;
        }
        poses.m_snapshot = map;
    }
    const auto K_POSE_PATH = root.m_path / "source.weposes";
    const auto K_POSE_DIGEST = write_pose_asset(K_POSE_PATH, poses);
    REQUIRE(K_POSE_DIGEST);
    depth.m_metadata.m_dependency.m_pose_asset_sha256 = *K_POSE_DIGEST;
    depth.m_metadata.m_dependency.m_epoch = { 0 };
    depth.m_metadata.m_dependency.m_pose_revision = { 1 };
    switch (K_MUTATION) {
    case 0:
    case 7:
    case 8:
    case 9:
    case 10:
        break;
    case 1:
        depth.m_metadata.m_dependency.m_observation_image_sha256 = std::string(64, 'e');
        break;
    case 2:
        ++depth.m_metadata.m_dependency.m_frame.m_value;
        break;
    case 3:
        ++depth.m_metadata.m_dependency.m_calibration.m_value;
        break;
    case 4:
        ++depth.m_metadata.m_dependency.m_epoch.m_value;
        break;
    case 5:
        ++depth.m_metadata.m_dependency.m_pose_revision.m_value;
        break;
    case 6:
        depth.m_metadata.m_dependency.m_pose_asset_sha256 = std::string(64, 'e');
        break;
    default:
        FAIL("Unknown dependency change");
    }
    const auto K_DEPTH_PATH = root.m_path / "image.wedepth";
    const auto K_DEPTH_DIGEST = write_depth_asset(K_DEPTH_PATH, depth);
    REQUIRE(K_DEPTH_DIGEST);
    const auto K_ADMITTED = load_pose_bound_depth(
        { K_DEPTH_PATH, K_POSE_PATH, *K_DEPTH_DIGEST, *K_POSE_DIGEST, depth.m_metadata });
    if (K_MUTATION == 0 || K_MUTATION == 10) {
        REQUIRE(K_ADMITTED);
        CHECK(K_ADMITTED->m_axial_depth == depth.m_axial_depth);
    } else {
        REQUIRE(K_ADMITTED.has_value() == false);
        const auto K_EXPECTED = K_MUTATION == 4   ? ErrorCode::WRONG_EPOCH
                                : K_MUTATION == 5 ? ErrorCode::WRONG_BASE
                                : (K_MUTATION == 2 || K_MUTATION == 6 || K_MUTATION >= 7)
                                    ? ErrorCode::RESYNC_REQUIRED
                                    : ErrorCode::INVALID_DATA;
        CHECK(K_ADMITTED.error().m_code == K_EXPECTED);
    }
    CHECK(*sha256_file(K_DEPTH_PATH, 4096) == *K_DEPTH_DIGEST);
    CHECK(*sha256_file(K_POSE_PATH, 65536) == *K_POSE_DIGEST);
}

TEST_CASE("depth invalid limits and symbolic links preserve existing caller files", "[depth]") {
    const auto K_MUTATION = GENERATE(0, 1, 2, 3, 4);
    DepthAssetLimits limits;
    switch (K_MUTATION) {
    case 0:
        limits.m_max_dimension = 0;
        break;
    case 1:
        limits.m_max_pixels = 0;
        break;
    case 2:
        limits.m_payload_bytes.m_bytes = 0;
        break;
    case 3:
        limits.m_encoded_bytes.m_bytes = std::numeric_limits<std::size_t>::max();
        break;
    case 4:
        limits.m_max_support_count = 0;
        break;
    default:
        FAIL("Unknown invalid limit");
    }
    const auto K_INVALID = encode_depth_asset(fixture(), limits);
    REQUIRE(K_INVALID.has_value() == false);
    CHECK(K_INVALID.error().m_code == ErrorCode::INVALID_ARGUMENT);
    Directory root;
    const auto K_PATH = root.m_path / "image.wedepth";
    const auto K_ASSET = fixture();
    const auto K_DIGEST = write_depth_asset(K_PATH, K_ASSET);
    REQUIRE(K_DIGEST);
    const auto K_LINK = root.m_path / "link.wedepth";
    std::error_code error;
    std::filesystem::create_symlink(K_PATH, K_LINK, error);
#ifdef _WIN32
    if (error == std::errc::operation_not_permitted || error == std::errc::permission_denied)
        SKIP("Native symbolic-link creation unavailable to this Windows account");
#endif
    REQUIRE(!error);
    CHECK(read_depth_asset(K_LINK, *K_DIGEST, K_ASSET.m_metadata).has_value() == false);
    CHECK(write_depth_asset(K_LINK, K_ASSET).has_value() == false);
    CHECK(*sha256_file(K_PATH, 4096) == *K_DIGEST);
    CHECK(std::filesystem::is_symlink(K_LINK));
}
