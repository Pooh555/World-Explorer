#include "world_explorer/reconstruction_adapters/image_decoder.hpp"

#include "world_explorer/core/hash.hpp"
#include "world_explorer/reconstruction_adapters/process.hpp"

#include <algorithm>
#include <new>

namespace world_explorer {

Result<std::vector<std::byte>> decode_calibrated_rgb(const std::filesystem::path& path,
                                                     const std::string& expected_sha256,
                                                     std::uint32_t width,
                                                     std::uint32_t height,
                                                     const std::stop_token& stop) {
    try {
        if (!width || !height || width > 4096 || height > 4096) {
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "RGB image dimensions exceed budget");
        }
        const auto K_INITIAL = sha256_file(path, 64uz * 1024uz * 1024uz, stop);
        if (!K_INITIAL) {
            return std::unexpected(K_INITIAL.error());
        }
        if (*K_INITIAL != expected_sha256) {
            return fail(ErrorCode::INVALID_DATA, "RGB checksum differs");
        }
        const auto K_UTF8 = path.u8string();
        const std::vector<std::string> K_COMMAND {
            "ffmpeg",    "-nostdin",   "-hide_banner", "-loglevel",
            "error",     "-max_alloc", "67108864",     "-noautorotate",
            "-threads",  "1",          "-i",           std::string(K_UTF8.begin(), K_UTF8.end()),
            "-frames:v", "1",          "-threads",     "1",
            "-f",        "image2pipe", "-c:v",         "ppm",
            "-pix_fmt",  "rgb24",      "pipe:1" };
        const auto K_SIZE = static_cast<std::size_t>(width) * height * 3uz;
        const auto K_HEADER =
            "P6\n" + std::to_string(width) + " " + std::to_string(height) + "\n255\n";
        const auto K_OUTPUT_SIZE = K_SIZE + K_HEADER.size();
        std::vector<std::byte> rgb;
        rgb.reserve(K_OUTPUT_SIZE);
        std::stop_source cancellation;
        std::stop_callback forward(stop, [&] {
            cancellation.request_stop();
        });
        bool oversized = false;
        std::string diagnostic;
        const auto K_RESULT = execute_process(
            K_COMMAND,
            cancellation.get_token(),
            [&](const std::string& output) {
                if (output.size() > K_OUTPUT_SIZE - rgb.size()) {
                    oversized = true;
                    cancellation.request_stop();
                } else {
                    const auto K_BYTES = std::as_bytes(std::span(output));
                    rgb.insert(rgb.end(), K_BYTES.begin(), K_BYTES.end());
                }
            },
            [&](const std::string& output) {
                diagnostic.append(output, 0uz, std::min(output.size(), 4096uz - diagnostic.size()));
            });
        if (stop.stop_requested()) {
            return fail(ErrorCode::CANCELLED, "RGB decode cancelled");
        }
        if (oversized) {
            return fail(ErrorCode::RESOURCE_EXHAUSTED, "RGB decoder output exceeds budget");
        }
        if (!K_RESULT) {
            return std::unexpected(K_RESULT.error());
        }
        if (*K_RESULT || rgb.size() != K_OUTPUT_SIZE ||
            !std::ranges::equal(std::span(rgb).first(K_HEADER.size()),
                                std::as_bytes(std::span(K_HEADER)))) {
            return fail(ErrorCode::BACKEND_FAILURE,
                        "RGB decoder failed or image dimensions differ: " + diagnostic);
        }
        const auto K_FINAL = sha256_file(path, 64uz * 1024uz * 1024uz, stop);
        if (!K_FINAL) {
            return std::unexpected(K_FINAL.error());
        }
        if (*K_FINAL != expected_sha256) {
            return fail(ErrorCode::INVALID_DATA, "RGB changed during decode");
        }
        rgb.erase(rgb.begin(), rgb.begin() + static_cast<std::ptrdiff_t>(K_HEADER.size()));
        return rgb;
    } catch (const std::bad_alloc&) {
        return fail(ErrorCode::RESOURCE_EXHAUSTED, "RGB decode allocation failed");
    } catch (const std::exception& exception) {
        return fail(ErrorCode::IO_ERROR, exception.what());
    }
}

}  // namespace world_explorer
