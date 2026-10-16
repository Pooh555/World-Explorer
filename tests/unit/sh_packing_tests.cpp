#include "world_explorer/render_core/sh_packing.hpp"
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>

using namespace world_explorer;

TEST_CASE("SH binary16 preserves every finite binary16 value", "[render][packing]") {
    // Independent arithmetic decode, including both signs of zero/subnormals.
    for (std::uint32_t bits = 0; bits < 65536; ++bits) {
        const auto exponent = (bits >> 10U) & 31U;
        if (exponent == 31)
            continue;
        const auto fraction = bits & 1023U;
        const auto magnitude = exponent == 0 ? std::ldexp(static_cast<float>(fraction), -24)
                                             : std::ldexp(1.F + static_cast<float>(fraction) / 1024,
                                                          static_cast<int>(exponent) - 15);
        const auto value = bits & 0x8000U ? -magnitude : magnitude;
        REQUIRE(encode_finite_binary16(value) == bits);
    }
}

TEST_CASE("SH binary16 rounds ties to even across storage boundaries", "[render][packing]") {
    for (const float sign : {1.F, -1.F}) {
        const auto sign_bit = sign < 0 ? 0x8000U : 0U;
        const auto check_tie = [&](std::uint32_t low, float tie, std::uint32_t even) {
            REQUIRE(encode_finite_binary16(sign * tie) == (sign_bit | even));
            REQUIRE(encode_finite_binary16(sign * std::nextafter(tie, 0.F)) == (sign_bit | low));
            REQUIRE(encode_finite_binary16(sign * std::nextafter(tie, INFINITY)) ==
                    (sign_bit | (low + 1)));
        };
        check_tie(0, std::ldexp(1.F, -25), 0);
        check_tie(1, std::ldexp(3.F, -25), 2);
        check_tie(1023, std::ldexp(2047.F, -25), 1024);
        check_tie(0x3c00, 1.F + std::ldexp(1.F, -11), 0x3c00);
        check_tie(0x3c01, 1.F + std::ldexp(3.F, -11), 0x3c02);
        check_tie(0x3fff, 2.F - std::ldexp(1.F, -11), 0x4000);
        REQUIRE(encode_finite_binary16(sign * std::numeric_limits<float>::denorm_min()) ==
                sign_bit);
    }
}

TEST_CASE("SH packing keeps coefficient order and rejects finite overflow", "[render][packing]") {
    std::array<float, 48> sh{};
    for (std::size_t index = 0; index < sh.size(); ++index)
        sh[index] = index % 2 ? -static_cast<float>(index) : static_cast<float>(index);
    const auto original = sh;
    const auto packed = pack_sh_binary16(sh);
    REQUIRE(packed);
    REQUIRE(sh == original);
    for (std::size_t index = 0; index < sh.size(); ++index)
        REQUIRE(encode_finite_binary16(sh[index]) ==
                ((*packed)[index / 2] >> (16 * (index % 2)) & 0xffffU));
    for (const float invalid : {std::nextafter(65504.F, INFINITY), -65505.F, INFINITY, -INFINITY,
                                std::numeric_limits<float>::quiet_NaN()}) {
        sh[47] = invalid;
        const auto result = pack_sh_binary16(sh);
        REQUIRE(!result.has_value());
        REQUIRE(result.error().code == error_code::INVALID_ARGUMENT);
    }
    REQUIRE(original[47] == -47.F);
}
