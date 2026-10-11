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

} // namespace world_explorer::viewer
#endif // WORLD_EXPLORER_VIEWER_PREFERENCES_HPP
