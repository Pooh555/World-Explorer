#include "preferences.hpp"
#include <SDL3/SDL_scancode.h>
#include <algorithm>
#include <fstream>
#include <imgui.h>
#include <nlohmann/json.hpp>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace world_explorer::viewer {
std::array<int, 7> default_fly_key_bindings() {
    return {SDL_SCANCODE_W,     SDL_SCANCODE_S,      SDL_SCANCODE_A,    SDL_SCANCODE_D,
            SDL_SCANCODE_SPACE, SDL_SCANCODE_LSHIFT, SDL_SCANCODE_LCTRL};
}
void read_preferences(const std::filesystem::path& path, preferences& settings) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) ||
        std::filesystem::file_size(path, error) > 65536)
        return;
    try {
        std::ifstream input(path);
        const auto value = nlohmann::json::parse(input);
        if (value.at("schema_version") != 1)
            return;
        preferences candidate;
        candidate.left = value.value("left_panel", true);
        candidate.right = value.value("right_panel", true);
        candidate.controls = value.value("controls", true);
        const auto keys = value.at("keys").get<std::array<int, 7>>();
        if (!std::ranges::all_of(keys, [](int key) { return key > 0 && key < SDL_SCANCODE_COUNT; }))
            return;
        const auto legacy_default = std::array<int, 7>{
            SDL_SCANCODE_W,     SDL_SCANCODE_S,     SDL_SCANCODE_A,     SDL_SCANCODE_D,
            SDL_SCANCODE_SPACE, SDL_SCANCODE_LCTRL, SDL_SCANCODE_LSHIFT};
        if (keys == legacy_default) {
            candidate.keys = default_fly_key_bindings();
        } else
            candidate.keys = keys;
        settings = candidate;
    } catch (const std::exception&) {
        // Invalid/unknown settings retain the caller's current defaults atomically.
        return;
    }
}
result<void> save_preferences(const std::filesystem::path& path, const preferences& settings) {
    if (path.empty())
        return {};
    try {
        const auto temporary = std::filesystem::path(path.string() + ".tmp");
        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path());
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            output << nlohmann::json{{"schema_version", 1},
                                     {"left_panel", settings.left},
                                     {"right_panel", settings.right},
                                     {"controls", settings.controls},
                                     {"keys", settings.keys}}
                          .dump(2);
            output.flush();
            if (!output)
                throw std::runtime_error("Cannot write preferences");
        }
#ifdef _WIN32
        // Windows rename does not replace an existing destination.
        if (!MoveFileExW(temporary.c_str(), path.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot replace preferences");
#else
        std::filesystem::rename(temporary, path);
#endif
        return {};
    } catch (const std::exception& error) {
        return fail(error_code::IO_ERROR, error.what());
    }
}
void apply_theme(float scale) {
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowPadding = {16, 16};
    style.FramePadding = {8, 6};
    style.ItemSpacing = {8, 8};
    style.WindowRounding = 5;
    style.FrameRounding = 4;
    style.Colors[ImGuiCol_WindowBg] = {0.075F, 0.085F, 0.10F, 0.98F};
    style.Colors[ImGuiCol_Button] = {0.10F, 0.28F, 0.34F, 1};
    style.Colors[ImGuiCol_ButtonHovered] = {0.13F, 0.40F, 0.48F, 1};
    style.Colors[ImGuiCol_ButtonActive] = {0.13F, 0.47F, 0.55F, 1};
    style.Colors[ImGuiCol_CheckMark] = {0.3F, 0.8F, 0.87F, 1};
    style.ScaleAllSizes(scale);
}

} // namespace world_explorer::viewer
