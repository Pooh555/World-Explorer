#include "preferences.hpp"
#include <SDL3/SDL_scancode.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <chrono>
#include <cmath>
#include <fstream>
#include <imgui.h>
#include <limits>
#include <nlohmann/json.hpp>

using namespace world_explorer::viewer;
TEST_CASE("window scaling separates physical font pixels from logical layout coordinates",
          "[dpi]") {
    const auto native = display_scaling(1, 1);
    REQUIRE(native);
    CHECK(native->layout_scale == 1);
    CHECK(native->font_scale == 1);
    const auto windows = display_scaling(2, 1);
    REQUIRE(windows);
    CHECK(windows->layout_scale == 2);
    const auto retina = display_scaling(2, 2);
    REQUIRE(retina);
    CHECK(retina->layout_scale == 1);
    CHECK(retina->font_scale == 0.5F);
    CHECK(retina->font_index == windows->font_index);
    for (const auto scale : {1.25F, 1.5F, 1.75F, 2.F, 2.25F, 3.F, 4.F}) {
        const auto metrics = display_scaling(scale, scale);
        REQUIRE(metrics);
        CHECK(metrics->layout_scale == 1);
        CHECK(metrics->font_index < 13);
        const auto physical_font = (1.F + static_cast<float>(metrics->font_index) * 0.25F);
        CHECK(std::abs(physical_font * metrics->font_scale * scale - scale) < 0.0001F);
    }
    for (const auto invalid : {0.F, -1.F, 5.F, std::numeric_limits<float>::infinity(),
                               std::numeric_limits<float>::quiet_NaN()}) {
        CHECK_FALSE(display_scaling(invalid, 1));
        CHECK_FALSE(display_scaling(1, invalid));
    }
}
TEST_CASE("theme resize uses absolute scale without accumulating style multipliers", "[dpi]") {
    ImGui::CreateContext();
    struct context_owner {
        ~context_owner() { ImGui::DestroyContext(); }
    } owner;
    apply_theme(1);
    const auto padding = ImGui::GetStyle().WindowPadding;
    apply_theme(2);
    CHECK(ImGui::GetStyle().WindowPadding.x == padding.x * 2);
    apply_theme(1);
    CHECK(ImGui::GetStyle().WindowPadding.x == padding.x);
    apply_theme(2);
    CHECK(ImGui::GetStyle().WindowPadding.y == padding.y * 2);
}
namespace {
struct preferences_fixture {
    std::filesystem::path file =
        std::filesystem::temp_directory_path() /
        ("we_preferences_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
    ~preferences_fixture() {
        std::error_code error;
        std::filesystem::remove(file, error);
    }
};
} // namespace
TEST_CASE("viewer preferences preserve specification legacy and custom bindings on reload") {
    preferences_fixture fixture;
    const auto configuration = GENERATE(0, 1, 2);
    preferences saved;
    saved.keys = {SDL_SCANCODE_W,     SDL_SCANCODE_S,     SDL_SCANCODE_A,     SDL_SCANCODE_D,
                  SDL_SCANCODE_SPACE, SDL_SCANCODE_LCTRL, SDL_SCANCODE_LSHIFT};
    if (configuration == 1)
        std::swap(saved.keys[5], saved.keys[6]);
    if (configuration == 2)
        saved.keys[0] = SDL_SCANCODE_UP;
    saved.left = false;
    saved.right = false;
    saved.controls = false;
    REQUIRE(save_preferences(fixture.file, saved));
    preferences loaded;
    read_preferences(fixture.file, loaded);
    CHECK(loaded.keys == saved.keys);
    CHECK(loaded.left == saved.left);
    CHECK(loaded.right == saved.right);
    CHECK(loaded.controls == saved.controls);
}
TEST_CASE("invalid viewer preferences cannot partially replace caller settings") {
    preferences_fixture fixture;
    preferences current;
    current.keys[0] = SDL_SCANCODE_UP;
    current.right = false;
    auto json =
        nlohmann::json{{"schema_version", 1}, {"left_panel", false}, {"keys", current.keys}};
    SECTION("unknown schema") { json["schema_version"] = 2; }
    SECTION("short key list") { json["keys"].erase(0); }
    SECTION("long key list") { json["keys"].push_back(SDL_SCANCODE_W); }
    SECTION("fractional key") { json["keys"][0] = 26.5; }
    SECTION("overflowing key") { json["keys"][0] = 4294967322ULL; }
    SECTION("invalid key") { json["keys"][0] = -1; }
    SECTION("wrong key type") { json["keys"][0] = "W"; }
    {
        std::ofstream output(fixture.file);
        output << json.dump();
    }
    const auto keys = current.keys;
    read_preferences(fixture.file, current);
    CHECK(current.keys == keys);
    CHECK(current.left);
    CHECK_FALSE(current.right);
}
