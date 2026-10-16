#include "world_explorer/reconstruction_adapters/colmap_images.hpp"
#include "world_explorer/core/asset_files.hpp"
#include "world_explorer/core/hash.hpp"
#include <bit>
#include <limits>
#include <set>
#include <stdexcept>

namespace world_explorer {
namespace {
struct reader {
    std::span<const std::byte> bytes;
    std::uint64_t integer(std::size_t count) {
        if (count > bytes.size())
            throw std::invalid_argument("Truncated COLMAP images");
        std::uint64_t value{};
        for (std::size_t i = 0; i < count; ++i)
            value |= std::uint64_t{std::to_integer<unsigned char>(bytes[i])} << (i * 8);
        bytes = bytes.subspan(count);
        return value;
    }
    double number() {
        const auto value = std::bit_cast<double>(integer(8));
        if (!std::isfinite(value))
            throw std::invalid_argument("Nonfinite COLMAP image value");
        return value;
    }
    std::string name() {
        std::string value;
        for (std::size_t i = 0; i <= 255; ++i) {
            const auto character = static_cast<char>(integer(1));
            if (!character) {
                if (value.empty() || value == "." || value == "..")
                    break;
                return value;
            }
            if (static_cast<unsigned char>(character) < 32 || character == '/' ||
                character == '\\' || character == ':')
                break;
            value += character;
        }
        throw std::invalid_argument("Unsafe or oversized COLMAP image name");
    }
};
} // namespace
result<std::vector<colmap_image_pose>> read_colmap_image_poses(const std::filesystem::path& file,
                                                               const std::string& expected_sha256,
                                                               const std::stop_token& stop) {
    try {
        const auto bytes = asset_files::read_bytes(file, std::size_t{256} * 1024 * 1024, stop);
        if (!bytes)
            return std::unexpected(bytes.error());
        if (sha256(*bytes) != expected_sha256)
            return fail(error_code::INVALID_DATA, "COLMAP image checksum differs");
        reader input{*bytes};
        const auto count = input.integer(8);
        if (!count || count > 120)
            return fail(error_code::RESOURCE_EXHAUSTED, "COLMAP image count exceeds profile");
        std::vector<colmap_image_pose> images;
        std::set<std::uint32_t> ids;
        std::set<std::string> names;
        for (std::uint64_t i = 0; i < count; ++i) {
            if (stop.stop_requested())
                return fail(error_code::CANCELLED, "COLMAP image read cancelled");
            colmap_image_pose image;
            image.image_id = static_cast<std::uint32_t>(input.integer(4));
            const quatd rotation{input.number(), input.number(), input.number(), input.number()};
            const vec3d translation{input.number(), input.number(), input.number()};
            image.camera_id = static_cast<std::uint32_t>(input.integer(4));
            image.name = input.name();
            constexpr auto MAX_ID =
                static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());
            if (!image.image_id || image.image_id > MAX_ID || !image.camera_id ||
                image.camera_id > MAX_ID || !valid(rotation) ||
                !ids.insert(image.image_id).second || !names.insert(image.name).second)
                return fail(error_code::INVALID_DATA, "Invalid/duplicate COLMAP image identity");
            const quatd inverse{rotation.w, -rotation.x, -rotation.y, -rotation.z};
            const auto center = rotate(inverse, translation) * -1;
            image.world_from_camera = {{-inverse.x, inverse.w, -inverse.z, inverse.y},
                                       {center.x, -center.y, -center.z},
                                       1};
            if (!valid(image.world_from_camera))
                return fail(error_code::INVALID_DATA, "Invalid canonical COLMAP pose");
            const auto points = input.integer(8);
            if (points > 2000000 || points > input.bytes.size() / 24)
                return fail(error_code::RESOURCE_EXHAUSTED, "COLMAP track count exceeds profile");
            for (std::uint64_t point = 0; point < points; ++point) {
                if (point % 4096 == 0 && stop.stop_requested())
                    return fail(error_code::CANCELLED, "COLMAP track read cancelled");
                input.number();
                input.number();
                input.integer(8);
            }
            images.push_back(std::move(image));
        }
        if (!input.bytes.empty())
            return fail(error_code::INVALID_DATA, "Trailing COLMAP image bytes");
        return images;
    } catch (const std::bad_alloc&) {
        return fail(error_code::RESOURCE_EXHAUSTED, "COLMAP image allocation failed");
    } catch (const std::exception& exception) {
        return fail(error_code::INVALID_DATA, exception.what());
    }
}
} // namespace world_explorer
