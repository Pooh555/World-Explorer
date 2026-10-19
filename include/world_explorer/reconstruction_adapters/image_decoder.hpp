#ifndef WORLD_EXPLORER_RECONSTRUCTION_ADAPTERS_IMAGE_DECODER_HPP
#define WORLD_EXPLORER_RECONSTRUCTION_ADAPTERS_IMAGE_DECODER_HPP

#include "world_explorer/core/result.hpp"

#include <filesystem>
#include <stop_token>
#include <vector>

namespace world_explorer {

// Bounded RGB8 decode of a pinned calibrated still image. No rotation/resize.
// FFmpeg runs as a cancellable/reaped child with argument arrays.
Result<std::vector<std::byte>> decode_calibrated_rgb(const std::filesystem::path& path,
                                                     const std::string& expected_sha256,
                                                     std::uint32_t width,
                                                     std::uint32_t height,
                                                     const std::stop_token& stop = {});

}  // namespace world_explorer
#endif
