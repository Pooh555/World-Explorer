#include "preferences.hpp"

#include <chrono>
#include <cmath>
#include <fstream>
#include <imgui.h>
#include <limits>

#include <SDL3/SDL_scancode.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <nlohmann/json.hpp>

using namespace world_explorer::viewer;

TEST_CASE("window scaling separates physical font pixels from logical layout coordinates",
          "[dpi]") {
    const auto K_NATIVE = display_scaling(1, 1);
    REQUIRE(K_NATIVE);
    CHECK(K_NATIVE->layout_scale == 1);
    CHECK(K_NATIVE->font_scale == 1);
    const auto K_WINDOWS = display_scaling(2, 1);
    REQUIRE(K_WINDOWS);
    CHECK(K_WINDOWS->layout_scale == 2);
    const auto K_RETINA = display_scaling(2, 2);
    REQUIRE(K_RETINA);
    CHECK(K_RETINA->layout_scale == 1);
    CHECK(K_RETINA->font_scale == 0.5F);
    CHECK(K_RETINA->font_index == K_WINDOWS->font_index);
    for (const auto K_SCALE : {1.25F, 1.5F, 1.75F, 2.F, 2.25F, 3.F, 4.F}) {
        const auto K_METRICS = display_scaling(K_SCALE, K_SCALE);
        REQUIRE(K_METRICS);
        CHECK(K_METRICS->layout_scale == 1);
        CHECK(K_METRICS->font_index < 13);
        const auto K_PHYSICAL_FONT = (1.F + static_cast<float>(K_METRICS->font_index) * 0.25F);
        CHECK(std::abs(K_PHYSICAL_FONT * K_METRICS->font_scale * K_SCALE - K_SCALE) < 0.0001F);
    }
    for (const auto K_INVALID : {0.F,
                                 -1.F,
                                 5.F,
                                 std::numeric_limits<float>::infinity(),
                                 std::numeric_limits<float>::quiet_NaN()}) {
        CHECK_FALSE(display_scaling(K_INVALID, 1));
        CHECK_FALSE(display_scaling(1, K_INVALID));
    }
}

TEST_CASE("theme resize uses absolute scale without accumulating style multipliers", "[dpi]") {
    ImGui::CreateContext();

    struct ContextOwner {
        ~ContextOwner() { ImGui::DestroyContext(); }
    } owner;

    apply_theme(1);
    const auto K_PADDING = ImGui::GetStyle().WindowPadding;
    apply_theme(2);
    CHECK(ImGui::GetStyle().WindowPadding.x == K_PADDING.x * 2);
    apply_theme(1);
    CHECK(ImGui::GetStyle().WindowPadding.x == K_PADDING.x);
    apply_theme(2);
    CHECK(ImGui::GetStyle().WindowPadding.y == K_PADDING.y * 2);
}

namespace {

struct PreferencesFixture {
    std::filesystem::path m_file =
        std::filesystem::temp_directory_path() /
        ("we_preferences_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");

    ~PreferencesFixture() {
        std::error_code error;
        std::filesystem::remove(m_file, error);
    }
};

}  // namespace

TEST_CASE("viewer preferences preserve specification legacy and custom bindings on reload") {
    PreferencesFixture fixture;
    const auto K_CONFIGURATION = GENERATE(0, 1, 2);
    preferences saved;
    saved.keys = { SDL_SCANCODE_W,
                  SDL_SCANCODE_S,
                  SDL_SCANCODE_A,
                  SDL_SCANCODE_D,
                  SDL_SCANCODE_SPACE,
                  SDL_SCANCODE_LCTRL,
                  SDL_SCANCODE_LSHIFT };
    if (K_CONFIGURATION == 1) std::swap(saved.keys[5], saved.keys[6]);
    if (K_CONFIGURATION == 2) saved.keys[0] = SDL_SCANCODE_UP;
    saved.left = false;
    saved.right = false;
    saved.controls = false;
    REQUIRE(save_preferences(fixture.m_file, saved));
    preferences loaded;
    read_preferences(fixture.m_file, loaded);
    CHECK(loaded.keys == saved.keys);
    CHECK(loaded.left == saved.left);
    CHECK(loaded.right == saved.right);
    CHECK(loaded.controls == saved.controls);
}

TEST_CASE("invalid viewer preferences cannot partially replace caller settings") {
    PreferencesFixture fixture;
    preferences current;
    current.keys[0] = SDL_SCANCODE_UP;
    current.right = false;
    auto json =
        nlohmann::json {{"schema_version", 1}, {"left_panel", false}, {"keys", current.keys}};
    SECTION("unknown schema") {
        json["schema_version"] = 2;
    }
    SECTION("short key list") {
        json["keys"].erase(0);
    }
    SECTION("long key list") {
        json["keys"].push_back(SDL_SCANCODE_W);
    }
    SECTION("fractional key") {
        json["keys"][0] = 26.5;
    }
    SECTION("overflowing key") {
        json["keys"][0] = 4294967322ULL;
    }
    SECTION("invalid key") {
        json["keys"][0] = -1;
    }
    SECTION("wrong key type") {
        json["keys"][0] = "W";
    }
    {
        std::ofstream output(fixture.m_file);
        output << json.dump();
    }
    const auto K_KEYS = current.keys;
    read_preferences(fixture.m_file, current);
    CHECK(current.keys == K_KEYS);
    CHECK(current.left);
    CHECK_FALSE(current.right);
}
