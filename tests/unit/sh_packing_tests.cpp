#include "world_explorer/render_core/sh_packing.hpp"

#include <cmath>
#include <limits>

#include <catch2/catch_test_macros.hpp>

using namespace world_explorer;

TEST_CASE("SH binary16 preserves every finite binary16 value", "[render][packing]") {
    // Independent arithmetic decode, including both signs of zero/subnormals.
    for (std::uint32_t bits { 0 }; bits < 65536; ++bits) {
        const auto K_EXPONENT = (bits >> 10U) & 31U;
        if (K_EXPONENT == 31) continue;
        const auto K_FRACTION = bits & 1023U;
        const auto K_MAGNITUDE = K_EXPONENT == 0
                                     ? std::ldexp(static_cast<float>(K_FRACTION), -24)
                                     : std::ldexp(1.F + static_cast<float>(K_FRACTION) / 1024,
                                                  static_cast<int>(K_EXPONENT) - 15);
        const auto K_VALUE = bits & 0x8000U ? -K_MAGNITUDE : K_MAGNITUDE;
        REQUIRE(encode_finite_binary16(K_VALUE) == bits);
    }
}

TEST_CASE("SH binary16 rounds ties to even across storage boundaries", "[render][packing]") {
    for (const float K_SIGN : {1.F, -1.F}) {
        const auto K_SIGN_BIT = K_SIGN < 0 ? 0x8000U : 0U;
        const auto K_CHECK_TIE = [&](std::uint32_t low, float tie, std::uint32_t even) {
            REQUIRE(encode_finite_binary16(K_SIGN * tie) == (K_SIGN_BIT | even));
            REQUIRE(encode_finite_binary16(K_SIGN * std::nextafter(tie, 0.F)) ==
                    (K_SIGN_BIT | low));
            REQUIRE(encode_finite_binary16(K_SIGN * std::nextafter(tie, INFINITY)) ==
                    (K_SIGN_BIT | (low + 1)));
        };
        K_CHECK_TIE(0, std::ldexp(1.F, -25), 0);
        K_CHECK_TIE(1, std::ldexp(3.F, -25), 2);
        K_CHECK_TIE(1023, std::ldexp(2047.F, -25), 1024);
        K_CHECK_TIE(0x3c00, 1.F + std::ldexp(1.F, -11), 0x3c00);
        K_CHECK_TIE(0x3c01, 1.F + std::ldexp(3.F, -11), 0x3c02);
        K_CHECK_TIE(0x3fff, 2.F - std::ldexp(1.F, -11), 0x4000);
        REQUIRE(encode_finite_binary16(K_SIGN * std::numeric_limits<float>::denorm_min()) ==
                K_SIGN_BIT);
    }
}

TEST_CASE("SH packing keeps coefficient order and rejects finite overflow", "[render][packing]") {
    std::array<float, 48> sh {};
    for (std::size_t index { 0uz }; index < sh.size(); ++index)
        sh[index] = index % 2 ? -static_cast<float>(index) : static_cast<float>(index);
    const auto K_ORIGINAL = sh;
    const auto K_PACKED = pack_sh_binary16(sh);
    REQUIRE(K_PACKED);
    REQUIRE(sh == K_ORIGINAL);
    for (std::size_t index { 0uz }; index < sh.size(); ++index)
        REQUIRE(encode_finite_binary16(sh[index]) ==
                ((*K_PACKED)[index / 2] >> (16 * (index % 2)) & 0xffffU));
    for (const float K_INVALID : {std::nextafter(65504.F, INFINITY),
                                  -65505.F,
                                  INFINITY,
                                  -INFINITY,
                                  std::numeric_limits<float>::quiet_NaN()}) {
        sh[47] = K_INVALID;
        const auto K_RESULT = pack_sh_binary16(sh);
        REQUIRE(!K_RESULT.has_value());
        REQUIRE(K_RESULT.error().m_code == ErrorCode::INVALID_ARGUMENT);
    }
    REQUIRE(K_ORIGINAL[47] == -47.F);
}
