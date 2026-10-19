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
    std::uint64_t value {};
    for (std::size_t index { 0uz }; index < bytes.size(); ++index) {
        value |= std::uint64_t { std::to_integer<unsigned char>(bytes[index]) } << (index * 8);
    }
    return value;
}

}  // namespace

Result<TrainingCamera> read_colmap_training_camera(const std::filesystem::path& file,
                                                   const std::string& expected_sha256,
                                                   const std::stop_token& stop) {
    if (stop.stop_requested()) {
        return fail(ErrorCode::CANCELLED, "Training camera import cancelled");
    }
    std::ifstream input(file, std::ios::binary);
    if (!input) {
        return fail(ErrorCode::IO_ERROR, "Training camera input cannot be opened");
    }
    std::array<char, 65> storage {};
    input.read(storage.data(), static_cast<std::streamsize>(storage.size()));
    if (input.bad()) {
        return fail(ErrorCode::IO_ERROR, "Training camera input cannot be read");
    }
    if (!input.eof()) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "Training camera exceeds single-camera profile");
    }
    const auto K_BYTES =
        std::as_bytes(std::span(storage)).first(static_cast<std::size_t>(input.gcount()));
    if (stop.stop_requested()) {
        return fail(ErrorCode::CANCELLED, "Training camera import cancelled");
    }
    if (sha256(K_BYTES) != expected_sha256 || K_BYTES.size() < 32) {
        return fail(ErrorCode::INVALID_DATA,
                    "Training camera checksum/structure differs from selection");
    }
    if (little_endian(K_BYTES.first(8)) != 1) {
        return fail(ErrorCode::UNSUPPORTED_OPERATION, "Training requires one undistorted camera");
    }
    const auto K_ID = little_endian(K_BYTES.subspan(8, 4));
    const auto K_MODEL = little_endian(K_BYTES.subspan(12, 4));
    if (!K_ID || K_ID > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())) {
        return fail(ErrorCode::INVALID_DATA, "Invalid training camera identifier");
    }
    if (K_MODEL > 1) {
        return fail(ErrorCode::UNSUPPORTED_OPERATION,
                    "Training camera must be undistorted pinhole");
    }
    if (K_BYTES.size() != (K_MODEL == 0 ? 56 : 64)) {
        return fail(ErrorCode::INVALID_DATA, "Truncated or trailing training camera bytes");
    }
    const auto K_WIDTH = little_endian(K_BYTES.subspan(16, 8));
    const auto K_HEIGHT = little_endian(K_BYTES.subspan(24, 8));
    if (K_WIDTH < 11 || K_HEIGHT < 11 || K_WIDTH > 8192 || K_HEIGHT > 8192 ||
        K_WIDTH * K_HEIGHT > 4096ULL * 4096) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED,
                    "Training camera dimensions exceed fitting profile");
    }
    const auto K_PARAMETER = [&](std::size_t index) {
        return std::bit_cast<double>(little_endian(K_BYTES.subspan(32 + index * 8, 8)));
    };
    const auto K_FX = K_PARAMETER(0);
    const auto K_FY = K_MODEL == 0 ? K_FX : K_PARAMETER(1);
    PinholeCamera camera {
        K_FX, K_FY, K_PARAMETER(K_MODEL == 0 ? 1 : 2) - .5, K_PARAMETER(K_MODEL == 0 ? 2 : 3) - .5 };
    if (!project(camera, { 0, 0, 1 })) {
        return fail(ErrorCode::INVALID_DATA, "Training camera parameters are nonfinite or invalid");
    }
    return TrainingCamera { camera,
                           static_cast<std::uint32_t>(K_WIDTH),
                           static_cast<std::uint32_t>(K_HEIGHT),
                           static_cast<std::uint32_t>(K_ID) };
}

}  // namespace world_explorer
