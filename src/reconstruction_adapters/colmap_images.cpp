#include "world_explorer/reconstruction_adapters/colmap_images.hpp"

#include "world_explorer/core/asset_files.hpp"
#include "world_explorer/core/hash.hpp"

#include <bit>
#include <limits>
#include <set>
#include <stdexcept>

namespace world_explorer {
namespace {

struct Reader {
    std::span<const std::byte> m_bytes;

    std::uint64_t integer(std::size_t count) {
        if (count > m_bytes.size()) throw std::invalid_argument("Truncated COLMAP images");
        std::uint64_t value {};
        for (std::size_t i { 0uz }; i < count; ++i)
            value |= std::uint64_t { std::to_integer<unsigned char>(m_bytes[i]) } << (i * 8);
        m_bytes = m_bytes.subspan(count);
        return value;
    }

    double number() {
        const auto K_VALUE = std::bit_cast<double>(integer(8));
        if (!std::isfinite(K_VALUE)) throw std::invalid_argument("Nonfinite COLMAP image value");
        return K_VALUE;
    }

    std::string name() {
        std::string value;
        for (std::size_t i { 0uz }; i <= 255uz; ++i) {
            const auto K_CHARACTER = static_cast<char>(integer(1));
            if (!K_CHARACTER) {
                if (value.empty() || value == "." || value == "..") break;
                return value;
            }
            if (static_cast<unsigned char>(K_CHARACTER) < 32 || K_CHARACTER == '/' ||
                K_CHARACTER == '\\' || K_CHARACTER == ':')
                break;
            value += K_CHARACTER;
        }
        throw std::invalid_argument("Unsafe or oversized COLMAP image name");
    }
};

}  // namespace

Result<std::vector<ColmapImagePose>> read_colmap_image_poses(const std::filesystem::path& file,
                                                             const std::string& expected_sha256,
                                                             const std::stop_token& stop) {
    try {
        const auto K_BYTES = asset_files::read_bytes(file, 256uz * 1024 * 1024, stop);
        if (!K_BYTES) return std::unexpected(K_BYTES.error());
        if (sha256(*K_BYTES) != expected_sha256)
            return fail(ErrorCode::INVALID_DATA, "COLMAP image checksum differs");
        Reader input { *K_BYTES };
        const auto K_COUNT = input.integer(8);
        if (!K_COUNT || K_COUNT > 120)
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "COLMAP image count exceeds profile");
        std::vector<ColmapImagePose> images;
        std::set<std::uint32_t> ids;
        std::set<std::string> names;
        for (std::uint64_t i { 0 }; i < K_COUNT; ++i) {
            if (stop.stop_requested())
                return fail(ErrorCode::CANCELLED, "COLMAP image read cancelled");
            ColmapImagePose image;
            image.m_image_id = static_cast<std::uint32_t>(input.integer(4));
            const Quatd K_ROTATION { input.number(), input.number(), input.number(), input.number() };
            const Vec3d K_TRANSLATION { input.number(), input.number(), input.number() };
            image.m_camera_id = static_cast<std::uint32_t>(input.integer(4));
            image.m_name = input.name();
            constexpr auto K_MAX_ID {
                static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())};
            if (!image.m_image_id || image.m_image_id > K_MAX_ID || !image.m_camera_id ||
                image.m_camera_id > K_MAX_ID || !valid(K_ROTATION) ||
                !ids.insert(image.m_image_id).second || !names.insert(image.m_name).second)
                return fail(ErrorCode::INVALID_DATA, "Invalid/duplicate COLMAP image identity");
            const Quatd K_INVERSE {
                K_ROTATION.m_w, -K_ROTATION.m_x, -K_ROTATION.m_y, -K_ROTATION.m_z};
            const auto K_CENTER = rotate(K_INVERSE, K_TRANSLATION) * -1;
            image.m_world_from_camera = {
                { -K_INVERSE.m_x, K_INVERSE.m_w, -K_INVERSE.m_z, K_INVERSE.m_y },
                { K_CENTER.m_x, -K_CENTER.m_y, -K_CENTER.m_z },
                1 };
            if (!valid(image.m_world_from_camera))
                return fail(ErrorCode::INVALID_DATA, "Invalid canonical COLMAP pose");
            const auto K_POINTS = input.integer(8);
            if (K_POINTS > 2000000 || K_POINTS > input.m_bytes.size() / 24)
                return fail(ErrorCode::RESOURCE_EXHAUSTED, "COLMAP track count exceeds profile");
            for (std::uint64_t point { 0 }; point < K_POINTS; ++point) {
                if (point % 4096 == 0 && stop.stop_requested())
                    return fail(ErrorCode::CANCELLED, "COLMAP track read cancelled");
                input.number();
                input.number();
                input.integer(8);
            }
            images.push_back(std::move(image));
        }
        if (!input.m_bytes.empty())
            return fail(ErrorCode::INVALID_DATA, "Trailing COLMAP image bytes");
        return images;
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "COLMAP image allocation failed");
    } catch (const std::exception& exception) {
        return fail(ErrorCode::INVALID_DATA, exception.what());
    }
}

}  // namespace world_explorer
