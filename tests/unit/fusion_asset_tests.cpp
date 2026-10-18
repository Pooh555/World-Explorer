#include "world_explorer/application/fusion_assets.hpp"
#include "world_explorer/core/asset_files.hpp"
#include "world_explorer/core/hash.hpp"
#include "world_explorer/map_store/pose_asset.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <limits>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <nlohmann/json.hpp>

using namespace world_explorer;

namespace {

using Json = nlohmann::json;

std::string text_hash(const std::string& text) {
    return sha256(std::as_bytes(std::span(text)));
}

template <std::size_t Width> void integer(std::vector<std::byte>& bytes, std::uint64_t value) {
    for (std::size_t i { 0uz }; i < Width; ++i) {
        bytes.push_back(static_cast<std::byte>(value & 255U));
        value >>= 8U;
    }
}

void real(std::vector<std::byte>& bytes, float value) {
    integer<4uz>(bytes, std::bit_cast<std::uint32_t>(value));
}

template <std::size_t Width = 4uz>
void put(std::vector<std::byte>& bytes, std::size_t offset, std::uint64_t value) {
    for (std::size_t i { 0uz }; i < Width; ++i) {
        bytes.at(offset + i) = static_cast<std::byte>(value & 255U);
        value >>= 8U;
    }
}

struct Fixture {
    Json m_manifest, m_receipt, m_recipe;
    FusionContext m_context;
    std::vector<std::byte> m_payload;

    Fixture() {
        m_context.m_pose_sha256 = std::string(64uz, 'a');
        m_context.m_epoch = { 0 };
        m_context.m_pose_revision = { 1 };
        m_receipt = {{ "schema_version", 1U },
                     { "kind", "POSE_ADMITTED_DEPTH_VISIBILITY_V1" },
                     { "units", "ARBITRARY_POSE_CAMERA_UNITS" },
                     { "depth_convention", "AXIAL_CAMERA_Z" },
                     { "camera_axes", "+x_right,+y_down,+z_forward" },
                     { "pixel_centers", "integer" },
                     { "confidence", "RECIPE_DEFINED_RELATIVE_WEIGHT_NOT_PROBABILITY" },
                     { "world_epoch", 0U },
                     { "pose_revision", 1U },
                     {"frames",
                      Json::array({{{ "frame", 9007199254740993ULL },
                                    { "calibration", 3U },
                                    { "observation_image_sha256", std::string(64uz, 'b') },
                                    { "width", 2U },
                                    { "height", 2U },
                                    { "valid_pixels", 3U },
                                    { "unknown_pixels", 1U },
                                    { "image", "images/one.png" },
                                    { "uncertainty", "CAMERA_UNIT_STANDARD_DEVIATION" }}})}};
        m_receipt["frames"][0]["world_from_camera"] = {{"rotation_wxyz", { 1.0, 0.0, 0.0, 0.0 }},
                                                       {"translation", { 0.0, 0.0, 0.0 }},
                                                       { "scale", 1.0 }};
        m_recipe = {{ "schema_version", 1U },
                    { "kind", "OBSERVED_TSDF_CPU_PILOT_V1" },
                    { "units", "ARBITRARY_POSE_CAMERA_UNITS" },
                    { "scope", "PARTIAL_OBSERVED_GEOMETRY_NO_Q5_N2_OR_WALK" },
                    { "free_space", "OBSERVED_POSITIVE_TSDF_IN_LOCAL_TRUNCATION_BAND_ONLY" },
                    { "weight", "REFERENCE_CONFIDENCE_ONLY_NOT_PROBABILITY" },
                    { "voxel_divisor", 512U },
                    { "truncation_voxels", 4U },
                    { "block_resolution", 8U },
                    { "minimum_consistent_sources", 2U },
                    { "relative_depth_error", .01 },
                    { "roundtrip_pixels", 1.0 },
                    { "reject_known_occlusion", true }};
        m_manifest = {
            { "schema_version", 1U },
            { "kind", "PARTIAL_OBSERVED_TSDF_V1" },
            { "scope", "PARTIAL_OBSERVED_GEOMETRY_NO_Q5_N2_OR_WALK" },
            { "units", "ARBITRARY_POSE_CAMERA_UNITS" },
            { "world_axes", "RIGHT_HANDED_Y_UP" },
            { "depth_convention", "AXIAL_CAMERA_Z" },
            { "walking_enabled", false },
            { "free_space", "OBSERVED_POSITIVE_TSDF_IN_LOCAL_TRUNCATION_BAND_ONLY" },
            { "unknown", "ABSENT_LATTICE_ENTRIES_AND_ZERO_WEIGHT_ARE_UNKNOWN; NO_FULL_RAY_CARVING" },
            {"uncertainty",
             "WEIGHTED_RMS_INPUT_SIGMA_WHERE_SIGMA_WEIGHT_POSITIVE; NOT_SURFACE_ERROR"},
            { "mesh_policy", "RAW_WEIGHT_SUPPORTED_EXTRACTION; NO_FILL_OR_SIMPLIFICATION" },
            { "radiance", "NO_COLOR_OR_RELIGHTABLE_ALBEDO_PRODUCT" },
            { "world_epoch", 0U },
            { "pose_revision", 1U },
            { "extent", 512.0 },
            { "voxel_size", 1.0 },
            { "truncation", 4.0 },
            { "extent_samples", 3U },
            { "observed_voxels", 2U },
            { "vertices", 3U },
            { "triangles", 1U },
            { "block_resolution", 8U },
            { "allocated_blocks", 1U },
            { "unknown_allocated_voxels", 510U },
            { "front_band_voxels", 1U },
            { "sigma_available_voxels", 1U },
            {"coverage",
             Json::array({{{ "name", "one.png" },
                           { "pixels", 4U },
                           { "valid", 3U },
                           { "unknown", 1U },
                           { "accepted", 2U },
                           { "rejected_positive_weight", 1U },
                           { "zero_confidence_valid", 0U },
                           { "enough_consistent_sources", 2U },
                           { "known_occlusion", 1U },
                           { "uncertainty", "CAMERA_UNIT_STANDARD_DEVIATION" }}})}};
        for (std::size_t i { 0uz }; i < 2uz; ++i) {
            integer<4uz>(m_payload, i);
            integer<4uz>(m_payload, 0);
            integer<4uz>(m_payload, 2);
            real(m_payload, i == 0uz ? -.25F : .5F);
            real(m_payload, .5F);
            integer<4uz>(m_payload, 1);
            integer<4uz>(m_payload, 3);
            integer<4uz>(m_payload, i);
            integer<8uz>(m_payload, std::bit_cast<std::uint64_t>(i == 0uz ? .125 : 0.0));
            real(m_payload, i == 0uz ? .5F : 0.0F);
        }
        for (std::size_t i { 0uz }; i < 3uz; ++i) {
            for (const auto K_VALUE : { static_cast<float>(i), 0.0F, 2.0F, 0.0F, 1.0F, 0.0F })
                real(m_payload, K_VALUE);
        }
        for (std::size_t i { 0uz }; i < 3uz; ++i) integer<4uz>(m_payload, i);
    }

    std::vector<std::byte> encode() {
        m_receipt["pose_asset_sha256"] = m_context.m_pose_sha256;
        m_receipt["files"]["poses.weposes"]["sha256"] = m_context.m_pose_sha256;
        const auto K_RECEIPT = m_receipt.dump();
        const auto K_RECIPE = m_recipe.dump();
        m_context.m_receipt_sha256 = text_hash(K_RECEIPT);
        m_context.m_recipe_sha256 = text_hash(K_RECIPE);
        const Json K_REQUEST {{ "schema_version", 1U },
                              { "cache", "unused" },
                              { "recipe", "unused" },
                              { "receipt_sha256", m_context.m_receipt_sha256 },
                              { "recipe_sha256", m_context.m_recipe_sha256 }};
        const auto K_REQUEST_TEXT = K_REQUEST.dump();
        m_manifest["request_sha256"] = text_hash(K_REQUEST_TEXT);
        m_manifest["receipt_sha256"] = m_context.m_receipt_sha256;
        m_manifest["recipe_sha256"] = m_context.m_recipe_sha256;
        m_manifest["pose_asset_sha256"] = m_context.m_pose_sha256;
        const auto K_MANIFEST = m_manifest.dump();
        m_context.m_manifest_sha256 = text_hash(K_MANIFEST);
        const Json K_ENVELOPE {{ "manifest", K_MANIFEST },
                               { "receipt", K_RECEIPT },
                               { "recipe", K_RECIPE },
                               { "request", K_REQUEST_TEXT }};
        const auto K_TEXT = K_ENVELOPE.dump();
        std::vector<std::byte> bytes;
        for (const char K_C : std::string("WEFUSN01")) bytes.push_back(static_cast<std::byte>(K_C));
        integer<4uz>(bytes, K_TEXT.size());
        integer<8uz>(bytes, 2);
        integer<8uz>(bytes, 3);
        integer<8uz>(bytes, 1);
        const auto K_TEXT_BYTES = std::as_bytes(std::span(K_TEXT));
        bytes.insert(bytes.end(), K_TEXT_BYTES.begin(), K_TEXT_BYTES.end());
        bytes.insert(bytes.end(), m_payload.begin(), m_payload.end());
        return bytes;
    }
};

}  // namespace

TEST_CASE("fusion preserves explicit sparse evidence and wide identities", "[fusion]") {
    Fixture fixture;
    const auto K_BYTES = fixture.encode();
    const auto K_ASSET = decode_fusion_asset(K_BYTES, sha256(K_BYTES), fixture.m_context);
    REQUIRE(K_ASSET);
    REQUIRE(K_ASSET->m_voxels.size() == 2uz);
    CHECK(K_ASSET->m_voxels[0].m_lattice == std::array<std::int32_t, 3> { 0, 0, 2 });
    CHECK(K_ASSET->m_voxels[0].m_tsdf == -.25F);
    CHECK(K_ASSET->m_voxels[0].m_weight == .5F);
    CHECK(K_ASSET->m_voxels[0].m_observation_count == 1);
    CHECK(K_ASSET->m_voxels[0].m_support_sum == 3);
    CHECK(K_ASSET->m_voxels[0].m_sigma2_sum == .125);
    CHECK(K_ASSET->m_voxels[0].m_sigma_weight == .5F);
    CHECK(K_ASSET->m_voxels[1].m_sigma_weight == 0);
    CHECK(K_ASSET->m_voxels[1].m_front_count == 1);
    CHECK(K_ASSET->m_unknown_allocated_voxels == 510uz);
    CHECK(K_ASSET->m_vertices[2].m_position[0] == 2);
    CHECK(K_ASSET->m_triangles[0] == std::array<std::uint32_t, 3> { 0, 1, 2 });
    const auto K_SUMMARY = inspect_fusion_asset(*K_ASSET);
    REQUIRE(K_SUMMARY);
    const auto K_JSON = Json::parse(*K_SUMMARY);
    CHECK(K_JSON.at("walking_enabled") == false);
    CHECK(K_JSON.at("asset_sha256") == sha256(K_BYTES));
    CHECK(K_JSON.at("coverage")[0].at("pixels") == 4);
    std::vector<std::byte> unaligned {std::byte { 0 }};
    unaligned.insert(unaligned.end(), K_BYTES.begin(), K_BYTES.end());
    CHECK(
        decode_fusion_asset(std::span(unaligned).subspan(1uz), sha256(K_BYTES), fixture.m_context));
}

TEST_CASE("fusion rejects truncated trailing oversized and versioned wire inputs", "[fusion]") {
    Fixture fixture;
    auto bytes = fixture.encode();
    const auto K_MUTATION = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
    switch (K_MUTATION) {
    case 0:
        bytes.clear();
        break;
    case 1:
        bytes.pop_back();
        break;
    case 2:
        bytes.push_back(std::byte {});
        break;
    case 3:
        bytes[7] = std::byte { '2' };
        break;
    case 4:
        put(bytes, 8uz, 4194305);
        break;
    case 5:
        put<8uz>(bytes, 12uz, UINT64_MAX);
        break;
    case 6:
        put<8uz>(bytes, 20uz, UINT64_MAX);
        break;
    case 7:
        put<8uz>(bytes, 28uz, UINT64_MAX);
        break;
    default:
        FAIL("Unknown mutation");
    }
    CHECK((decode_fusion_asset(bytes, sha256(bytes), fixture.m_context)).has_value() == false);
}

TEST_CASE("fusion rejects rehashed malformed voxel evidence in every field", "[fusion]") {
    Fixture fixture;
    // Wire offsets within one 44-byte voxel are independent of the production decoder.
    const auto K_MUTATION = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13);
    switch (K_MUTATION) {
    case 0:
        put(fixture.m_payload, 0uz, 1048577);
        break;
    case 1:
        put(fixture.m_payload, 12uz, 0x7fc00000U);
        break;
    case 2:
        put(fixture.m_payload, 12uz, std::bit_cast<std::uint32_t>(1.1F));
        break;
    case 3:
        put(fixture.m_payload, 16uz, 0);
        break;
    case 4:
        put(fixture.m_payload, 16uz, 0x7f800000U);
        break;
    case 5:
        put(fixture.m_payload, 20uz, 0);
        break;
    case 6:
        put(fixture.m_payload, 20uz, 2);
        break;
    case 7:
        put(fixture.m_payload, 24uz, 0);
        break;
    case 8:
        put(fixture.m_payload, 24uz, 1000001);
        break;
    case 9:
        put(fixture.m_payload, 28uz, 2);
        break;
    case 10:
        put<8uz>(fixture.m_payload, 32uz, 0x7ff8000000000000ULL);
        break;
    case 11:
        put(fixture.m_payload, 40uz, 0);
        break;
    case 12:
        put(fixture.m_payload, 40uz, std::bit_cast<std::uint32_t>(1.0F));
        break;
    case 13:
        put(fixture.m_payload, 44uz, 0);
        break;
    default:
        FAIL("Unknown mutation");
    }
    const auto K_BYTES = fixture.encode();
    CHECK((decode_fusion_asset(K_BYTES, sha256(K_BYTES), fixture.m_context)).has_value() == false);
}

TEST_CASE("fusion rejects rehashed mesh normals positions and indices", "[fusion]") {
    Fixture fixture;
    const auto K_MUTATION = GENERATE(0, 1, 2, 3, 4);
    switch (K_MUTATION) {
    case 0:
        put(fixture.m_payload, 88uz, 0x7fc00000U);
        break;
    case 1:
        put(fixture.m_payload, 100uz, 0x7f800000U);
        break;
    case 2:
        put(fixture.m_payload, 100uz, std::bit_cast<std::uint32_t>(2.0F));
        break;
    case 3:
        put(fixture.m_payload, 160uz, 3);
        break;
    case 4:
        put(fixture.m_payload, 160uz, 1);
        break;
    default:
        FAIL("Unknown mutation");
    }
    const auto K_BYTES = fixture.encode();
    CHECK((decode_fusion_asset(K_BYTES, sha256(K_BYTES), fixture.m_context)).has_value() == false);
}

TEST_CASE("fusion rejects rehashed metadata coverage semantics and resolution", "[fusion]") {
    Fixture fixture;
    const auto K_MUTATION = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    switch (K_MUTATION) {
    case 0:
        fixture.m_manifest["walking_enabled"] = true;
        break;
    case 1:
        fixture.m_manifest["units"] = "METRES";
        break;
    case 2:
        fixture.m_manifest["voxel_size"] = .5;
        break;
    case 3:
        fixture.m_manifest["observed_voxels"] = 3U;
        break;
    case 4:
        fixture.m_manifest["unknown_allocated_voxels"] = 511U;
        break;
    case 5:
        fixture.m_manifest["sigma_available_voxels"] = 0U;
        break;
    case 6:
        fixture.m_manifest["front_band_voxels"] = 0U;
        break;
    case 7:
        fixture.m_manifest["coverage"][0]["accepted"] = 3U;
        break;
    case 8:
        fixture.m_manifest["coverage"][0]["unknown"] = 0U;
        break;
    case 9:
        fixture.m_manifest["coverage"].clear();
        break;
    case 10:
        fixture.m_manifest["coverage"][0]["valid"] = -1;
        break;
    case 11:
        fixture.m_manifest["extent_samples"] = 2U;
        break;
    case 12:
        fixture.m_recipe["minimum_consistent_sources"] = 1U;
        break;
    case 13:
        fixture.m_receipt["frames"][0]["image"] = "images/other.png";
        break;
    case 14:
        fixture.m_manifest["uncertainty"] = "SURFACE_ERROR";
        break;
    case 15:
        fixture.m_manifest["unknown"] = "FREE_SPACE";
        break;
    default:
        FAIL("Unknown mutation");
    }
    const auto K_BYTES = fixture.encode();
    CHECK((decode_fusion_asset(K_BYTES, sha256(K_BYTES), fixture.m_context)).has_value() == false);
}

TEST_CASE("fusion independently pins all documents and current epoch revision", "[fusion]") {
    Fixture fixture;
    const auto K_BYTES = fixture.encode();
    auto expected = fixture.m_context;
    const auto K_MUTATION = GENERATE(0, 1, 2, 3, 4, 5, 6);
    switch (K_MUTATION) {
    case 0:
        expected.m_manifest_sha256 = std::string(64uz, 'b');
        break;
    case 1:
        expected.m_receipt_sha256 = std::string(64uz, 'b');
        break;
    case 2:
        expected.m_recipe_sha256 = std::string(64uz, 'b');
        break;
    case 3:
        expected.m_pose_sha256 = std::string(64uz, 'b');
        break;
    case 4:
        expected.m_epoch = { 1 };
        break;
    case 5:
        expected.m_pose_revision = { 2 };
        break;
    case 6:
        expected.m_pose_sha256 = std::string(64uz, 'z');
        break;
    default:
        FAIL("Unknown mutation");
    }
    const auto K_REJECTED = decode_fusion_asset(K_BYTES, sha256(K_BYTES), expected);
    REQUIRE((K_REJECTED).has_value() == false);
    if (K_MUTATION == 4) CHECK(K_REJECTED.error().m_code == ErrorCode::WRONG_EPOCH);
    if (K_MUTATION == 5) CHECK(K_REJECTED.error().m_code == ErrorCode::WRONG_BASE);
    CHECK((decode_fusion_asset(K_BYTES, std::string(64uz, 'c'), fixture.m_context)).has_value() ==
          false);
}

TEST_CASE("fusion exact budgets and cancellation reject before returning assets", "[fusion]") {
    Fixture fixture;
    const auto K_BYTES = fixture.encode();
    FusionAssetLimits limits {{ K_BYTES.size() }, 2uz, 3uz, 1uz};
    REQUIRE(decode_fusion_asset(K_BYTES, sha256(K_BYTES), fixture.m_context, limits));
    const auto K_MUTATION = GENERATE(0, 1, 2, 3);
    switch (K_MUTATION) {
    case 0:
        --limits.m_encoded_bytes.m_bytes;
        break;
    case 1:
        --limits.m_max_voxels;
        break;
    case 2:
        --limits.m_max_vertices;
        break;
    case 3:
        limits.m_max_triangles = 0uz;
        break;
    default:
        FAIL("Unknown mutation");
    }
    CHECK((decode_fusion_asset(K_BYTES, sha256(K_BYTES), fixture.m_context, limits)).has_value() ==
          false);
    std::stop_source cancelled;
    cancelled.request_stop();
    const auto K_CANCELLED =
        decode_fusion_asset(K_BYTES, sha256(K_BYTES), fixture.m_context, {}, cancelled.get_token());
    REQUIRE((K_CANCELLED).has_value() == false);
    CHECK(K_CANCELLED.error().m_code == ErrorCode::CANCELLED);
}

TEST_CASE("fusion pose admission preserves caller files on stale lost or altered inputs",
          "[fusion]") {
    const auto K_MUTATION = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
    Fixture fixture;
    MapStore store;
    MapTransaction transaction;
    transaction.m_new_revision = { 1 };
    transaction.m_operation = { 1 };
    const FrameId K_FRAME { 9007199254740993ULL };
    transaction.m_calibrations = {{{ 3 }, { 2, 4, 1, .5 }, 2, 2}};
    transaction.m_observations = {
        {K_FRAME, { 1 }, { 3 }, { 0, 1, 30 }, std::chrono::nanoseconds { 0 }, std::string(64uz, 'b')}};
    transaction.m_alignments = {{{ 1 }, { 1 }, {}}};
    transaction.m_poses = {
        {K_FRAME, { 1 }, {}, std::chrono::nanoseconds { 0 }, { 1 }, TrackingState::TRACKING, {}}};
    REQUIRE(store.commit(transaction));
    auto map = std::make_shared<MapState>(*store.acquire_snapshot());
    if (K_MUTATION == 1) map->m_invalidated_frames.insert(K_FRAME);
    if (K_MUTATION == 2) map->m_poses.at(K_FRAME).m_tracking = TrackingState::LOST;
    const CanonicalPoseAsset K_POSES {
        map, {std::string(64uz, '1'), std::string(64uz, '2'), std::string(64uz, '3'), {}}};
    static std::atomic<unsigned> sequence {};
    const auto K_ROOT =
        std::filesystem::temp_directory_path() /
        ("we_fusion_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
         std::to_string(sequence++));
    REQUIRE(std::filesystem::create_directory(K_ROOT));

    struct Cleanup {
        std::filesystem::path m_path;

        ~Cleanup() {
            std::error_code ignored;
            std::filesystem::remove_all(m_path, ignored);
        }
    } cleanup { K_ROOT };

    const auto K_POSE_PATH = K_ROOT / "poses.weposes";
    const auto K_POSE_HASH = write_pose_asset(K_POSE_PATH, K_POSES);
    REQUIRE(K_POSE_HASH);
    fixture.m_context.m_pose_sha256 = *K_POSE_HASH;
    if (K_MUTATION == 3) fixture.m_receipt["frames"][0]["frame"] = 42U;
    if (K_MUTATION == 4) fixture.m_receipt["frames"][0]["calibration"] = 4U;
    if (K_MUTATION == 5)
        fixture.m_receipt["frames"][0]["observation_image_sha256"] = std::string(64uz, 'c');
    if (K_MUTATION == 6)
        fixture.m_receipt["frames"][0]["world_from_camera"]["translation"][0] = 1.0;
    if (K_MUTATION == 7) fixture.m_receipt["frames"][0]["world_from_camera"]["scale"] = 2.0;
    const auto K_BYTES = fixture.encode();
    const auto K_PATH = K_ROOT / "geometry.wefusion";
    const auto K_HASH = asset_files::publish_asset(K_PATH, K_BYTES, {});
    REQUIRE(K_HASH);
    const auto K_RESULT = load_pose_bound_fusion({K_PATH,
                                                  K_POSE_PATH,
                                                  *K_HASH,
                                                  *K_POSE_HASH,
                                                  fixture.m_context.m_manifest_sha256,
                                                  fixture.m_context.m_receipt_sha256,
                                                  fixture.m_context.m_recipe_sha256});
    INFO((K_RESULT ? "ok" : K_RESULT.error().m_message));
    if (K_MUTATION == 0) REQUIRE(K_RESULT);
    else CHECK((K_RESULT).has_value() == false);
    CHECK(*sha256_file(K_PATH, 65536uz) == *K_HASH);
    CHECK(*sha256_file(K_POSE_PATH, 65536uz) == *K_POSE_HASH);
}

TEST_CASE("fusion strict metadata rejects duplicate keys raw NUL and excessive nesting",
          "[fusion]") {
    Fixture fixture;
    const auto K_ORIGINAL = fixture.encode();
    const auto K_METADATA_SIZE = K_ORIGINAL.size() - 36uz - fixture.m_payload.size();
    const std::string K_METADATA {reinterpret_cast<const char*>(K_ORIGINAL.data() + 36uz),
                                  K_METADATA_SIZE};
    auto metadata = Json::parse(K_METADATA);
    std::string text;
    const auto K_MUTATION = GENERATE(0, 1, 2, 3, 4);
    switch (K_MUTATION) {
    case 0:
        text = K_METADATA;
        text.insert(1uz, "\"manifest\":\"duplicate\",");
        break;
    case 1: {
        auto manifest = metadata.at("manifest").get<std::string>();
        manifest.insert(1uz, "\"schema_version\":1,");
        metadata["manifest"] = manifest;
        fixture.m_context.m_manifest_sha256 = text_hash(manifest);
        text = metadata.dump();
        break;
    }
    case 2:
        metadata["manifest"] = metadata.at("manifest").get<std::string>() + '\0';
        fixture.m_context.m_manifest_sha256 = text_hash(metadata.at("manifest").get<std::string>());
        text = metadata.dump();
        break;
    case 3:
        text = std::string(40uz, '[') + "0" + std::string(40uz, ']');
        break;
    case 4:
        text = K_METADATA + '\0';
        break;
    default:
        FAIL("Unknown mutation");
    }
    std::vector<std::byte> bytes(K_ORIGINAL.begin(), K_ORIGINAL.begin() + 36);
    put(bytes, 8uz, text.size());
    const auto K_TEXT_BYTES = std::as_bytes(std::span(text));
    bytes.insert(bytes.end(), K_TEXT_BYTES.begin(), K_TEXT_BYTES.end());
    bytes.insert(bytes.end(), fixture.m_payload.begin(), fixture.m_payload.end());
    CHECK((decode_fusion_asset(bytes, sha256(bytes), fixture.m_context)).has_value() == false);
}
