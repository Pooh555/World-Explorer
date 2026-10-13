#ifndef WORLD_EXPLORER_VIEWER_PREFERENCES_HPP
#define WORLD_EXPLORER_VIEWER_PREFERENCES_HPP

#include "world_explorer/core/result.hpp"
#include <array>
#include <filesystem>

namespace world_explorer::viewer {

std::array<int, 7> default_fly_key_bindings();
struct preferences {
    bool left{true}, right{true}, diagnostics{false}, controls{true};
    std::array<int, 7> keys{default_fly_key_bindings()};
};

void read_preferences(const std::filesystem::path& path, preferences& settings);
result<void> save_preferences(const std::filesystem::path& path, const preferences& settings);
void apply_theme(float display_scale);
struct ui_scaling {
    float layout_scale{}, font_scale{};
    std::size_t font_index{};
};
// Font atlas contains 13 immutable sizes from 1x through 4x in quarter steps.
// ImGui uses window coordinates; SDL display scale uses physical pixels.
result<ui_scaling> display_scaling(float display_scale, float pixel_density);

} // namespace world_explorer::viewer
#endif // WORLD_EXPLORER_VIEWER_PREFERENCES_HPP
