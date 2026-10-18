#include "world_explorer/render_core/timing.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace world_explorer;

TEST_CASE("GPU timestamp durations validate scales and handle counter wrapping") {
    REQUIRE(timestamp_duration_ms(10, 30, 64, 50000).value() == 1);
    REQUIRE(timestamp_duration_ms(0xfffffff0, 0x10, 32, 31250).value() == 1);
    REQUIRE(timestamp_duration_ms(0xfffffffffffffff0ULL, 0x10, 64, 31250).value() == 1);
    REQUIRE(timestamp_duration_ms(0x1ffffff0, 0x2ffffff8, 24, 125000).value() == 1);
    REQUIRE(timestamp_duration_ms(99, 99, 64, 1).value() == 0);
    REQUIRE(!timestamp_duration_ms(0, 1, 0, 1));
    REQUIRE(!timestamp_duration_ms(0, 1, 65, 1));
    REQUIRE(!timestamp_duration_ms(0, 1, 64, 0));
    REQUIRE(!timestamp_duration_ms(0, 1, 64, -1));
    REQUIRE(!timestamp_duration_ms(0, 1, 64, std::numeric_limits<double>::infinity()));
    REQUIRE(!timestamp_duration_ms(0, 1, 64, std::numeric_limits<double>::quiet_NaN()));
    REQUIRE(!timestamp_duration_ms(
        0, std::numeric_limits<std::uint64_t>::max(), 64, std::numeric_limits<double>::max()));
}
