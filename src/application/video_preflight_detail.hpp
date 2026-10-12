#ifndef WORLD_EXPLORER_APPLICATION_VIDEO_PREFLIGHT_DETAIL_HPP
#define WORLD_EXPLORER_APPLICATION_VIDEO_PREFLIGHT_DETAIL_HPP
#include "world_explorer/application/video_preflight.hpp"

namespace world_explorer::video_detail {
bool overlaps(const std::filesystem::path& first, const std::filesystem::path& second);
result<void> check_storage(const std::vector<std::filesystem::path>& outputs,
                           std::uint64_t minimum_free_bytes, const std::stop_token& stop);
} // namespace world_explorer::video_detail
#endif
