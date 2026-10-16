#include "world_explorer/application/depth_assets.hpp"
#include "world_explorer/core/asset_files.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/map_store/pose_asset.hpp"
#include <algorithm>
#include <atomic>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <chrono>
#include <fstream>
#include <iterator>
#include <limits>
#include <nlohmann/json.hpp>

using namespace world_explorer;
namespace {
depth_image_asset fixture() {
    depth_image_asset asset;
    asset.metadata.dependency = {
        {7}, {9}, {9007199254740993ULL}, {3}, std::string(64, 'a'), std::string(64, 'b')};
    asset.metadata.width = 3;
    asset.metadata.height = 2;
    asset.metadata.camera = {2, 4, 1, .5};
    asset.metadata.calibrated_image_sha256 = std::string(64, 'c');
    asset.metadata.recipe_sha256 = std::string(64, 'd');
    asset.axial_depth = {2, 0, 4, 1, 8, 0};
    asset.valid = {1, 0, 1, 1, 1, 0};
    asset.confidence = {.5, 0, 1, 0, .25, 0};
    asset.support_count = {2, 0, 4, 1, 3, 0};
    return asset;
}
struct directory {
    std::filesystem::path path;
    directory() {
        static std::atomic<unsigned> sequence{};
        path = std::filesystem::temp_directory_path() /
               ("we_depth_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
                std::to_string(sequence++));
        REQUIRE(std::filesystem::create_directory(path));
    }
    ~directory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};
// Fixed wire offsets are independent of the production encoder's implementation.
void put_u32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        bytes.at(offset + i) = static_cast<std::byte>(value & 255U);
        value >>= 8U;
    }
}
} // namespace

TEST_CASE("depth planes preserve explicit unknowns camera units and wide frame identities",
          "[depth]") {
    const auto asset = fixture();
    const auto encoded = encode_depth_asset(asset);
    REQUIRE(encoded);
    CHECK(encoded->size() == 356 + 6 * 13);
    const auto decoded = decode_depth_asset(*encoded, sha256(*encoded));
    REQUIRE(decoded);
    CHECK(decoded->metadata.dependency.frame.value == 9007199254740993ULL);
    CHECK(decoded->metadata.camera.cy == .5);
    CHECK(decoded->axial_depth == asset.axial_depth);
    CHECK(decoded->valid == asset.valid);
    CHECK(decoded->confidence == asset.confidence);
    CHECK(decoded->support_count == asset.support_count);
    CHECK((decoded->standard_deviation).has_value() == false);
    CHECK(admit_depth_asset(*decoded, asset.metadata));
    CHECK(*encode_depth_asset(*decoded) == *encoded);
    std::vector<std::byte> unaligned(encoded->size() + 1);
    std::copy(encoded->begin(), encoded->end(), unaligned.begin() + 1);
    CHECK(decode_depth_asset(std::span(unaligned).subspan(1), sha256(*encoded)));
    // Off-axis axial Z=4 is (2,-.5,4), whose range is greater than 4.
    const auto point = unproject(decoded->metadata.camera, {2, 0}, decoded->axial_depth[2]);
    REQUIRE(point);
    CHECK(*point == vec3d{2, -.5, 4});
    CHECK(std::sqrt(dot(*point, *point)) > 4);
    CHECK((*project(decoded->metadata.camera, *point))[0] == 2);
}

TEST_CASE("depth wire planes have independent little endian scalar oracles", "[depth]") {
    auto asset = fixture();
    asset.standard_deviation = std::vector<float>{.25, 0, .5, 0, 1, 0};
    const auto bytes = encode_depth_asset(asset);
    REQUIRE(bytes);
    CHECK(bytes->size() == 356 + 6 * 17);
    // First depth=2: IEEE 0x40000000; validity begins after six floats.
    CHECK(bytes->at(356) == std::byte{0});
    CHECK(bytes->at(359) == std::byte{0x40});
    CHECK(bytes->at(380) == std::byte{1});
    CHECK(bytes->at(381) == std::byte{0});
    // Confidence=.5 at 386; support=2 at 410; sigma=.25 at 434.
    CHECK(bytes->at(389) == std::byte{0x3f});
    CHECK(bytes->at(410) == std::byte{2});
    CHECK(bytes->at(437) == std::byte{0x3e});
    const auto decoded = decode_depth_asset(*bytes, sha256(*bytes));
    REQUIRE(decoded);
    CHECK(decoded->standard_deviation == asset.standard_deviation);
    CHECK(decoded->metadata.producer == depth_producer_kind::SYNTHETIC_DIAGNOSTIC);
}

TEST_CASE(
    "depth rejects rehashed unsupported semantics dimensions and identities before allocation",
    "[depth]") {
    const auto offset = GENERATE(std::size_t{8}, 12, 16, 20, 24, 28, 32, 100);
    const auto value = GENERATE(0U, 0xffffffffU);
    const auto original = encode_depth_asset(fixture());
    REQUIRE(original);
    auto bytes = *original;
    put_u32(bytes, offset, value);
    const auto decoded = decode_depth_asset(bytes, sha256(bytes));
    // Scale=0 is its existing arbitrary-unit encoding; uncertainty=0 is absent.
    if ((offset == 20 || offset == 32) && value == 0)
        CHECK(decoded);
    else
        CHECK((decoded).has_value() == false);
    CHECK(*encode_depth_asset(fixture()) == *original);
}

TEST_CASE("depth rejects rehashed malformed samples in every serialized plane", "[depth]") {
    const auto offset = GENERATE(std::size_t{356}, 386, 410, 434);
    auto asset = fixture();
    asset.standard_deviation = std::vector<float>{.25, 0, .5, .125, 1, 0};
    const auto original = encode_depth_asset(asset);
    REQUIRE(original);
    auto bytes = *original;
    put_u32(bytes, offset, 0x7fc00000U);
    CHECK((decode_depth_asset(bytes, sha256(bytes))).has_value() == false);
    bytes = *original;
    bytes.at(380) = std::byte{2};
    CHECK((decode_depth_asset(bytes, sha256(bytes))).has_value() == false);
}

TEST_CASE("depth validates sample support masks confidence and optional uncertainty", "[depth]") {
    auto asset = fixture();
    const auto original = *encode_depth_asset(asset);
    const auto mutation = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11);
    switch (mutation) {
    case 0:
        asset.axial_depth[0] = 0;
        break;
    case 1:
        asset.axial_depth[1] = 1;
        break;
    case 2:
        asset.axial_depth[1] = -0.0F;
        break;
    case 3:
        asset.confidence[0] = 1.01F;
        break;
    case 4:
        asset.confidence[1] = .1F;
        break;
    case 5:
        asset.support_count[0] = 0;
        break;
    case 6:
        asset.support_count[1] = 1;
        break;
    case 7:
        asset.support_count[0] = 1000001;
        break;
    case 8:
        asset.confidence.pop_back();
        break;
    case 9:
        asset.standard_deviation = std::vector<float>(5);
        break;
    case 10:
        asset.standard_deviation = std::vector<float>{-1, 0, .5, 0, 1, 0};
        break;
    case 11:
        asset.standard_deviation = std::vector<float>{1, 1, 1, 1, 1, 1};
        break;
    default:
        FAIL("Unknown mutation");
    }
    const auto rejected = encode_depth_asset(asset);
    REQUIRE((rejected).has_value() == false);
    CHECK(rejected.error().code == error_code::INVALID_DATA);
    CHECK(*encode_depth_asset(fixture()) == original);
}

TEST_CASE("depth enforces independent exact pixel payload and encoded byte budgets", "[depth]") {
    const auto asset = fixture();
    depth_asset_limits exact;
    exact.max_dimension = 3;
    exact.max_pixels = 6;
    exact.payload_bytes.bytes = 78;
    exact.encoded_bytes.bytes = 434;
    const auto encoded = encode_depth_asset(asset, exact);
    REQUIRE(encoded);
    REQUIRE(decode_depth_asset(*encoded, sha256(*encoded), exact));
    const auto limit = GENERATE(0, 1, 2, 3, 4);
    switch (limit) {
    case 0:
        --exact.max_pixels;
        break;
    case 1:
        --exact.max_dimension;
        break;
    case 2:
        --exact.payload_bytes.bytes;
        break;
    case 3:
        --exact.encoded_bytes.bytes;
        break;
    case 4:
        exact.max_support_count = 3;
        break;
    default:
        FAIL("Unknown budget");
    }
    CHECK((encode_depth_asset(asset, exact)).has_value() == false);
    CHECK((decode_depth_asset(*encoded, sha256(*encoded), exact)).has_value() == false);
}

TEST_CASE("depth checksum version truncation and trailing data fail without caller mutation",
          "[depth]") {
    const auto original = *encode_depth_asset(fixture());
    for (std::size_t length = 0; length < original.size(); ++length) {
        const auto bytes = std::span(original).first(length);
        CHECK((decode_depth_asset(bytes, sha256(bytes))).has_value() == false);
    }
    auto appended = original;
    appended.push_back(std::byte{0});
    CHECK((decode_depth_asset(appended, sha256(appended))).has_value() == false);
    CHECK((decode_depth_asset(original, std::string(64, 'f'))).has_value() == false);
    CHECK((decode_depth_asset(original, "INVALID")).has_value() == false);
    CHECK(*encode_depth_asset(fixture()) == original);
}

TEST_CASE("depth wrong epoch revision and dependency require resynchronization", "[depth]") {
    const auto asset = fixture();
    const auto original = *encode_depth_asset(asset);
    auto expected = asset.metadata;
    const auto mutation = GENERATE(0, 1, 2);
    const auto code = mutation == 0   ? error_code::WRONG_EPOCH
                      : mutation == 1 ? error_code::WRONG_BASE
                                      : error_code::RESYNC_REQUIRED;
    if (mutation == 0)
        ++expected.dependency.epoch.value;
    if (mutation == 1)
        ++expected.dependency.pose_revision.value;
    if (mutation == 2)
        expected.dependency.pose_asset_sha256 = std::string(64, 'e');
    const auto rejected = admit_depth_asset(asset, expected);
    REQUIRE((rejected).has_value() == false);
    CHECK(rejected.error().code == code);
    CHECK(*encode_depth_asset(asset) == original);
}

TEST_CASE("depth admission binds the full calibrated domain recipe and source observation",
          "[depth]") {
    const auto asset = fixture();
    auto expected = asset.metadata;
    const auto mutation = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
    switch (mutation) {
    case 0:
        ++expected.dependency.frame.value;
        break;
    case 1:
        ++expected.dependency.calibration.value;
        break;
    case 2:
        expected.dependency.observation_image_sha256 = std::string(64, 'e');
        break;
    case 3:
        expected.calibrated_image_sha256 = std::string(64, 'e');
        break;
    case 4:
        expected.recipe_sha256 = std::string(64, 'e');
        break;
    case 5:
        expected.camera.cx += .5;
        break;
    case 6:
        expected.width = 2;
        break;
    case 7:
        expected.producer = depth_producer_kind::MULTIVIEW_STEREO;
        break;
    default:
        FAIL("Unknown metadata change");
    }
    CHECK((admit_depth_asset(asset, expected)).has_value() == false);
}

TEST_CASE("depth metric range and invalid limit claims remain unsupported", "[depth]") {
    auto asset = fixture();
    SECTION("ray range") { asset.metadata.convention = depth_convention::RAY_RANGE; }
    SECTION("measured units") { asset.metadata.scale = scale_status::METRIC_MEASURED; }
    const auto checked = encode_depth_asset(asset);
    REQUIRE((checked).has_value() == false);
    CHECK(checked.error().code == error_code::UNSUPPORTED_OPERATION);
}

TEST_CASE("depth file publication preserves existing entries and admission failures", "[depth]") {
    directory root;
    const auto asset = fixture();
    const auto path = root.path / "image.wedepth";
    const auto digest = write_depth_asset(path, asset);
    REQUIRE(digest);
    const auto loaded = read_depth_asset(path, *digest, asset.metadata);
    REQUIRE(loaded);
    const auto original = *asset_files::read_bytes(path, 1024, {});
    auto changed = asset;
    changed.axial_depth[0] = 3;
    CHECK((write_depth_asset(path, changed)).has_value() == false);
    auto expected = asset.metadata;
    ++expected.dependency.pose_revision.value;
    CHECK((read_depth_asset(path, *digest, expected)).has_value() == false);
    CHECK(*asset_files::read_bytes(path, 1024, {}) == original);
    const auto inspected = inspect_depth_file(path, *digest);
    REQUIRE(inspected);
    const auto summary = nlohmann::json::parse(*inspected);
    CHECK(summary["producer"] == "SYNTHETIC_DIAGNOSTIC");
    CHECK(summary["frame"].get<std::uint64_t>() == 9007199254740993ULL);
    CHECK(summary["valid_pixels"] == 4);
    CHECK(summary["unknown_pixels"] == 2);
    CHECK(summary["uncertainty"] == "UNAVAILABLE");
    CHECK((write_depth_asset(root.path, asset)).has_value() == false);
    CHECK((write_depth_asset(root.path / "missing/image.wedepth", asset)).has_value() == false);
    CHECK(std::distance(std::filesystem::directory_iterator(root.path),
                        std::filesystem::directory_iterator{}) == 1);
}

TEST_CASE("depth pre cancellation preserves files planes and publication state", "[depth]") {
    directory root;
    const auto asset = fixture();
    const auto bytes = *encode_depth_asset(asset);
    std::stop_source source;
    source.request_stop();
    const auto token = source.get_token();
    const auto path = root.path / "cancelled.wedepth";
    const auto require_cancelled = [](const auto& result) {
        REQUIRE(result.has_value() == false);
        CHECK(result.error().code == error_code::CANCELLED);
    };
    require_cancelled(encode_depth_asset(asset, {}, token));
    require_cancelled(decode_depth_asset(bytes, sha256(bytes), {}, token));
    require_cancelled(admit_depth_asset(asset, asset.metadata, {}, token));
    require_cancelled(write_depth_asset(path, asset, {}, token));
    require_cancelled(read_depth_asset(path, sha256(bytes), asset.metadata, {}, token));
    require_cancelled(inspect_depth_file(path, sha256(bytes), {}, token));
    require_cancelled(load_pose_bound_depth({}, {}, token));
    CHECK(std::filesystem::is_empty(root.path));
    CHECK(*encode_depth_asset(asset) == bytes);
}

TEST_CASE("depth scalar wire tags never alias through narrowing", "[depth]") {
    const auto offset = GENERATE(std::size_t{12}, 16, 20);
    const auto original = *encode_depth_asset(fixture());
    auto bytes = original;
    put_u32(bytes, offset, offset == 20 ? 256 : 257);
    CHECK(decode_depth_asset(bytes, sha256(bytes)).has_value() == false);
}

TEST_CASE("depth admission verifies the independent canonical pose file", "[depth]") {
    directory root;
    auto depth = fixture();
    map_store store;
    map_transaction transaction;
    transaction.new_revision = {1};
    transaction.operation = {1};
    const auto frame = depth.metadata.dependency.frame;
    transaction.calibrations = {{{3}, {2, 4, 1, .5}, 3, 2}};
    transaction.observations = {
        {frame, {1}, {3}, {0, 1, 30}, std::chrono::nanoseconds{0}, std::string(64, 'b')}};
    transaction.alignments = {{{1}, {1}, {}}};
    transaction.poses = {
        {frame, {1}, {}, std::chrono::nanoseconds{0}, {1}, tracking_state::TRACKING, {}}};
    REQUIRE(store.commit(transaction));
    canonical_pose_asset poses{
        store.acquire_snapshot(),
        {std::string(64, '1'), std::string(64, '2'), std::string(64, '3'), {}}};
    const auto mutation = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
    if (mutation >= 7) {
        auto map = std::make_shared<map_state>(*poses.snapshot);
        if (mutation == 7)
            map->invalidated_frames.insert(frame);
        if (mutation == 8)
            map->poses.at(frame).tracking = tracking_state::LOST;
        if (mutation == 9)
            map->poses.at(frame).tracking = tracking_state::INITIALIZING;
        if (mutation == 10)
            map->poses.at(frame).tracking = tracking_state::FINISHED;
        poses.snapshot = map;
    }
    const auto pose_path = root.path / "source.weposes";
    const auto pose_digest = write_pose_asset(pose_path, poses);
    REQUIRE(pose_digest);
    depth.metadata.dependency.pose_asset_sha256 = *pose_digest;
    depth.metadata.dependency.epoch = {0};
    depth.metadata.dependency.pose_revision = {1};
    switch (mutation) {
    case 0:
    case 7:
    case 8:
    case 9:
    case 10:
        break;
    case 1:
        depth.metadata.dependency.observation_image_sha256 = std::string(64, 'e');
        break;
    case 2:
        ++depth.metadata.dependency.frame.value;
        break;
    case 3:
        ++depth.metadata.dependency.calibration.value;
        break;
    case 4:
        ++depth.metadata.dependency.epoch.value;
        break;
    case 5:
        ++depth.metadata.dependency.pose_revision.value;
        break;
    case 6:
        depth.metadata.dependency.pose_asset_sha256 = std::string(64, 'e');
        break;
    default:
        FAIL("Unknown dependency change");
    }
    const auto depth_path = root.path / "image.wedepth";
    const auto depth_digest = write_depth_asset(depth_path, depth);
    REQUIRE(depth_digest);
    const auto admitted =
        load_pose_bound_depth({depth_path, pose_path, *depth_digest, *pose_digest, depth.metadata});
    if (mutation == 0 || mutation == 10) {
        REQUIRE(admitted);
        CHECK(admitted->axial_depth == depth.axial_depth);
    } else {
        REQUIRE(admitted.has_value() == false);
        const auto expected = mutation == 4   ? error_code::WRONG_EPOCH
                              : mutation == 5 ? error_code::WRONG_BASE
                              : (mutation == 2 || mutation == 6 || mutation >= 7)
                                  ? error_code::RESYNC_REQUIRED
                                  : error_code::INVALID_DATA;
        CHECK(admitted.error().code == expected);
    }
    CHECK(*sha256_file(depth_path, 4096) == *depth_digest);
    CHECK(*sha256_file(pose_path, 65536) == *pose_digest);
}

TEST_CASE("depth invalid limits and symbolic links preserve existing caller files", "[depth]") {
    const auto mutation = GENERATE(0, 1, 2, 3, 4);
    depth_asset_limits limits;
    switch (mutation) {
    case 0:
        limits.max_dimension = 0;
        break;
    case 1:
        limits.max_pixels = 0;
        break;
    case 2:
        limits.payload_bytes.bytes = 0;
        break;
    case 3:
        limits.encoded_bytes.bytes = std::numeric_limits<std::size_t>::max();
        break;
    case 4:
        limits.max_support_count = 0;
        break;
    default:
        FAIL("Unknown invalid limit");
    }
    const auto invalid = encode_depth_asset(fixture(), limits);
    REQUIRE(invalid.has_value() == false);
    CHECK(invalid.error().code == error_code::INVALID_ARGUMENT);
    directory root;
    const auto path = root.path / "image.wedepth";
    const auto asset = fixture();
    const auto digest = write_depth_asset(path, asset);
    REQUIRE(digest);
    const auto link = root.path / "link.wedepth";
    std::error_code error;
    std::filesystem::create_symlink(path, link, error);
#ifdef _WIN32
    if (error == std::errc::operation_not_permitted || error == std::errc::permission_denied)
        SKIP("Native symbolic-link creation unavailable to this Windows account");
#endif
    REQUIRE(!error);
    CHECK(read_depth_asset(link, *digest, asset.metadata).has_value() == false);
    CHECK(write_depth_asset(link, asset).has_value() == false);
    CHECK(*sha256_file(path, 4096) == *digest);
    CHECK(std::filesystem::is_symlink(link));
}
