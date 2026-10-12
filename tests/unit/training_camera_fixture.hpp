#ifndef WORLD_EXPLORER_TEST_TRAINING_CAMERA_FIXTURE_HPP
#define WORLD_EXPLORER_TEST_TRAINING_CAMERA_FIXTURE_HPP
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace world_explorer::test {
inline std::vector<std::byte>
training_camera_bytes(std::uint32_t model = 1, std::uint64_t width = 128,
                      std::uint64_t height = 128,
                      std::array<double, 4> parameters = {200, 200, 64, 64}) {
    std::vector<std::byte> bytes;
    const auto append = [&](std::uint64_t value, std::size_t count) {
        for (std::size_t index = 0; index < count; ++index)
            bytes.push_back(static_cast<std::byte>((value >> (index * 8)) & 255));
    };
    append(1, 8);
    append(1, 4);
    append(model, 4);
    append(width, 8);
    append(height, 8);
    for (std::size_t index = 0; index < (model == 0 ? 3U : 4U); ++index)
        append(std::bit_cast<std::uint64_t>(parameters[index]), 8);
    return bytes;
}
inline void write_training_camera(const std::filesystem::path& file,
                                  const std::vector<std::byte>& bytes) {
    std::ofstream output(file, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
}
} // namespace world_explorer::test
#endif
