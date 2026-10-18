#ifndef WORLD_EXPLORER_TESTS_GPU_FIT_VIEW_SELECTION_HPP
#define WORLD_EXPLORER_TESTS_GPU_FIT_VIEW_SELECTION_HPP

#include "world_explorer/core/result.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <string_view>
#include <vector>

namespace world_explorer {

// Diagnostic-only, zero-based original fit indices. Order determines admission origin.
inline Result<std::vector<std::size_t>> parse_fit_view_indices(std::string_view input,
                                                               std::size_t frame_count) {
    if (frame_count < 3 || frame_count > 120 || input.empty() || input.size() > 479)
        return fail(ErrorCode::INVALID_ARGUMENT, "fit index selection exceeds diagnostic budget");
    std::array<bool, 120> seen {};
    std::vector<std::size_t> indices;
    while (!input.empty()) {
        const auto end = input.find(',');
        const auto token = input.substr(0, end);
        std::size_t index {};
        const auto parsed = std::from_chars(token.data(), token.data() + token.size(), index);
        if (token.empty() || token.size() > 3 || (token.size() > 1 && token.front() == '0') ||
            parsed.ec != std::errc {} || parsed.ptr != token.data() + token.size() ||
            index >= frame_count || seen[index])
            return fail(ErrorCode::INVALID_ARGUMENT, "invalid or duplicate fit index selection");
        seen[index] = true;
        indices.push_back(index);
        if (end == std::string_view::npos) break;
        input.remove_prefix(end + 1);
        if (input.empty())
            return fail(ErrorCode::INVALID_ARGUMENT, "empty trailing fit index selection");
    }
    return indices;
}

}  // namespace world_explorer

#endif  // WORLD_EXPLORER_TESTS_GPU_FIT_VIEW_SELECTION_HPP
