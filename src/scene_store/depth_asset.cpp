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
constexpr std::size_t HEADER_BYTES = 356;
constexpr std::size_t MAX_BYTES = std::size_t{256} * 1024 * 1024;
constexpr std::size_t HASH_BLOCK_BYTES = std::size_t{256} * 1024;
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);
bool digest_valid(std::string_view value) {
    return value.size() == 64 &&
           value.find_first_not_of("0123456789abcdef") == std::string_view::npos;
}
bool limits_valid(const depth_asset_limits& limits) {
    return limits.max_dimension && limits.max_dimension <= 16384 && limits.max_pixels &&
           limits.max_pixels <= std::size_t{16384} * 16384 && limits.encoded_bytes.bytes &&
           limits.encoded_bytes.bytes <= MAX_BYTES && limits.payload_bytes.bytes &&
           limits.payload_bytes.bytes <= MAX_BYTES && limits.max_support_count;
}
result<std::size_t> pixel_count(const depth_asset_metadata& metadata,
                                const depth_asset_limits& limits) {
    if (!limits_valid(limits))
        return fail(error_code::INVALID_ARGUMENT, "Invalid depth asset limits");
    if (metadata.convention != depth_convention::AXIAL_CAMERA_Z ||
        metadata.scale != scale_status::ARBITRARY)
        return fail(error_code::UNSUPPORTED_OPERATION,
                    "Depth v1 requires axial camera-Z in arbitrary pose-camera units");
    if (!metadata.width || !metadata.height || metadata.width > limits.max_dimension ||
        metadata.height > limits.max_dimension ||
        metadata.width > limits.max_pixels / metadata.height)
        return fail(error_code::RESOURCE_EXHAUSTED, "Depth grid exceeds pixel budget");
    const auto& dependency = metadata.dependency;
    if ((metadata.producer != depth_producer_kind::SYNTHETIC_DIAGNOSTIC &&
         metadata.producer != depth_producer_kind::MULTIVIEW_STEREO) ||
        !dependency.pose_revision.value || !dependency.frame.value ||
        !dependency.calibration.value || !digest_valid(dependency.pose_asset_sha256) ||
        !digest_valid(dependency.observation_image_sha256) ||
        !digest_valid(metadata.calibrated_image_sha256) || !digest_valid(metadata.recipe_sha256) ||
        !project(metadata.camera, {0, 0, 1}))
        return fail(error_code::INVALID_DATA, "Invalid depth identity or calibrated camera");
    return std::size_t{metadata.width} * metadata.height;
}
result<std::size_t> payload_size(std::size_t pixels, bool uncertainty,
                                 const depth_asset_limits& limits) {
    const std::size_t stride = uncertainty ? 17 : 13;
    if (pixels > limits.payload_bytes.bytes / stride || limits.encoded_bytes.bytes < HEADER_BYTES ||
        pixels > (limits.encoded_bytes.bytes - HEADER_BYTES) / stride)
        return fail(error_code::RESOURCE_EXHAUSTED, "Depth planes exceed byte budget");
    return pixels * stride;
}
result<std::string> checked_hash(std::span<const std::byte> bytes, const std::stop_token& stop) {
    sha256_builder builder;
    for (std::size_t offset = 0; offset < bytes.size();) {
        if (stop.stop_requested())
            return fail(error_code::CANCELLED, "Depth checksum cancelled");
        const auto count = std::min(HASH_BLOCK_BYTES, bytes.size() - offset);
        builder.update(bytes.subspan(offset, count));
        offset += count;
    }
    return builder.finish();
}
struct encoder {
    std::vector<std::byte> bytes;
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): scalar value and wire byte width.
    void integer(std::uint64_t value, std::size_t width) {
        for (std::size_t i = 0; i < width; ++i) {
            bytes.push_back(static_cast<std::byte>(value & 255U));
            value >>= 8U;
        }
    }
    void text(std::string_view value) {
        for (const auto c : value)
            integer(static_cast<unsigned char>(c), 1);
    }
    template <class value_type>
    result<void> plane(const std::vector<value_type>& values, const std::stop_token& stop) {
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (i % 4096 == 0 && stop.stop_requested())
                return fail(error_code::CANCELLED, "Depth encoding cancelled");
            if constexpr (std::is_same_v<value_type, float>)
                integer(std::bit_cast<std::uint32_t>(values[i]), 4);
            else
                integer(values[i], sizeof(value_type));
        }
        return {};
    }
};
struct decoder {
    std::span<const std::byte> bytes;
    std::size_t position{};
    std::uint64_t integer(std::size_t width) {
        if (width > bytes.size() - position)
            throw std::runtime_error("Truncated depth asset");
        std::uint64_t value{};
        for (std::size_t i = 0; i < width; ++i)
            value |= std::uint64_t{std::to_integer<unsigned char>(bytes[position++])} << (i * 8);
        return value;
    }
    std::string text(std::size_t count) {
        if (count > bytes.size() - position)
            throw std::runtime_error("Truncated depth metadata");
        std::string value;
        value.reserve(count);
        for (std::size_t i = 0; i < count; ++i)
            value.push_back(static_cast<char>(integer(1)));
        return value;
    }
    template <class value_type>
    result<void> plane(std::vector<value_type>& values, std::size_t pixels,
                       const std::stop_token& stop) {
        values.resize(pixels);
        for (std::size_t i = 0; i < pixels; ++i) {
            if (i % 4096 == 0 && stop.stop_requested())
                return fail(error_code::CANCELLED, "Depth decoding cancelled");
            if constexpr (std::is_same_v<value_type, float>)
                values[i] = std::bit_cast<float>(static_cast<std::uint32_t>(integer(4)));
            else
                values[i] = static_cast<value_type>(integer(sizeof(value_type)));
        }
        return {};
    }
};
bool positive_zero(float value) { return value == 0 && !std::signbit(value); }
} // namespace

result<std::size_t> validate_depth_asset(const depth_image_asset& asset, depth_asset_limits limits,
                                         const std::stop_token& stop) {
    if (stop.stop_requested())
        return fail(error_code::CANCELLED, "Depth validation cancelled");
    const auto pixels = pixel_count(asset.metadata, limits);
    if (!pixels)
        return std::unexpected(pixels.error());
    const auto size = payload_size(*pixels, asset.standard_deviation.has_value(), limits);
    if (!size)
        return std::unexpected(size.error());
    if (asset.axial_depth.size() != *pixels || asset.valid.size() != *pixels ||
        asset.confidence.size() != *pixels || asset.support_count.size() != *pixels ||
        (asset.standard_deviation && asset.standard_deviation->size() != *pixels))
        return fail(error_code::INVALID_DATA, "Depth plane dimensions disagree");
    for (std::size_t i = 0; i < *pixels; ++i) {
        if (i % 4096 == 0 && stop.stop_requested())
            return fail(error_code::CANCELLED, "Depth validation cancelled");
        const auto z = asset.axial_depth[i];
        const auto weight = asset.confidence[i];
        if (asset.valid[i] > 1 || !std::isfinite(z) || !std::isfinite(weight) || weight < 0 ||
            weight > 1 || asset.support_count[i] > limits.max_support_count)
            return fail(error_code::INVALID_DATA, "Invalid depth sample or confidence");
        if (asset.valid[i]) {
            if (z <= 0 || asset.support_count[i] == 0)
                return fail(error_code::INVALID_DATA,
                            "Valid depth requires positive Z and support");
        } else if (!positive_zero(z) || !positive_zero(weight) || asset.support_count[i] != 0) {
            return fail(error_code::INVALID_DATA,
                        "Invalid depth must retain explicit unknown samples");
        }
        if (asset.standard_deviation) {
            const auto sigma = (*asset.standard_deviation)[i];
            if (!std::isfinite(sigma) || sigma < 0 || (!asset.valid[i] && !positive_zero(sigma)))
                return fail(error_code::INVALID_DATA, "Invalid depth uncertainty");
        }
    }
    return size;
}

result<void> admit_depth_asset(const depth_image_asset& asset, const depth_asset_metadata& expected,
                               depth_asset_limits limits, const std::stop_token& stop) {
    const auto checked = validate_depth_asset(asset, limits, stop);
    if (!checked)
        return std::unexpected(checked.error());
    const auto expected_pixels = pixel_count(expected, limits);
    if (!expected_pixels)
        return std::unexpected(expected_pixels.error());
    const auto& actual = asset.metadata;
    if (actual.dependency.epoch != expected.dependency.epoch)
        return fail(error_code::WRONG_EPOCH,
                    "Depth world epoch changed; resynchronization required");
    if (actual.dependency.pose_revision != expected.dependency.pose_revision)
        return fail(error_code::WRONG_BASE,
                    "Depth pose revision changed; resynchronization required");
    if (actual.dependency.pose_asset_sha256 != expected.dependency.pose_asset_sha256)
        return fail(error_code::RESYNC_REQUIRED,
                    "Depth depends on a different canonical pose asset");
    if (actual.dependency != expected.dependency || actual.width != expected.width ||
        actual.height != expected.height || actual.camera.fx != expected.camera.fx ||
        actual.camera.fy != expected.camera.fy || actual.camera.cx != expected.camera.cx ||
        actual.camera.cy != expected.camera.cy || actual.recipe_sha256 != expected.recipe_sha256 ||
        actual.producer != expected.producer ||
        actual.calibrated_image_sha256 != expected.calibrated_image_sha256)
        return fail(error_code::INVALID_DATA,
                    "Depth observation, recipe or calibrated grid mismatch");
    return {};
}

result<std::vector<std::byte>> encode_depth_asset(const depth_image_asset& asset,
                                                  depth_asset_limits limits,
                                                  const std::stop_token& stop) {
    try {
        const auto checked = validate_depth_asset(asset, limits, stop);
        if (!checked)
            return std::unexpected(checked.error());
        encoder output;
        output.bytes.reserve(HEADER_BYTES + *checked);
        const auto& metadata = asset.metadata;
        const auto& dependency = metadata.dependency;
        output.text("WEXDEP01");
        for (const auto value : {1U, static_cast<std::uint32_t>(metadata.convention),
                                 static_cast<std::uint32_t>(metadata.producer),
                                 static_cast<std::uint32_t>(metadata.scale), metadata.width,
                                 metadata.height, asset.standard_deviation ? 1U : 0U})
            output.integer(value, 4);
        for (const auto value : {dependency.epoch.value, dependency.pose_revision.value,
                                 dependency.frame.value, dependency.calibration.value})
            output.integer(value, 8);
        for (const auto value :
             {metadata.camera.fx, metadata.camera.fy, metadata.camera.cx, metadata.camera.cy})
            output.integer(std::bit_cast<std::uint64_t>(value), 8);
        for (const auto& value : {dependency.pose_asset_sha256, dependency.observation_image_sha256,
                                  metadata.calibrated_image_sha256, metadata.recipe_sha256})
            output.text(value);
        auto status = output.plane(asset.axial_depth, stop);
        if (status)
            status = output.plane(asset.valid, stop);
        if (status)
            status = output.plane(asset.confidence, stop);
        if (status)
            status = output.plane(asset.support_count, stop);
        if (status && asset.standard_deviation)
            status = output.plane(*asset.standard_deviation, stop);
        if (!status)
            return std::unexpected(status.error());
        return std::move(output.bytes);
    } catch (const std::bad_alloc&) {
        return fail(error_code::RESOURCE_EXHAUSTED, "Depth encoding allocation failed");
    } catch (const std::exception& exception) {
        return fail(error_code::INVALID_DATA,
                    std::string("Depth encoding failed: ") + exception.what());
    }
}

result<depth_image_asset> decode_depth_asset(std::span<const std::byte> bytes,
                                             const std::string& expected_sha256,
                                             depth_asset_limits limits,
                                             const std::stop_token& stop) {
    try {
        if (stop.stop_requested())
            return fail(error_code::CANCELLED, "Depth decoding cancelled");
        if (!limits_valid(limits))
            return fail(error_code::INVALID_ARGUMENT, "Invalid depth asset limits");
        if (bytes.size() > limits.encoded_bytes.bytes)
            return fail(error_code::RESOURCE_EXHAUSTED, "Depth file exceeds encoded byte budget");
        if (bytes.size() < HEADER_BYTES || !digest_valid(expected_sha256))
            return fail(error_code::INVALID_DATA, "Invalid depth file or checksum");
        const auto hash = checked_hash(bytes, stop);
        if (!hash)
            return std::unexpected(hash.error());
        if (*hash != expected_sha256)
            return fail(error_code::INVALID_DATA, "Depth checksum mismatch");
        decoder input{bytes};
        if (input.text(8) != "WEXDEP01" || input.integer(4) != 1)
            return fail(error_code::UNSUPPORTED_OPERATION, "Unsupported depth asset version");
        depth_image_asset asset;
        auto& metadata = asset.metadata;
        const auto convention = input.integer(4);
        if (convention != static_cast<std::uint32_t>(depth_convention::AXIAL_CAMERA_Z))
            return fail(error_code::UNSUPPORTED_OPERATION, "Unsupported depth convention");
        const auto producer = input.integer(4);
        if (producer != static_cast<std::uint32_t>(depth_producer_kind::SYNTHETIC_DIAGNOSTIC) &&
            producer != static_cast<std::uint32_t>(depth_producer_kind::MULTIVIEW_STEREO))
            return fail(error_code::INVALID_DATA, "Unsupported depth producer");
        metadata.producer = static_cast<depth_producer_kind>(producer);
        const auto scale = input.integer(4);
        if (scale != static_cast<std::uint32_t>(scale_status::ARBITRARY))
            return fail(error_code::UNSUPPORTED_OPERATION, "Unsupported depth scale provenance");
        metadata.width = static_cast<std::uint32_t>(input.integer(4));
        metadata.height = static_cast<std::uint32_t>(input.integer(4));
        const auto uncertainty = input.integer(4);
        if (uncertainty > 1)
            return fail(error_code::INVALID_DATA, "Unknown depth uncertainty encoding");
        auto& dependency = metadata.dependency;
        dependency.epoch = {input.integer(8)};
        dependency.pose_revision = {input.integer(8)};
        dependency.frame = {input.integer(8)};
        dependency.calibration = {input.integer(8)};
        metadata.camera = {
            std::bit_cast<double>(input.integer(8)), std::bit_cast<double>(input.integer(8)),
            std::bit_cast<double>(input.integer(8)), std::bit_cast<double>(input.integer(8))};
        dependency.pose_asset_sha256 = input.text(64);
        dependency.observation_image_sha256 = input.text(64);
        metadata.calibrated_image_sha256 = input.text(64);
        metadata.recipe_sha256 = input.text(64);
        const auto pixels = pixel_count(metadata, limits);
        if (!pixels)
            return std::unexpected(pixels.error());
        const auto size = payload_size(*pixels, uncertainty != 0, limits);
        if (!size)
            return std::unexpected(size.error());
        if (*size != bytes.size() - input.position)
            return fail(error_code::INVALID_DATA,
                        "Depth planes are truncated or have trailing data");
        auto status = input.plane(asset.axial_depth, *pixels, stop);
        if (status)
            status = input.plane(asset.valid, *pixels, stop);
        if (status)
            status = input.plane(asset.confidence, *pixels, stop);
        if (status)
            status = input.plane(asset.support_count, *pixels, stop);
        if (status && uncertainty)
            status = input.plane(asset.standard_deviation.emplace(), *pixels, stop);
        if (!status)
            return std::unexpected(status.error());
        const auto checked = validate_depth_asset(asset, limits, stop);
        if (!checked)
            return std::unexpected(checked.error());
        return asset;
    } catch (const std::bad_alloc&) {
        return fail(error_code::RESOURCE_EXHAUSTED, "Depth decoding allocation failed");
    } catch (const std::exception& exception) {
        return fail(error_code::INVALID_DATA,
                    std::string("Depth decoding failed: ") + exception.what());
    }
}

result<std::string> write_depth_asset(const std::filesystem::path& path,
                                      const depth_image_asset& asset, depth_asset_limits limits,
                                      const std::stop_token& stop) {
    const auto encoded = encode_depth_asset(asset, limits, stop);
    if (!encoded)
        return std::unexpected(encoded.error());
    return asset_files::publish_asset(path, *encoded, stop);
}
result<depth_image_asset> read_depth_asset(const std::filesystem::path& path,
                                           const std::string& expected_sha256,
                                           const depth_asset_metadata& expected,
                                           depth_asset_limits limits, const std::stop_token& stop) {
    if (!limits_valid(limits))
        return fail(error_code::INVALID_ARGUMENT, "Invalid depth asset limits");
    const auto bytes = asset_files::read_bytes(path, limits.encoded_bytes.bytes, stop);
    if (!bytes)
        return std::unexpected(bytes.error());
    auto decoded = decode_depth_asset(*bytes, expected_sha256, limits, stop);
    if (!decoded)
        return std::unexpected(decoded.error());
    const auto admitted = admit_depth_asset(*decoded, expected, limits, stop);
    if (!admitted)
        return std::unexpected(admitted.error());
    return decoded;
}
} // namespace world_explorer
