#include "../gpu/fit_view_selection.hpp"
#include <catch2/catch_test_macros.hpp>
#include <string>

using namespace world_explorer;

TEST_CASE("calibrated diagnostic fit indices preserve explicit order and all camera boundaries") {
    const auto selected = parse_fit_view_indices("79,0,40", 80);
    REQUIRE(selected);
    REQUIRE(*selected == std::vector<std::size_t>{79, 0, 40});
    const auto single = parse_fit_view_indices("0", 3);
    REQUIRE(single);
    REQUIRE(*single == std::vector<std::size_t>{0});
    std::string all;
    for (std::size_t index = 0; index < 120; ++index) {
        if (index != 0)
            all += ',';
        all += std::to_string(index);
    }
    const auto complete = parse_fit_view_indices(all, 120);
    REQUIRE(complete);
    REQUIRE(complete->size() == 120);
    for (std::size_t index = 0; index < 120; ++index)
        REQUIRE(complete->at(index) == index);
}

TEST_CASE("calibrated diagnostic fit indices reject ambiguous unbounded and duplicate selections") {
    for (const auto* input : {"",   ",",    "0,", ",0",  "0,,1", "0,0",  "1,0,1",
                              "-1", "+1",   "01", "000", "1.0",  "true", " 1",
                              "1 ", "1, 2", "80", "120", "999",  "1000", "18446744073709551616",
                              "0\n"}) {
        INFO(input);
        const auto rejected = parse_fit_view_indices(input, 80);
        REQUIRE(!rejected.has_value());
        REQUIRE(rejected.error().code == error_code::INVALID_ARGUMENT);
    }
    REQUIRE(!parse_fit_view_indices(std::string(480, '0'), 120).has_value());
    REQUIRE(!parse_fit_view_indices("0", 2).has_value());
    REQUIRE(!parse_fit_view_indices("0", 121).has_value());
    REQUIRE(!parse_fit_view_indices(std::string_view("0\0,1", 4), 80).has_value());
}
