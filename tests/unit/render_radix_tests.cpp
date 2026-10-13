#include "world_explorer/render_core/radix.hpp"
#include <catch2/catch_test_macros.hpp>
#include <limits>

using namespace world_explorer;
TEST_CASE("tile radix passes preserve ping-pong output across key width boundaries") {
    REQUIRE(tile_radix_passes(0) == 0);
    REQUIRE(tile_radix_passes(1) == 2);
    REQUIRE(tile_radix_passes(255) == 2);
    REQUIRE(tile_radix_passes(256) == 4);
    REQUIRE(tile_radix_passes(65535) == 4);
    REQUIRE(tile_radix_passes(65536) == 6);
    REQUIRE(tile_radix_passes(16777215) == 6);
    REQUIRE(tile_radix_passes(16777216) == 8);
    REQUIRE(tile_radix_passes(std::numeric_limits<std::uint32_t>::max()) == 8);
    for (std::uint32_t bit = 0; bit < 32; ++bit) {
        const auto key = std::uint32_t{1} << bit;
        const auto passes = tile_radix_passes(key);
        REQUIRE(passes % 2 == 0);
        REQUIRE(passes * 4 > bit);
    }
}
