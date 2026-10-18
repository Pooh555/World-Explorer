#include "world_explorer/scene_store/depth_asset.hpp"

#include "world_explorer/core/asset_files.hpp"
#include "world_explorer/core/hash.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <new>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace world_explorer {
namespace {

constexpr std::size_t K_HEADER_BYTES { 356uz };
constexpr std::size_t K_MAX_BYTES { 256uz * 1024uz * 1024uz };
constexpr std::size_t K_HASH_BLOCK_BYTES { 256uz * 1024uz };
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);

bool digest_valid(std::string_view value) {
    return value.size() == 64 &&
           value.find_first_not_of("0123456789abcdef") == std::string_view::npos;
}

bool limits_valid(const DepthAssetLimits& limits) {
    return limits.m_max_dimension && limits.m_max_dimension <= 16384 && limits.m_max_pixels &&
           limits.m_max_pixels <= 16384uz * 16384 && limits.m_encoded_bytes.m_bytes &&
           limits.m_encoded_bytes.m_bytes <= K_MAX_BYTES && limits.m_payload_bytes.m_bytes &&
           limits.m_payload_bytes.m_bytes <= K_MAX_BYTES && limits.m_max_support_count;
}

Result<std::size_t> pixel_count(const DepthAssetMetadata& metadata,
                                const DepthAssetLimits& limits) {
    if (!limits_valid(limits))
        return fail(ErrorCode::INVALID_ARGUMENT, "Invalid depth asset limits");
    if (metadata.m_convention != DepthConvention::AXIAL_CAMERA_Z ||
        metadata.m_scale != ScaleStatus::ARBITRARY)
        return fail(ErrorCode::UNSUPPORTED_OPERATION,
                    "Depth v1 requires axial camera-Z in arbitrary pose-camera units");
    if (!metadata.m_width || !metadata.m_height || metadata.m_width > limits.m_max_dimension ||
        metadata.m_height > limits.m_max_dimension ||
        metadata.m_width > limits.m_max_pixels / metadata.m_height)
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Depth grid exceeds pixel budget");
    const auto& dependency = metadata.m_dependency;
    if ((metadata.m_producer != DepthProducerKind::SYNTHETIC_DIAGNOSTIC &&
         metadata.m_producer != DepthProducerKind::MULTIVIEW_STEREO) ||
        !dependency.m_pose_revision.m_value || !dependency.m_frame.m_value ||
        !dependency.m_calibration.m_value || !digest_valid(dependency.m_pose_asset_sha256) ||
        !digest_valid(dependency.m_observation_image_sha256) ||
        !digest_valid(metadata.m_calibrated_image_sha256) ||
        !digest_valid(metadata.m_recipe_sha256) || !project(metadata.m_camera, {0, 0, 1}))
        return fail(ErrorCode::INVALID_DATA, "Invalid depth identity or calibrated camera");
    return std::size_t { metadata.m_width } * metadata.m_height;
}

Result<std::size_t>
payload_size(std::size_t pixels, bool uncertainty, const DepthAssetLimits& limits) {
    const std::size_t K_STRIDE = uncertainty ? 17 : 13;
    if (pixels > limits.m_payload_bytes.m_bytes / K_STRIDE ||
        limits.m_encoded_bytes.m_bytes < K_HEADER_BYTES ||
        pixels > (limits.m_encoded_bytes.m_bytes - K_HEADER_BYTES) / K_STRIDE)
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Depth planes exceed byte budget");
    return pixels * K_STRIDE;
}

Result<std::string> checked_hash(std::span<const std::byte> bytes, const std::stop_token& stop) {
    Sha256Builder builder;
    for (std::size_t offset { 0uz }; offset < bytes.size();) {
        if (stop.stop_requested()) return fail(ErrorCode::CANCELLED, "Depth checksum cancelled");
        const auto K_COUNT = std::min(K_HASH_BLOCK_BYTES, bytes.size() - offset);
        builder.update(bytes.subspan(offset, K_COUNT));
        offset += K_COUNT;
    }
    return builder.finish();
}

struct Encoder {
    std::vector<std::byte> m_bytes;

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): scalar value and wire byte width.
    void integer(std::uint64_t value, std::size_t width) {
        for (std::size_t i { 0uz }; i < width; ++i) {
            m_bytes.push_back(static_cast<std::byte>(value & 255U));
            value >>= 8U;
        }
    }

    void text(std::string_view value) {
        for (const auto K_C : value) integer(static_cast<unsigned char>(K_C), 1);
    }

    template <class ValueType>
    Result<void> plane(const std::vector<ValueType>& values, const std::stop_token& stop) {
        for (std::size_t i { 0uz }; i < values.size(); ++i) {
            if (i % 4096 == 0 && stop.stop_requested())
                return fail(ErrorCode::CANCELLED, "Depth encoding cancelled");
            if constexpr (std::is_same_v<ValueType, float>)
                integer(std::bit_cast<std::uint32_t>(values[i]), 4);
            else integer(values[i], sizeof(ValueType));
        }
        return {};
    }
};

struct Decoder {
    std::span<const std::byte> m_bytes;
    std::size_t m_position {};

    std::uint64_t integer(std::size_t width) {
        if (width > m_bytes.size() - m_position) throw std::runtime_error("Truncated depth asset");
        std::uint64_t value {};
        for (std::size_t i { 0uz }; i < width; ++i)
            value |= std::uint64_t { std::to_integer<unsigned char>(m_bytes[m_position++]) }
                     << (i * 8);
        return value;
    }

    std::string text(std::size_t count) {
        if (count > m_bytes.size() - m_position)
            throw std::runtime_error("Truncated depth metadata");
        std::string value;
        value.reserve(count);
        for (std::size_t i { 0uz }; i < count; ++i) value.push_back(static_cast<char>(integer(1)));
        return value;
    }

    template <class ValueType>
    Result<void>
    plane(std::vector<ValueType>& values, std::size_t pixels, const std::stop_token& stop) {
        values.resize(pixels);
        for (std::size_t i { 0uz }; i < pixels; ++i) {
            if (i % 4096 == 0 && stop.stop_requested())
                return fail(ErrorCode::CANCELLED, "Depth decoding cancelled");
            if constexpr (std::is_same_v<ValueType, float>)
                values[i] = std::bit_cast<float>(static_cast<std::uint32_t>(integer(4)));
            else values[i] = static_cast<ValueType>(integer(sizeof(ValueType)));
        }
        return {};
    }
};

bool positive_zero(float value) {
    return value == 0 && !std::signbit(value);
}

}  // namespace

Result<std::size_t> validate_depth_asset(const DepthImageAsset& asset,
                                         DepthAssetLimits limits,
                                         const std::stop_token& stop) {
    if (stop.stop_requested()) return fail(ErrorCode::CANCELLED, "Depth validation cancelled");
    const auto K_PIXELS = pixel_count(asset.m_metadata, limits);
    if (!K_PIXELS) return std::unexpected(K_PIXELS.error());
    const auto K_SIZE = payload_size(*K_PIXELS, asset.m_standard_deviation.has_value(), limits);
    if (!K_SIZE) return std::unexpected(K_SIZE.error());
    if (asset.m_axial_depth.size() != *K_PIXELS || asset.m_valid.size() != *K_PIXELS ||
        asset.m_confidence.size() != *K_PIXELS || asset.m_support_count.size() != *K_PIXELS ||
        (asset.m_standard_deviation && asset.m_standard_deviation->size() != *K_PIXELS))
        return fail(ErrorCode::INVALID_DATA, "Depth plane dimensions disagree");
    for (std::size_t i { 0uz }; i < *K_PIXELS; ++i) {
        if (i % 4096 == 0 && stop.stop_requested())
            return fail(ErrorCode::CANCELLED, "Depth validation cancelled");
        const auto K_Z = asset.m_axial_depth[i];
        const auto K_WEIGHT = asset.m_confidence[i];
        if (asset.m_valid[i] > 1 || !std::isfinite(K_Z) || !std::isfinite(K_WEIGHT) ||
            K_WEIGHT < 0 || K_WEIGHT > 1 || asset.m_support_count[i] > limits.m_max_support_count)
            return fail(ErrorCode::INVALID_DATA, "Invalid depth sample or confidence");
        if (asset.m_valid[i]) {
            if (K_Z <= 0 || asset.m_support_count[i] == 0)
                return fail(ErrorCode::INVALID_DATA, "Valid depth requires positive Z and support");
        } else if (!positive_zero(K_Z) || !positive_zero(K_WEIGHT) ||
                   asset.m_support_count[i] != 0) {
            return fail(ErrorCode::INVALID_DATA,
                        "Invalid depth must retain explicit unknown samples");
        }
        if (asset.m_standard_deviation) {
            const auto K_SIGMA = (*asset.m_standard_deviation)[i];
            if (!std::isfinite(K_SIGMA) || K_SIGMA < 0 ||
                (!asset.m_valid[i] && !positive_zero(K_SIGMA)))
                return fail(ErrorCode::INVALID_DATA, "Invalid depth uncertainty");
        }
    }
    return K_SIZE;
}

Result<void> admit_depth_asset(const DepthImageAsset& asset,
                               const DepthAssetMetadata& expected,
                               DepthAssetLimits limits,
                               const std::stop_token& stop) {
    const auto K_CHECKED = validate_depth_asset(asset, limits, stop);
    if (!K_CHECKED) return std::unexpected(K_CHECKED.error());
    const auto K_EXPECTED_PIXELS = pixel_count(expected, limits);
    if (!K_EXPECTED_PIXELS) return std::unexpected(K_EXPECTED_PIXELS.error());
    const auto& actual = asset.m_metadata;
    if (actual.m_dependency.m_epoch != expected.m_dependency.m_epoch)
        return fail(ErrorCode::WRONG_EPOCH,
                    "Depth world epoch changed; resynchronization required");
    if (actual.m_dependency.m_pose_revision != expected.m_dependency.m_pose_revision)
        return fail(ErrorCode::WRONG_BASE,
                    "Depth pose revision changed; resynchronization required");
    if (actual.m_dependency.m_pose_asset_sha256 != expected.m_dependency.m_pose_asset_sha256)
        return fail(ErrorCode::RESYNC_REQUIRED,
                    "Depth depends on a different canonical pose asset");
    if (actual.m_dependency != expected.m_dependency || actual.m_width != expected.m_width ||
        actual.m_height != expected.m_height || actual.m_camera.m_fx != expected.m_camera.m_fx ||
        actual.m_camera.m_fy != expected.m_camera.m_fy ||
        actual.m_camera.m_cx != expected.m_camera.m_cx ||
        actual.m_camera.m_cy != expected.m_camera.m_cy ||
        actual.m_recipe_sha256 != expected.m_recipe_sha256 ||
        actual.m_producer != expected.m_producer ||
        actual.m_calibrated_image_sha256 != expected.m_calibrated_image_sha256)
        return fail(ErrorCode::INVALID_DATA,
                    "Depth observation, recipe or calibrated grid mismatch");
    return {};
}

Result<std::vector<std::byte>> encode_depth_asset(const DepthImageAsset& asset,
                                                  DepthAssetLimits limits,
                                                  const std::stop_token& stop) {
    try {
        const auto K_CHECKED = validate_depth_asset(asset, limits, stop);
        if (!K_CHECKED) return std::unexpected(K_CHECKED.error());
        Encoder output;
        output.m_bytes.reserve(K_HEADER_BYTES + *K_CHECKED);
        const auto& metadata = asset.m_metadata;
        const auto& dependency = metadata.m_dependency;
        output.text("WEXDEP01");
        for (const auto K_VALUE : {1U,
                                   static_cast<std::uint32_t>(metadata.m_convention),
                                   static_cast<std::uint32_t>(metadata.m_producer),
                                   static_cast<std::uint32_t>(metadata.m_scale),
                                   metadata.m_width,
                                   metadata.m_height,
                                   asset.m_standard_deviation ? 1U : 0U})
            output.integer(K_VALUE, 4);
        for (const auto K_VALUE : {dependency.m_epoch.m_value,
                                   dependency.m_pose_revision.m_value,
                                   dependency.m_frame.m_value,
                                   dependency.m_calibration.m_value})
            output.integer(K_VALUE, 8);
        for (const auto K_VALUE : {metadata.m_camera.m_fx,
                                   metadata.m_camera.m_fy,
                                   metadata.m_camera.m_cx,
                                   metadata.m_camera.m_cy})
            output.integer(std::bit_cast<std::uint64_t>(K_VALUE), 8);
        for (const auto& value : {dependency.m_pose_asset_sha256,
                                  dependency.m_observation_image_sha256,
                                  metadata.m_calibrated_image_sha256,
                                  metadata.m_recipe_sha256})
            output.text(value);
        auto status = output.plane(asset.m_axial_depth, stop);
        if (status) status = output.plane(asset.m_valid, stop);
        if (status) status = output.plane(asset.m_confidence, stop);
        if (status) status = output.plane(asset.m_support_count, stop);
        if (status && asset.m_standard_deviation)
            status = output.plane(*asset.m_standard_deviation, stop);
        if (!status) return std::unexpected(status.error());
        return std::move(output.m_bytes);
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Depth encoding allocation failed");
    } catch (const std::exception& exception) {
        return fail(ErrorCode::INVALID_DATA,
                    std::string("Depth encoding failed: ") + exception.what());
    }
}

Result<DepthImageAsset> decode_depth_asset(std::span<const std::byte> bytes,
                                           const std::string& expected_sha256,
                                           DepthAssetLimits limits,
                                           const std::stop_token& stop) {
    try {
        if (stop.stop_requested()) return fail(ErrorCode::CANCELLED, "Depth decoding cancelled");
        if (!limits_valid(limits))
            return fail(ErrorCode::INVALID_ARGUMENT, "Invalid depth asset limits");
        if (bytes.size() > limits.m_encoded_bytes.m_bytes)
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "Depth file exceeds encoded byte budget");
        if (bytes.size() < K_HEADER_BYTES || !digest_valid(expected_sha256))
            return fail(ErrorCode::INVALID_DATA, "Invalid depth file or checksum");
        const auto K_HASH = checked_hash(bytes, stop);
        if (!K_HASH) return std::unexpected(K_HASH.error());
        if (*K_HASH != expected_sha256)
            return fail(ErrorCode::INVALID_DATA, "Depth checksum mismatch");
        Decoder input { bytes };
        if (input.text(8) != "WEXDEP01" || input.integer(4) != 1)
            return fail(ErrorCode::UNSUPPORTED_OPERATION, "Unsupported depth asset version");
        DepthImageAsset asset;
        auto& metadata = asset.m_metadata;
        const auto K_CONVENTION = input.integer(4);
        if (K_CONVENTION != static_cast<std::uint32_t>(DepthConvention::AXIAL_CAMERA_Z))
            return fail(ErrorCode::UNSUPPORTED_OPERATION, "Unsupported depth convention");
        const auto K_PRODUCER = input.integer(4);
        if (K_PRODUCER != static_cast<std::uint32_t>(DepthProducerKind::SYNTHETIC_DIAGNOSTIC) &&
            K_PRODUCER != static_cast<std::uint32_t>(DepthProducerKind::MULTIVIEW_STEREO))
            return fail(ErrorCode::INVALID_DATA, "Unsupported depth producer");
        metadata.m_producer = static_cast<DepthProducerKind>(K_PRODUCER);
        const auto K_SCALE = input.integer(4);
        if (K_SCALE != static_cast<std::uint32_t>(ScaleStatus::ARBITRARY))
            return fail(ErrorCode::UNSUPPORTED_OPERATION, "Unsupported depth scale provenance");
        metadata.m_width = static_cast<std::uint32_t>(input.integer(4));
        metadata.m_height = static_cast<std::uint32_t>(input.integer(4));
        const auto K_UNCERTAINTY = input.integer(4);
        if (K_UNCERTAINTY > 1)
            return fail(ErrorCode::INVALID_DATA, "Unknown depth uncertainty encoding");
        auto& dependency = metadata.m_dependency;
        dependency.m_epoch = { input.integer(8) };
        dependency.m_pose_revision = { input.integer(8) };
        dependency.m_frame = { input.integer(8) };
        dependency.m_calibration = { input.integer(8) };
        metadata.m_camera = { std::bit_cast<double>(input.integer(8)),
                             std::bit_cast<double>(input.integer(8)),
                             std::bit_cast<double>(input.integer(8)),
                             std::bit_cast<double>(input.integer(8)) };
        dependency.m_pose_asset_sha256 = input.text(64);
        dependency.m_observation_image_sha256 = input.text(64);
        metadata.m_calibrated_image_sha256 = input.text(64);
        metadata.m_recipe_sha256 = input.text(64);
        const auto K_PIXELS = pixel_count(metadata, limits);
        if (!K_PIXELS) return std::unexpected(K_PIXELS.error());
        const auto K_SIZE = payload_size(*K_PIXELS, K_UNCERTAINTY != 0, limits);
        if (!K_SIZE) return std::unexpected(K_SIZE.error());
        if (*K_SIZE != bytes.size() - input.m_position)
            return fail(ErrorCode::INVALID_DATA,
                        "Depth planes are truncated or have trailing data");
        auto status = input.plane(asset.m_axial_depth, *K_PIXELS, stop);
        if (status) status = input.plane(asset.m_valid, *K_PIXELS, stop);
        if (status) status = input.plane(asset.m_confidence, *K_PIXELS, stop);
        if (status) status = input.plane(asset.m_support_count, *K_PIXELS, stop);
        if (status && K_UNCERTAINTY)
            status = input.plane(asset.m_standard_deviation.emplace(), *K_PIXELS, stop);
        if (!status) return std::unexpected(status.error());
        const auto K_CHECKED = validate_depth_asset(asset, limits, stop);
        if (!K_CHECKED) return std::unexpected(K_CHECKED.error());
        return asset;
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Depth decoding allocation failed");
    } catch (const std::exception& exception) {
        return fail(ErrorCode::INVALID_DATA,
                    std::string("Depth decoding failed: ") + exception.what());
    }
}

Result<std::string> write_depth_asset(const std::filesystem::path& path,
                                      const DepthImageAsset& asset,
                                      DepthAssetLimits limits,
                                      const std::stop_token& stop) {
    const auto K_ENCODED = encode_depth_asset(asset, limits, stop);
    if (!K_ENCODED) return std::unexpected(K_ENCODED.error());
    return asset_files::publish_asset(path, *K_ENCODED, stop);
}

Result<DepthImageAsset> read_depth_asset(const std::filesystem::path& path,
                                         const std::string& expected_sha256,
                                         const DepthAssetMetadata& expected,
                                         DepthAssetLimits limits,
                                         const std::stop_token& stop) {
    if (!limits_valid(limits))
        return fail(ErrorCode::INVALID_ARGUMENT, "Invalid depth asset limits");
    const auto K_BYTES = asset_files::read_bytes(path, limits.m_encoded_bytes.m_bytes, stop);
    if (!K_BYTES) return std::unexpected(K_BYTES.error());
    auto decoded = decode_depth_asset(*K_BYTES, expected_sha256, limits, stop);
    if (!decoded) return std::unexpected(decoded.error());
    const auto K_ADMITTED = admit_depth_asset(*decoded, expected, limits, stop);
    if (!K_ADMITTED) return std::unexpected(K_ADMITTED.error());
    return decoded;
}

}  // namespace world_explorer
