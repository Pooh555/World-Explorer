#include "world_explorer/scene_store/fusion_asset.hpp"

#include "world_explorer/core/hash.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <new>
#include <set>
#include <stdexcept>
#include <string_view>

#include <nlohmann/json.hpp>

namespace world_explorer {
namespace {

using Json = nlohmann::json;
constexpr std::size_t K_MAX_BYTES { 256uz * 1024uz * 1024uz };
constexpr std::size_t K_MAX_METADATA { 4uz * 1024uz * 1024uz };
constexpr std::string_view K_UNITS { "ARBITRARY_POSE_CAMERA_UNITS" };
constexpr std::string_view K_SCOPE { "PARTIAL_OBSERVED_GEOMETRY_NO_Q5_N2_OR_WALK" };
constexpr std::string_view K_FREE { "OBSERVED_POSITIVE_TSDF_IN_LOCAL_TRUNCATION_BAND_ONLY" };
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::string text_hash(const std::string& text) {
    return sha256(std::as_bytes(std::span(text)));
}

bool digest_valid(const std::string& value) {
    return value.size() == 64uz && value.find_first_not_of("0123456789abcdef") == value.npos;
}

Json document(const std::string& text) {
    require(text.find('\0') == text.npos, "Raw NUL in fusion metadata");
    std::vector<std::set<std::string>> keys;
    return Json::parse(text, [&](int depth, Json::parse_event_t event, Json& value) {
        require(depth <= 32, "Fusion JSON nesting limit exceeded");
        if (event == Json::parse_event_t::object_start) keys.emplace_back();
        if (event == Json::parse_event_t::key)
            require(!keys.empty() && keys.back().insert(value.get<std::string>()).second,
                    "Duplicate fusion metadata key");
        if (event == Json::parse_event_t::object_end) keys.pop_back();
        return true;
    });
}

std::uint64_t count(const Json& object, const char* key, std::uint64_t maximum) {
    const auto& value = object.at(key);
    require(value.is_number_unsigned(), "Fusion count must be unsigned integer");
    const auto K_RESULT = value.get<std::uint64_t>();
    require(K_RESULT <= maximum, "Fusion count exceeds limit");
    return K_RESULT;
}

double positive(const Json& object, const char* key) {
    require(object.at(key).is_number(), "Fusion resolution must be numeric");
    const auto K_VALUE = object.at(key).get<double>();
    require(std::isfinite(K_VALUE) && K_VALUE > 0, "Invalid fusion resolution");
    return K_VALUE;
}

struct Decoder {
    std::span<const std::byte> m_bytes;
    std::size_t m_position {};

    std::uint64_t integer(std::size_t width) {
        require(width <= m_bytes.size() - m_position, "Truncated fusion asset");
        std::uint64_t value {};
        for (std::size_t i { 0uz }; i < width; ++i)
            value |= std::uint64_t { std::to_integer<unsigned char>(m_bytes[m_position++]) }
                     << (i * 8uz);
        return value;
    }

    std::string text(std::size_t size) {
        require(size <= m_bytes.size() - m_position, "Truncated fusion text");
        const std::string K_TEXT { reinterpret_cast<const char*>(m_bytes.data() + m_position), size };
        m_position += size;
        return K_TEXT;
    }

    float real32() { return std::bit_cast<float>(static_cast<std::uint32_t>(integer(4uz))); }

    double real64() { return std::bit_cast<double>(integer(8uz)); }
};

void check_policy(const Json& manifest, const Json& recipe, const Json& receipt) {
    require(count(manifest, "schema_version", 1) == 1 &&
                manifest.at("kind") == "PARTIAL_OBSERVED_TSDF_V1" &&
                manifest.at("scope") == K_SCOPE && manifest.at("units") == K_UNITS &&
                manifest.at("world_axes") == "RIGHT_HANDED_Y_UP" &&
                manifest.at("depth_convention") == "AXIAL_CAMERA_Z" &&
                manifest.at("walking_enabled").is_boolean() &&
                manifest.at("walking_enabled") == false && manifest.at("free_space") == K_FREE &&
                manifest.at("unknown") ==
                    "ABSENT_LATTICE_ENTRIES_AND_ZERO_WEIGHT_ARE_UNKNOWN; NO_FULL_RAY_CARVING" &&
                manifest.at("uncertainty") ==
                    "WEIGHTED_RMS_INPUT_SIGMA_WHERE_SIGMA_WEIGHT_POSITIVE; NOT_SURFACE_ERROR" &&
                manifest.at("mesh_policy") ==
                    "RAW_WEIGHT_SUPPORTED_EXTRACTION; NO_FILL_OR_SIMPLIFICATION" &&
                manifest.at("radiance") == "NO_COLOR_OR_RELIGHTABLE_ALBEDO_PRODUCT",
            "Unsupported fusion semantics");
    require(count(recipe, "schema_version", 1) == 1 &&
                recipe.at("kind") == "OBSERVED_TSDF_CPU_PILOT_V1" &&
                recipe.at("units") == K_UNITS && recipe.at("scope") == K_SCOPE &&
                recipe.at("free_space") == K_FREE &&
                recipe.at("weight") == "REFERENCE_CONFIDENCE_ONLY_NOT_PROBABILITY" &&
                count(recipe, "voxel_divisor", 512) == 512 &&
                count(recipe, "truncation_voxels", 4) == 4 &&
                count(recipe, "block_resolution", 8) == 8 &&
                count(recipe, "minimum_consistent_sources", 2) == 2 &&
                recipe.at("relative_depth_error") == .01 && recipe.at("roundtrip_pixels") == 1.0 &&
                recipe.at("reject_known_occlusion").is_boolean() &&
                recipe.at("reject_known_occlusion") == true,
            "Unsupported fusion recipe");
    require(count(receipt, "schema_version", 1) == 1 &&
                receipt.at("kind") == "POSE_ADMITTED_DEPTH_VISIBILITY_V1" &&
                receipt.at("units") == K_UNITS &&
                receipt.at("depth_convention") == "AXIAL_CAMERA_Z" &&
                receipt.at("pixel_centers") == "integer" &&
                receipt.at("camera_axes") == "+x_right,+y_down,+z_forward" &&
                receipt.at("confidence") == "RECIPE_DEFINED_RELATIVE_WEIGHT_NOT_PROBABILITY",
            "Unsupported fusion input receipt");
}

void check_coverage(const Json& manifest, const Json::array_t& frames) {
    const auto& coverage = manifest.at("coverage");
    require(!frames.empty() && frames.size() <= 18uz && coverage.is_array() &&
                coverage.size() == frames.size(),
            "Incomplete fusion coverage inventory");
    std::set<std::uint64_t> seen;
    std::uint64_t total {}, positive_pixels {};
    for (std::size_t i { 0uz }; i < frames.size(); ++i) {
        const auto& frame = frames[i];
        const auto& row = coverage[i];
        require(seen.insert(count(frame, "frame", UINT64_MAX)).second,
                "Duplicate fusion input frame");
        const auto K_WIDTH = count(frame, "width", 4096);
        const auto K_HEIGHT = count(frame, "height", 4096);
        require(K_WIDTH && K_HEIGHT, "Empty fusion grid");
        const auto K_PIXELS = K_WIDTH * K_HEIGHT;
        total += K_PIXELS;
        require(total <= 48000000 && count(row, "pixels", K_PIXELS) == K_PIXELS &&
                    frame.at("image") == "images/" + row.at("name").get<std::string>(),
                "Fusion coverage grid or order differs");
        const auto K_VALID = count(row, "valid", K_PIXELS);
        const auto K_ZERO = count(row, "zero_confidence_valid", K_VALID);
        require(count(row, "unknown", K_PIXELS) + K_VALID == K_PIXELS &&
                    count(frame, "valid_pixels", K_PIXELS) == K_VALID &&
                    count(frame, "unknown_pixels", K_PIXELS) + K_VALID == K_PIXELS &&
                    count(row, "accepted", K_VALID - K_ZERO) +
                            count(row, "rejected_positive_weight", K_VALID - K_ZERO) ==
                        K_VALID - K_ZERO &&
                    count(row, "enough_consistent_sources", K_VALID - K_ZERO) >=
                        count(row, "accepted", K_VALID) &&
                    count(row, "known_occlusion", K_VALID - K_ZERO) <= K_VALID - K_ZERO &&
                    row.at("uncertainty") == frame.at("uncertainty") &&
                    (row.at("uncertainty") == "UNAVAILABLE" ||
                     row.at("uncertainty") == "CAMERA_UNIT_STANDARD_DEVIATION"),
                "Fusion coverage denominators differ");
        positive_pixels += K_VALID - K_ZERO;
    }
    require(count(manifest, "extent_samples", 48000000) == positive_pixels,
            "Fusion extent denominator differs");
}

}  // namespace

Result<ObservedFusionAsset> decode_fusion_asset(std::span<const std::byte> bytes,
                                                const std::string& expected_sha256,
                                                const FusionContext& expected,
                                                FusionAssetLimits limits,
                                                const std::stop_token& stop) {
    try {
        if (stop.stop_requested()) return fail(ErrorCode::CANCELLED, "Fusion admission cancelled");
        if (!limits.m_encoded_bytes.m_bytes || limits.m_encoded_bytes.m_bytes > K_MAX_BYTES ||
            !limits.m_max_voxels || limits.m_max_voxels > 16777216uz || !limits.m_max_vertices ||
            limits.m_max_vertices > 4000000uz || !limits.m_max_triangles ||
            limits.m_max_triangles > 8000000uz)
            return fail(ErrorCode::INVALID_ARGUMENT, "Invalid fusion limits");
        if (bytes.size() > limits.m_encoded_bytes.m_bytes)
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "Fusion encoded byte limit exceeded");
        for (const auto& pin : {expected_sha256,
                                expected.m_manifest_sha256,
                                expected.m_recipe_sha256,
                                expected.m_receipt_sha256,
                                expected.m_pose_sha256})
            require(digest_valid(pin), "Invalid fusion SHA-256 pin");
        Sha256Builder hash;
        for (std::size_t offset { 0uz }; offset < bytes.size();) {
            if (stop.stop_requested()) return fail(ErrorCode::CANCELLED, "Fusion hash cancelled");
            const auto K_SIZE = std::min(256uz * 1024uz, bytes.size() - offset);
            hash.update(bytes.subspan(offset, K_SIZE));
            offset += K_SIZE;
        }
        require(hash.finish() == expected_sha256, "Fusion checksum differs");
        Decoder decoder { bytes };
        require(decoder.text(8uz) == "WEFUSN01", "Unsupported fusion wire version");
        const auto K_METADATA_SIZE = decoder.integer(4uz);
        const auto K_VOXELS = decoder.integer(8uz);
        const auto K_VERTICES = decoder.integer(8uz);
        const auto K_TRIANGLES = decoder.integer(8uz);
        require(K_METADATA_SIZE && K_METADATA_SIZE <= K_MAX_METADATA &&
                    K_VOXELS <= limits.m_max_voxels && K_VERTICES <= limits.m_max_vertices &&
                    K_TRIANGLES <= limits.m_max_triangles,
                "Fusion header exceeds limits");
        const auto K_TOTAL =
            36uz + K_METADATA_SIZE + K_VOXELS * 44uz + K_VERTICES * 24uz + K_TRIANGLES * 12uz;
        require(K_TOTAL == bytes.size(), "Fusion payload length differs");
        const auto K_METADATA = document(decoder.text(K_METADATA_SIZE));
        require(K_METADATA.is_object() && K_METADATA.size() == 4uz,
                "Invalid fusion metadata envelope");
        ObservedFusionAsset asset;
        asset.m_context = expected;
        asset.m_asset_sha256 = expected_sha256;
        asset.m_manifest_json = K_METADATA.at("manifest").get<std::string>();
        asset.m_receipt_json = K_METADATA.at("receipt").get<std::string>();
        const auto K_RECIPE_TEXT = K_METADATA.at("recipe").get<std::string>();
        const auto K_REQUEST_TEXT = K_METADATA.at("request").get<std::string>();
        require(text_hash(asset.m_manifest_json) == expected.m_manifest_sha256 &&
                    text_hash(asset.m_receipt_json) == expected.m_receipt_sha256 &&
                    text_hash(K_RECIPE_TEXT) == expected.m_recipe_sha256,
                "Fusion embedded document checksum differs");
        const auto K_MANIFEST = document(asset.m_manifest_json);
        const auto K_RECEIPT = document(asset.m_receipt_json);
        const auto K_RECIPE = document(K_RECIPE_TEXT);
        const auto K_REQUEST = document(K_REQUEST_TEXT);
        require(
            K_MANIFEST.at("request_sha256") == text_hash(K_REQUEST_TEXT) && K_REQUEST.is_object() &&
                K_REQUEST.size() == 5uz && count(K_REQUEST, "schema_version", 1) == 1 &&
                K_REQUEST.at("cache").is_string() && K_REQUEST.at("recipe").is_string() &&
                K_REQUEST.at("receipt_sha256") == expected.m_receipt_sha256 &&
                K_REQUEST.at("recipe_sha256") == expected.m_recipe_sha256 &&
                K_MANIFEST.at("receipt_sha256") == expected.m_receipt_sha256 &&
                K_MANIFEST.at("recipe_sha256") == expected.m_recipe_sha256 &&
                K_MANIFEST.at("pose_asset_sha256") == expected.m_pose_sha256 &&
                K_RECEIPT.at("pose_asset_sha256") == expected.m_pose_sha256 &&
                K_RECEIPT.at("files").at("poses.weposes").at("sha256") == expected.m_pose_sha256,
            "Fusion dependency graph differs");
        if (count(K_MANIFEST, "world_epoch", UINT64_MAX) != expected.m_epoch.m_value ||
            count(K_RECEIPT, "world_epoch", UINT64_MAX) != expected.m_epoch.m_value)
            return fail(ErrorCode::WRONG_EPOCH, "Fusion pose epoch differs");
        if (!expected.m_pose_revision.m_value ||
            count(K_MANIFEST, "pose_revision", UINT64_MAX) != expected.m_pose_revision.m_value ||
            count(K_RECEIPT, "pose_revision", UINT64_MAX) != expected.m_pose_revision.m_value)
            return fail(ErrorCode::WRONG_BASE, "Fusion pose revision differs");
        check_policy(K_MANIFEST, K_RECIPE, K_RECEIPT);
        check_coverage(K_MANIFEST, K_RECEIPT.at("frames").get_ref<const Json::array_t&>());
        asset.m_extent = positive(K_MANIFEST, "extent");
        asset.m_voxel_size = positive(K_MANIFEST, "voxel_size");
        asset.m_truncation = positive(K_MANIFEST, "truncation");
        require(asset.m_voxel_size == asset.m_extent / 512 &&
                    asset.m_truncation == asset.m_voxel_size * 4 &&
                    count(K_MANIFEST, "observed_voxels", limits.m_max_voxels) == K_VOXELS &&
                    count(K_MANIFEST, "vertices", limits.m_max_vertices) == K_VERTICES &&
                    count(K_MANIFEST, "triangles", limits.m_max_triangles) == K_TRIANGLES &&
                    count(K_MANIFEST, "block_resolution", 8) == 8,
                "Fusion resolution or counts differ");
        const auto K_BLOCKS = count(K_MANIFEST, "allocated_blocks", 32768);
        asset.m_unknown_allocated_voxels = count(K_MANIFEST, "unknown_allocated_voxels", 16777216);
        require(K_VOXELS + asset.m_unknown_allocated_voxels == K_BLOCKS * 512uz,
                "Fusion unknown allocation denominator differs");
        asset.m_voxels.reserve(K_VOXELS);
        std::size_t front {}, sigma {};
        for (std::size_t i { 0uz }; i < K_VOXELS; ++i) {
            if (i % 4096uz == 0uz && stop.stop_requested())
                return fail(ErrorCode::CANCELLED, "Fusion voxel admission cancelled");
            ObservedVoxel voxel;
            for (auto& coordinate : voxel.m_lattice) {
                coordinate =
                    std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(decoder.integer(4uz)));
                require(coordinate >= -1048576 && coordinate <= 1048576,
                        "Fusion lattice exceeds precision bound");
            }
            require(asset.m_voxels.empty() || asset.m_voxels.back().m_lattice < voxel.m_lattice,
                    "Fusion lattice must be unique and sorted");
            voxel.m_tsdf = decoder.real32();
            voxel.m_weight = decoder.real32();
            voxel.m_observation_count = static_cast<std::uint32_t>(decoder.integer(4uz));
            voxel.m_support_sum = static_cast<std::uint32_t>(decoder.integer(4uz));
            voxel.m_front_count = static_cast<std::uint32_t>(decoder.integer(4uz));
            voxel.m_sigma2_sum = decoder.real64();
            voxel.m_sigma_weight = decoder.real32();
            const auto K_VIEWS = K_RECEIPT.at("frames").size();
            require(std::isfinite(voxel.m_tsdf) && std::abs(voxel.m_tsdf) <= 1 &&
                        std::isfinite(voxel.m_weight) && voxel.m_weight > 0 &&
                        voxel.m_observation_count > 0 && voxel.m_observation_count <= K_VIEWS &&
                        voxel.m_weight <= static_cast<float>(voxel.m_observation_count) &&
                        voxel.m_support_sum >= voxel.m_observation_count &&
                        voxel.m_support_sum <= voxel.m_observation_count * 1000000U &&
                        voxel.m_front_count <= voxel.m_observation_count &&
                        std::isfinite(voxel.m_sigma_weight) && voxel.m_sigma_weight >= 0 &&
                        voxel.m_sigma_weight <= voxel.m_weight &&
                        std::isfinite(voxel.m_sigma2_sum) && voxel.m_sigma2_sum >= 0 &&
                        (voxel.m_sigma_weight > 0 || voxel.m_sigma2_sum == 0),
                    "Invalid fusion voxel evidence");
            front += voxel.m_front_count > 0;
            sigma += voxel.m_sigma_weight > 0;
            asset.m_voxels.push_back(voxel);
        }
        require(count(K_MANIFEST, "front_band_voxels", K_VOXELS) == front &&
                    count(K_MANIFEST, "sigma_available_voxels", K_VOXELS) == sigma,
                "Fusion evidence denominators differ");
        asset.m_vertices.reserve(K_VERTICES);
        for (std::size_t i { 0uz }; i < K_VERTICES; ++i) {
            if (i % 4096uz == 0uz && stop.stop_requested())
                return fail(ErrorCode::CANCELLED, "Fusion mesh admission cancelled");
            FusionVertex vertex;
            for (auto& position : vertex.m_position) {
                position = decoder.real32();
                require(std::isfinite(position) &&
                            std::abs(static_cast<double>(position)) <= 1048578 * asset.m_voxel_size,
                        "Invalid fusion mesh position");
            }
            double length {};
            for (auto& normal : vertex.m_normal) {
                normal = decoder.real32();
                require(std::isfinite(normal), "Nonfinite fusion normal");
                length += static_cast<double>(normal) * normal;
            }
            // Open3D may return a zero normal on degenerate raw extraction vertices.
            require(length == 0 || std::abs(length - 1) <= .001,
                    "Fusion normal must be unit or explicitly zero");
            asset.m_vertices.push_back(vertex);
        }
        asset.m_triangles.reserve(K_TRIANGLES);
        for (std::size_t i { 0uz }; i < K_TRIANGLES; ++i) {
            if (i % 4096uz == 0uz && stop.stop_requested())
                return fail(ErrorCode::CANCELLED, "Fusion indices admission cancelled");
            std::array<std::uint32_t, 3> triangle {};
            for (auto& index : triangle) {
                index = static_cast<std::uint32_t>(decoder.integer(4uz));
                require(index < K_VERTICES, "Fusion triangle index out of bounds");
            }
            require(triangle[0] != triangle[1] && triangle[0] != triangle[2] &&
                        triangle[1] != triangle[2],
                    "Fusion triangle repeats an index");
            asset.m_triangles.push_back(triangle);
        }
        return asset;
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Fusion allocation failed");
    } catch (const std::exception& exception) {
        return fail(ErrorCode::INVALID_DATA,
                    std::string("Fusion admission failed: ") + exception.what());
    }
}

}  // namespace world_explorer
