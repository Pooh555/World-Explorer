#include "../gpu/fit_view_selection.hpp"

#include <string>

#include <catch2/catch_test_macros.hpp>

using namespace world_explorer;

TEST_CASE("calibrated diagnostic fit indices preserve explicit order and all camera boundaries") {
    const auto K_SELECTED = parse_fit_view_indices("79,0,40", 80);
    REQUIRE(K_SELECTED);
    REQUIRE(*K_SELECTED == std::vector<std::size_t> {79, 0, 40});
    const auto K_SINGLE = parse_fit_view_indices("0", 3);
    REQUIRE(K_SINGLE);
    REQUIRE(*K_SINGLE == std::vector<std::size_t> {0});
    std::string all;
    for (std::size_t index { 0uz }; index < 120uz; ++index) {
        if (index != 0) all += ',';
        all += std::to_string(index);
    }
    const auto K_COMPLETE = parse_fit_view_indices(all, 120);
    REQUIRE(K_COMPLETE);
    REQUIRE(K_COMPLETE->size() == 120);
    for (std::size_t index { 0uz }; index < 120uz; ++index) REQUIRE(K_COMPLETE->at(index) == index);
}

TEST_CASE("calibrated diagnostic fit indices reject ambiguous unbounded and duplicate selections") {
    for (const auto* input : {"",   ",",    "0,", ",0",  "0,,1", "0,0",  "1,0,1",
                              "-1", "+1",   "01", "000", "1.0",  "true", " 1",
                              "1 ", "1, 2", "80", "120", "999",  "1000", "18446744073709551616",
                              "0\n"}) {
        INFO(input);
        const auto K_REJECTED = parse_fit_view_indices(input, 80);
        REQUIRE(!K_REJECTED.has_value());
        REQUIRE(K_REJECTED.error().m_code == ErrorCode::INVALID_ARGUMENT);
    }
    REQUIRE(!parse_fit_view_indices(std::string(480, '0'), 120).has_value());
    REQUIRE(!parse_fit_view_indices("0", 2).has_value());
    REQUIRE(!parse_fit_view_indices("0", 121).has_value());
    REQUIRE(!parse_fit_view_indices(std::string_view("0\0,1", 4), 80).has_value());
}
