#include "world_explorer/scene_store/bundle.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>

namespace {
using namespace world_explorer;
constexpr std::size_t MAX_INPUT_BYTES = 65536;
constexpr std::size_t MAX_HEADER_BYTES = 8192;
constexpr std::size_t MAX_SPLATS = 64;
class fuzz_input_file {
  public:
    fuzz_input_file() {
        const std::filesystem::path root(WE_FUZZ_SCRATCH_ROOT);
        std::filesystem::create_directories(root);
        if (!std::filesystem::is_directory(std::filesystem::symlink_status(root)))
            throw std::runtime_error("Fuzz scratch root must be an owned build directory");
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        for (unsigned attempt = 0; attempt < 64; ++attempt) {
            directory_ = root / (std::to_string(nonce) + "-" + std::to_string(attempt));
            if (std::filesystem::create_directory(directory_)) {
                path_ = directory_ / "synthetic.ply";
                return;
            }
        }
        throw std::runtime_error("Cannot reserve a private fuzz input directory");
    }
    ~fuzz_input_file() {
        std::error_code error;
        std::filesystem::remove_all(directory_, error);
        std::cerr << "synthetic_ply accepted=" << accepted_ << " rejected=" << rejected_
                  << " cleanup_error=" << error.value() << '\n';
    }
    void exercise(std::span<const std::uint8_t> input) {
        {
            std::ofstream output(path_, std::ios::binary | std::ios::trunc);
            if (!input.empty())
                output.write(reinterpret_cast<const char*>(input.data()),
                             static_cast<std::streamsize>(input.size()));
            output.close();
            if (!output)
                throw std::runtime_error("Cannot write private fuzz input");
        }
        bundle_limits limits;
        limits.max_ply_header_bytes = MAX_HEADER_BYTES;
        limits.scene = {.max_chunks = 1,
                        .max_splats = MAX_SPLATS,
                        .max_payload_bytes = MAX_SPLATS * sizeof(gaussian),
                        .max_source_frames = 0};
        const auto scene = load_gsplat_ply(path_, {42}, limits);
        if (scene) {
            ++accepted_;
            if (!*scene || (*scene)->id != scene_id{42} || !validate_scene(**scene, limits.scene))
                std::abort();
        } else {
            ++rejected_;
        }
        // Admission, successful or rejected, must not modify any input bytes.
        if (std::filesystem::file_size(path_) != input.size())
            std::abort();
        std::ifstream stored(path_, std::ios::binary);
        std::array<char, 4096> buffer{};
        for (std::size_t offset = 0; offset < input.size();) {
            const auto count = std::min(input.size() - offset, buffer.size());
            if (!stored.read(buffer.data(), static_cast<std::streamsize>(count)))
                std::abort();
            for (std::size_t index = 0; index < count; ++index)
                if (static_cast<std::uint8_t>(buffer[index]) != input[offset + index])
                    std::abort();
            offset += count;
        }
        if (stored.peek() != std::char_traits<char>::eof() || stored.bad())
            std::abort();
    }

  private:
    std::filesystem::path directory_, path_;
    std::size_t accepted_{}, rejected_{};
};
} // namespace

// NOLINTNEXTLINE(readability-identifier-naming): libFuzzer's required external C entry point.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size > MAX_INPUT_BYTES)
        return 0;
    // libFuzzer serializes callbacks within each process. Each process owns a private file.
    static fuzz_input_file file;
    file.exercise({data, size});
    return 0;
}
