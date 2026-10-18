#include "preferences.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <imgui.h>

#include <SDL3/SDL_scancode.h>
#include <nlohmann/json.hpp>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace world_explorer::viewer {

std::array<int, 7> default_fly_key_bindings() {
    return {SDL_SCANCODE_W,
            SDL_SCANCODE_S,
            SDL_SCANCODE_A,
            SDL_SCANCODE_D,
            SDL_SCANCODE_SPACE,
            SDL_SCANCODE_LCTRL,
            SDL_SCANCODE_LSHIFT};
}

void read_preferences(const std::filesystem::path& path, preferences& settings) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) ||
        std::filesystem::file_size(path, error) > 65536)
        return;
    try {
        std::ifstream input(path);
        const auto K_VALUE = nlohmann::json::parse(input);
        if (K_VALUE.at("schema_version") != 1) return;
        preferences candidate;
        candidate.left = K_VALUE.value("left_panel", true);
        candidate.right = K_VALUE.value("right_panel", true);
        candidate.controls = K_VALUE.value("controls", true);
        const auto& keys = K_VALUE.at("keys");
        if (!keys.is_array() || keys.size() != candidate.keys.size() ||
            !std::ranges::all_of(keys, [](const nlohmann::json& key) {
                return key.is_number_integer() && key > 0 && key < SDL_SCANCODE_COUNT;
            }))
            return;
        // Saved bindings are explicit user preferences, even when they match an old default.
        candidate.keys = keys.get<std::array<int, 7>>();
        settings = candidate;
    } catch (const std::exception&) {
        // Invalid/unknown settings retain the caller's current defaults atomically.
        return;
    }
}

Result<void> save_preferences(const std::filesystem::path& path, const preferences& settings) {
    if (path.empty()) return {};
    try {
        const auto K_TEMPORARY = std::filesystem::path(path.string() + ".tmp");
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        {
            std::ofstream output(K_TEMPORARY, std::ios::binary | std::ios::trunc);
            output << nlohmann::json {{"schema_version", 1},
                                      {"left_panel", settings.left},
                                      {"right_panel", settings.right},
                                      {"controls", settings.controls},
                                      {"keys", settings.keys}}
                          .dump(2);
            output.flush();
            if (!output) throw std::runtime_error("Cannot write preferences");
        }
#ifdef _WIN32
        // Windows rename does not replace an existing destination.
        if (!MoveFileExW(K_TEMPORARY.c_str(),
                         path.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot replace preferences");
#else
        std::filesystem::rename(K_TEMPORARY, path);
#endif
        return {};
    } catch (const std::exception& error) {
        return fail(ErrorCode::IO_ERROR, error.what());
    }
}

void apply_theme(float scale) {
    ImGui::GetStyle() = ImGuiStyle {};
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowPadding = { 16, 16 };
    style.FramePadding = { 8, 6 };
    style.ItemSpacing = { 8, 8 };
    style.WindowRounding = 5;
    style.FrameRounding = 4;
    style.Colors[ImGuiCol_WindowBg] = { 0.075F, 0.085F, 0.10F, 0.98F };
    style.Colors[ImGuiCol_Button] = { 0.10F, 0.28F, 0.34F, 1 };
    style.Colors[ImGuiCol_ButtonHovered] = { 0.13F, 0.40F, 0.48F, 1 };
    style.Colors[ImGuiCol_ButtonActive] = { 0.13F, 0.47F, 0.55F, 1 };
    style.Colors[ImGuiCol_CheckMark] = { 0.3F, 0.8F, 0.87F, 1 };
    style.ScaleAllSizes(scale);
}

Result<ui_scaling> display_scaling(float display_scale, float pixel_density) {
    if (!std::isfinite(display_scale) || !std::isfinite(pixel_density) || display_scale <= 0 ||
        display_scale > 4 || pixel_density <= 0 || pixel_density > 4)
        return world_explorer::fail(world_explorer::ErrorCode::UNSUPPORTED_OPERATION,
                                    "Unsupported window display scale or pixel density");
    const auto K_ATLAS_SCALE = std::max(1.F, std::ceil(display_scale * 4) / 4);
    return ui_scaling {display_scale / pixel_density,
                       display_scale / (pixel_density * K_ATLAS_SCALE),
                       static_cast<std::size_t>((K_ATLAS_SCALE - 1) * 4)};
}

}  // namespace world_explorer::viewer
