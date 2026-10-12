#include "preferences.hpp"
#include <SDL3/SDL_scancode.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>

using namespace world_explorer::viewer;
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
