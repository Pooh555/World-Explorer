#include "world_explorer/reconstruction_adapters/training_camera.hpp"
#include "world_explorer/core/hash.hpp"
#include <array>
#include <bit>
#include <fstream>
#include <limits>
#include <span>

namespace world_explorer {
namespace {
std::uint64_t little_endian(std::span<const std::byte> bytes) {
    std::uint64_t value{};
    for (std::size_t index = 0; index < bytes.size(); ++index)
        value |= std::uint64_t{std::to_integer<unsigned char>(bytes[index])} << (index * 8);
    return value;
}
} // namespace
result<training_camera> read_colmap_training_camera(const std::filesystem::path& file,
                                                    const std::string& expected_sha256,
                                                    const std::stop_token& stop) {
    if (stop.stop_requested())
        return fail(error_code::CANCELLED, "Training camera import cancelled");
    std::ifstream input(file, std::ios::binary);
    if (!input)
        return fail(error_code::IO_ERROR, "Training camera input cannot be opened");
    std::array<char, 65> storage{};
    input.read(storage.data(), static_cast<std::streamsize>(storage.size()));
    if (input.bad())
        return fail(error_code::IO_ERROR, "Training camera input cannot be read");
    if (!input.eof())
        return fail(error_code::RESOURCE_EXHAUSTED,
                    "Training camera exceeds single-camera profile");
    const auto bytes =
        std::as_bytes(std::span(storage)).first(static_cast<std::size_t>(input.gcount()));
    if (stop.stop_requested())
        return fail(error_code::CANCELLED, "Training camera import cancelled");
    if (sha256(bytes) != expected_sha256 || bytes.size() < 32)
        return fail(error_code::INVALID_DATA,
                    "Training camera checksum/structure differs from selection");
    if (little_endian(bytes.first(8)) != 1)
        return fail(error_code::UNSUPPORTED_OPERATION, "Training requires one undistorted camera");
    const auto id = little_endian(bytes.subspan(8, 4));
    const auto model = little_endian(bytes.subspan(12, 4));
    if (!id || id > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()))
        return fail(error_code::INVALID_DATA, "Invalid training camera identifier");
    if (model > 1)
        return fail(error_code::UNSUPPORTED_OPERATION,
                    "Training camera must be undistorted pinhole");
    if (bytes.size() != (model == 0 ? 56 : 64))
        return fail(error_code::INVALID_DATA, "Truncated or trailing training camera bytes");
    const auto width = little_endian(bytes.subspan(16, 8));
    const auto height = little_endian(bytes.subspan(24, 8));
    if (width < 11 || height < 11 || width > 8192 || height > 8192 ||
        width * height > 4096ULL * 4096)
        return fail(error_code::RESOURCE_EXHAUSTED,
                    "Training camera dimensions exceed fitting profile");
    const auto parameter = [&](std::size_t index) {
        return std::bit_cast<double>(little_endian(bytes.subspan(32 + index * 8, 8)));
    };
    const auto fx = parameter(0);
    const auto fy = model == 0 ? fx : parameter(1);
    pinhole_camera camera{fx, fy, parameter(model == 0 ? 1 : 2) - .5,
                          parameter(model == 0 ? 2 : 3) - .5};
    if (!project(camera, {0, 0, 1}))
        return fail(error_code::INVALID_DATA,
                    "Training camera parameters are nonfinite or invalid");
    return training_camera{camera, static_cast<std::uint32_t>(width),
                           static_cast<std::uint32_t>(height)};
}
} // namespace world_explorer
